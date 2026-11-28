// Deterministic capture driver: same pipeline as main.c, fixed 1/30 s step, scripted keypresses.
// Writes the terminal byte stream to stdout (frames are delimited by the sync-update marker).
//   shotcap COLS ROWS THEME SEED MODE FRAMES        MODE: shot | gif
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "gfx.h"
#include "scene.h"
#include "shell.h"
#include "term.h"
#include "theme.h"
#include "ui.h"
#include "util.h"
#include "world.h"

static World world;
static time_t fake_now;
time_t time(time_t *t) { if (t) *t = fake_now; return fake_now; }   // deterministic HUD clock

#define SIM_DT (1.0f / 60.0f)
#define FPS 30

int main(int argc, char **argv) {
    if (argc < 7) return 2;
    int cols = atoi(argv[1]), rows = atoi(argv[2]), th = atoi(argv[3]);
    uint32_t seed = (uint32_t)strtoul(argv[4], NULL, 10);
    int gif = !strcmp(argv[5], "gif");
    int shell = !strcmp(argv[5], "shell");
    int frames = atoi(argv[6]);

    rng_seed(seed * 2654435761u + 1u);              // same seeding as main -s
    theme_set_now(th);
    term_cols = cols; term_rows = rows;
    gfx_resize(cols, rows);
    world_init(&world, gfx_w, gfx_h);
    for (int i = 0; i < 5; i++) world_add_fish(&world);     // 3 + 5 = 8 fish, like the current screenshots

    fake_now = 12 * 3600;                           // HUD clock starts at 12:00:00 (TZ=UTC)
    double acc = 0;
    for (int f = 0; f < frames; f++) {
        if (gif) {
            if (f == 30)  world_drop_food_burst(&world, world.w * 0.30f, 1.5f);
            if (f == 42)  world_drop_food_burst(&world, world.w * 0.62f, 1.5f);
            if (f == 54)  world_drop_food_burst(&world, world.w * 0.45f, 1.5f);
            if (f == 120) world_bubble_burst(&world, 14);
            if (f == 165) theme_step(+1);           // ocean -> retrowave
            if (f == 270) theme_step(+1);           // retrowave -> sunset
        }
        if (shell) {
            static const char *cmds[] = { "./build/cfish -l\r", "ls\r", "git --no-pager log --oneline -4\r" };
            static const int at[] = { 60, 100, 150 };
            if (f == 20) shell_toggle();
            for (int i = 0; i < 3; i++) if (f == at[i]) shell_input(cmds[i], strlen(cmds[i]));
            shell_pump();
            usleep(1000000 / FPS);                  // real time: the child shell answers in real time
        }
        fake_now = 12 * 3600 + f / FPS;
        acc += 1.0 / FPS;
        theme_update(1.0f / FPS);
        while (acc >= SIM_DT) { world_update(&world, SIM_DT); acc -= SIM_DT; }
        float alpha = (float)(acc / SIM_DT);
        scene_draw(&world, alpha);
        ui_draw(&world, alpha, 0, (float)FPS);
        gfx_present();
    }
    return 0;
}
