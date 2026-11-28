#!/usr/bin/env python3
"""Regenerate README assets (demo.gif, screenshot1-6.png, screenshot_shell.png) from the real cfish code.

Nothing here is drawn by hand: shotcap.c runs the actual pipeline (scene_draw -> ui_draw -> gfx_present) with a
fixed time step and scripted keypresses, render.py replays the terminal byte stream and rasterizes the cells
(8x16 px, half-block = two 8x8 pixels, text in DejaVu Sans Mono).

Needs: gcc, make, python3 + Pillow + numpy, ffmpeg.
    python3 tools/assets/make_assets.py --cfish-dir . --out assets
"""
import argparse, os, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render
from PIL import Image

THEMES = ['ocean', 'retrowave', 'sunset', 'abyss', 'matrix', 'gruvbox']   # order of theme_names in theme.c
SEED = 2                  # composition (fish positions); picked by eye from a contact sheet of seeds
SHOT_COLS, SHOT_ROWS = 238, 67          # 1920x1080 window with 8x16 cells
SHOT_FRAMES = 330                       # ~11 s of simulation before the shot
GIF_COLS, GIF_ROWS, GIF_FRAMES, GIF_FPS = 120, 34, 400, 15    # 960x544, sampled from the 30 fps capture

def run(cmd, **kw):
    subprocess.run(cmd, check=True, **kw)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cfish-dir', default='.'); ap.add_argument('--out', default='assets')
    ap.add_argument('--only', choices=['shots', 'shell', 'gif'], help='build just one group')
    a = ap.parse_args()
    cfish = os.path.abspath(a.cfish_dir); out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix='cfish-assets-')
    try:
        srcs = [f for f in sorted(os.listdir(cfish)) if f.endswith('.c') and f != 'main.c']
        exe = os.path.join(tmp, 'shotcap')
        run(['gcc', '-std=gnu11', '-O2', '-DNDEBUG', '-I.', '-o', exe, os.path.join(HERE, 'shotcap.c')] + srcs + ['-lm'], cwd=cfish)
        env = dict(os.environ, TZ='UTC')           # the HUD clock is faked (starts 12:00:00) but formatted via localtime

        def capture(cols, rows, theme, mode, frames, extra_env=None, name='x'):
            path = os.path.join(tmp, name + '.bin')
            with open(path, 'wb') as f:
                run([exe, str(cols), str(rows), str(theme), str(SEED), mode, str(frames)], stdout=f, cwd=cfish, env=dict(env, **(extra_env or {})))
            return path

        def last_frame(path, cols, rows):
            last = None
            for scr in render.frames(path, cols, rows): last = scr
            return last

        if a.only in (None, 'shots'):
            for i, _ in enumerate(THEMES):
                p = capture(SHOT_COLS, SHOT_ROWS, i, 'shot', SHOT_FRAMES, name=f'shot{i}')
                im = render.with_border(last_frame(p, SHOT_COLS, SHOT_ROWS).image())
                im.save(os.path.join(out, f'screenshot{i + 1}.png'), optimize=True)
                print('screenshot%d.png (%s)' % (i + 1, THEMES[i]))

        if a.only in (None, 'shell'):
            run(['make', '-s'], cwd=cfish)           # the demo runs ./build/cfish -l inside the panel
            home = os.path.join(tmp, 'home'); os.makedirs(home)
            open(os.path.join(home, '.bashrc'), 'w').write(
                "PS1='\\[\\e[1;32m\\]user@cfish\\[\\e[0m\\] \\[\\e[1;34m\\]~/cfish\\[\\e[0m\\] \\[\\e[33m\\](master)\\[\\e[0m\\] $ '\n"
                "alias ls='ls --color=auto'\n")
            p = capture(SHOT_COLS, SHOT_ROWS, 1, 'shell', 260, name='shell',
                        extra_env=dict(HOME=home, SHELL='/bin/bash', TERM='xterm-256color'))   # real time: ~9 s
            im = render.with_border(last_frame(p, SHOT_COLS, SHOT_ROWS).image())
            im.save(os.path.join(out, 'screenshot_shell.png'), optimize=True); print('screenshot_shell.png')

        if a.only in (None, 'gif'):
            p = capture(GIF_COLS, GIF_ROWS, 0, 'gif', GIF_FRAMES, name='gif')
            fdir = os.path.join(tmp, 'frames'); os.makedirs(fdir); step = 30 // GIF_FPS; n = 0
            for i, scr in enumerate(render.frames(p, GIF_COLS, GIF_ROWS)):
                if i % step == 0:
                    scr.image().save(os.path.join(fdir, 'f%04d.png' % n)); n += 1
            run(['ffmpeg', '-y', '-loglevel', 'error', '-framerate', str(GIF_FPS), '-i', os.path.join(fdir, 'f%04d.png'),
                 '-vf', 'split[a][b];[a]palettegen=max_colors=256:stats_mode=full[p];[b][p]paletteuse=dither=none:diff_mode=rectangle',
                 '-loop', '0', os.path.join(out, 'demo.gif')])
            print('demo.gif: %d frames, %.1f MB' % (n, os.path.getsize(os.path.join(out, 'demo.gif')) / 1e6))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == '__main__':
    main()
