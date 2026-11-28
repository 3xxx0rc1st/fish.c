#!/usr/bin/env python3
"""Replay aquash's terminal byte stream and render cell grids to images (cell = 8x16, half-block = 8x8 pixels)."""
import re, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

CW, CH = 8, 16
FONT = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 13)
TOK = re.compile(r'\x1b\[([0-9;?]*)([A-Za-z])|([^\x1b]+)')
SYNC = '\x1b[?2026h'

class Screen:
    def __init__(self, cols, rows):
        self.cols, self.rows = cols, rows
        self.ch = [[' '] * cols for _ in range(rows)]
        self.fg = np.zeros((rows, cols, 3), np.uint8)
        self.bg = np.zeros((rows, cols, 3), np.uint8)
        self.x = self.y = 0
        self.cfg = (200, 200, 200); self.cbg = (0, 0, 0)

    def feed(self, s):
        cols, rows = self.cols, self.rows
        for m in TOK.finditer(s):
            if m.group(3) is not None:
                for ch in m.group(3):
                    if 0 <= self.y < rows and 0 <= self.x < cols:
                        self.ch[self.y][self.x] = ch
                        self.fg[self.y, self.x] = self.cfg
                        self.bg[self.y, self.x] = self.cbg
                    if self.x < cols: self.x += 1
                continue
            a, c = m.group(1), m.group(2)
            if c == 'H':
                r, cc = a.split(';'); self.y = int(r) - 1; self.x = int(cc) - 1
            elif c == 'C':
                self.x += int(a or 1)
            elif c == 'm':
                v = [int(t) for t in a.split(';') if t != '']
                k = 0
                while k < len(v):
                    if v[k] in (38, 48) and k + 1 < len(v) and v[k + 1] == 2:
                        col = tuple(v[k + 2:k + 5])
                        if v[k] == 38: self.cfg = col
                        else: self.cbg = col
                        k += 5
                    elif v[k] == 0: self.cfg = (200, 200, 200); self.cbg = (0, 0, 0); k += 1
                    else: k += 1
            elif c == 'J':
                for r in range(rows): self.ch[r] = [' '] * cols
                self.fg[:] = 0; self.bg[:] = 0

    def image(self):
        rows, cols = self.rows, self.cols
        half = np.array([[c == '\u2580' for c in row] for row in self.ch])
        img = np.empty((rows, CH, cols, CW, 3), np.uint8)
        img[:] = self.bg[:, None, :, None, :]
        top = img[:, :CH // 2]
        top[:] = np.where(half[:, None, :, None, None], self.fg[:, None, :, None, :], top)
        im = Image.fromarray(img.reshape(rows * CH, cols * CW, 3), 'RGB')
        d = ImageDraw.Draw(im)
        for y in range(rows):
            for x in range(cols):
                c = self.ch[y][x]
                if c != ' ' and c != '\u2580':
                    d.text((x * CW, y * CH + 12), c, font=FONT, fill=tuple(int(v) for v in self.fg[y, x]), anchor='ls')
        return im

def frames(path, cols, rows):
    data = open(path, 'rb').read().decode('utf-8', 'replace')
    parts = data.split(SYNC)
    scr = Screen(cols, rows)
    scr.feed(parts[0])                       # initial clear
    for p in parts[1:]:
        scr.feed(p)
        yield scr

def with_border(im, size=(1920, 1080), color=(28, 28, 31)):
    canvas = Image.new('RGB', size, color)
    canvas.paste(im, ((size[0] - im.width) // 2, (size[1] - im.height) // 2))
    return canvas
