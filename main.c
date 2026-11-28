// cfish - terminal aquarium in C
// make && ./build/cfish [-f fps] [-t theme] [-s seed] [-e level] [-g mode] [-T] [-l]   (s / F12 - shell over the water)
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gfx.h"
#include "input.h"
#include "kgp.h"
#include "scene.h"
#include "shell.h"
#include "term.h"
#include "ui.h"
#include "theme.h"
#include "util.h"
#include "vt.h"
#include "world.h"

#define SIM_DT (1.0f / 60.0f) // fixed simulation step

static World world;

// Sleep until the next frame, but stay awake for the shell: its output goes into the
// emulator at once, and while the panel is open typing is forwarded immediately and a
// frame is drawn soon after (not at the next 1/fps tick) - so echo does not lag.
static void wait_frame(double start, double target, int *intro) {
    const double min_gap = target / 3;
    for (;;) {
        double left = start + target - now_s();
        if (left <= 0 || term_quit || term_resized) return;
        struct pollfd pf[2];
        int n = 0, shown = shell_visible();
        if (shell_fd() >= 0) pf[n++] = (struct pollfd){shell_fd(), POLLIN, 0};
        if (shown) pf[n++] = (struct pollfd){STDIN_FILENO, POLLIN, 0};
        int r = poll(pf, (nfds_t)n, (int)ceil(left * 1000.0));
        if (r < 0 && errno != EINTR) return;
        if (r <= 0) continue;
        if (shown && (pf[n - 1].revents & POLLIN)) input_poll(&world, intro);
        shell_pump();
        if (shell_visible() && now_s() - start >= min_gap) return;
    }
}

static void usage(FILE *o) {
    fprintf(o,
        "usage: cfish [-f fps] [-t theme] [-s seed] [-e level] [-g mode] [-T] [-l]\n"
        "  -f fps    frame rate, 5..120 (default 30)\n"
        "  -t theme  start palette (see -l); 't' / 'T' switch it in the aquarium\n"
        "  -s seed   random seed\n"
        "  -e level  truecolor redraw tolerance 0..32 (default 0 = exact); 4..8 cuts traffic a lot\n"
        "  -g mode   kitty graphics image for the water: auto (default: ask the terminal), on, off.\n"
        "            Local terminals with the kitty protocol only (kitty, Ghostty, ...); the rest is unchanged\n"
        "  -T        do not touch the Linux console (font, palette)\n"
        "  -l        list palettes\n");
}

int main(int argc, char **argv) {
    int fps_target = 30, theme0 = 0, no_vt = 0, have_seed = 0;
    KgpMode kmode = KGP_AUTO;
    uint32_t seed = 0;
    int opt;
    while ((opt = getopt(argc, argv, "f:t:s:e:g:Tlh")) != -1) {
        switch (opt) {
            case 'f': fps_target = atoi(optarg); break;
            case 't':
                theme0 = theme_find(optarg);
                if (theme0 < 0) { fprintf(stderr, "cfish: no such palette '%s' (see -l)\n", optarg); return 1; }
                break;
            case 'e': gfx_tol = atoi(optarg); if (gfx_tol < 0) gfx_tol = 0; if (gfx_tol > 32) gfx_tol = 32; break;
            case 'g':
                if (!strcmp(optarg, "auto")) kmode = KGP_AUTO;
                else if (!strcmp(optarg, "on")) kmode = KGP_ON;
                else if (!strcmp(optarg, "off")) kmode = KGP_OFF;
                else { fprintf(stderr, "cfish: -g wants auto, on or off\n"); return 1; }
                break;
            case 's': seed = (uint32_t)strtoul(optarg, NULL, 10); have_seed = 1; break;
            case 'T': no_vt = 1; break;
            case 'l': for (int i = 0; i < theme_count(); i++) puts(theme_name(i)); return 0;
            case 'h': usage(stdout); return 0;
            default: usage(stderr); return 1;
        }
    }
    if (fps_target < 5) fps_target = 5;
    if (fps_target > 120) fps_target = 120;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "cfish: needs real terminal (tty)\n");
        return 1;
    }
    rng_seed(have_seed ? seed * 2654435761u + 1u : (uint32_t)time(NULL) * 2654435761u);
    theme_set_now(theme0);

    term_init();
    vt_init(!no_vt);    // Linux console: font + palette (restored in term_restore)
    if (!vt_active) kgp_init(kmode);    // asks the terminal; the console has no use for it
    term_size();
    gfx_resize(term_cols, term_rows);
    world_init(&world, gfx_w, gfx_h);
    term_resized = 0;

    int intro = 1;
    float fps = 0;
    double last = now_s(), acc = 0;

    while (!term_quit) {
        double start = now_s();
        double dt = start - last;
        last = start;
        if (dt > 0.25) dt = 0.25;
        acc += dt;

        if (term_resized) {
            term_resized = 0;
            term_size();
            gfx_resize(term_cols, term_rows);
            world_resize(&world, gfx_w, gfx_h);
            shell_resize();
        }
        input_poll(&world, &intro);
        shell_pump();
        theme_update((float)dt);
        if (vt_active) {
            Col pal[16];
            theme_vt_palette(theme_cur(), pal);
            gfx_vt_palette(pal);
        }

        if (term_cols >= 24 && term_rows >= 10) {
            while (acc >= SIM_DT) { world_update(&world, SIM_DT); acc -= SIM_DT; }
            float alpha = (float)(acc / SIM_DT);
            scene_draw(&world, alpha);
            ui_draw(&world, alpha, intro, fps);
            gfx_present();
        } else {
            acc = 0;
        }

        if (dt > 1e-4) fps = fps * 0.95f + (float)(1.0 / dt) * 0.05f;

        wait_frame(start, 1.0 / fps_target, &intro);
    }
    term_restore();
    return 0;
}
