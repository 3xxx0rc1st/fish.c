// fish - terminal aquarium in C
// make && ./fish [-f fps] [-t theme] [-s seed] [-T] [-l]
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gfx.h"
#include "input.h"
#include "scene.h"
#include "term.h"
#include "ui.h"
#include "theme.h"
#include "util.h"
#include "vt.h"
#include "world.h"

#define SIM_DT (1.0f / 60.0f) // fixed simulation step

static World world;

static void usage(FILE *o) {
    fprintf(o,
        "usage: fish [-f fps] [-t theme] [-s seed] [-T] [-l]\n"
        "  -f fps    frame rate, 5..120 (default 30)\n"
        "  -t theme  start palette (see -l); 't' / 'T' switch it in the aquarium\n"
        "  -s seed   random seed\n"
        "  -T        do not touch the Linux console (font, palette)\n"
        "  -l        list palettes\n");
}

int main(int argc, char **argv) {
    int fps_target = 30, theme0 = 0, no_vt = 0, have_seed = 0;
    uint32_t seed = 0;
    int opt;
    while ((opt = getopt(argc, argv, "f:t:s:Tlh")) != -1) {
        switch (opt) {
            case 'f': fps_target = atoi(optarg); break;
            case 't':
                theme0 = theme_find(optarg);
                if (theme0 < 0) { fprintf(stderr, "fish: no such palette '%s' (see -l)\n", optarg); return 1; }
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
        fprintf(stderr, "fish: needs real terminal (tty)\n");
        return 1;
    }
    rng_seed(have_seed ? seed * 2654435761u + 1u : (uint32_t)time(NULL) * 2654435761u);
    theme_set_now(theme0);

    term_init();
    vt_init(!no_vt);    // Linux console: font + palette (restored in term_restore)
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
        }
        input_poll(&world, &intro);
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

        double target = 1.0 / fps_target, el = now_s() - start;
        if (el < target) {
            double s = target - el;
            struct timespec ts = {(time_t)s, (long)((s - (time_t)s) * 1e9)};
            nanosleep(&ts, NULL);
        }
    }
    term_restore();
    return 0;
}
