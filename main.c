// fish - terminal aquarium in C
// make && ./fish [-f fps]
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
#include "util.h"
#include "world.h"

#define SIM_DT (1.0f / 60.0f) // fixed simulation step

static World world;

int main(int argc, char **argv) {
    int fps_target = 30;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "-f")) fps_target = atoi(argv[i + 1]);
    if (fps_target < 5) fps_target = 5;
    if (fps_target > 120) fps_target = 120;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "fish: needs real terminal (tty)\n");
        return 1;
    }
    rng_seed((uint32_t)time(NULL) * 2654435761u);

    term_init();
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
