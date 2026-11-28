#define _GNU_SOURCE
#include "vt.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int vt_active = 0;

#if defined(__linux__) && defined(__has_include)
#if __has_include(<linux/kd.h>)
#include <linux/kd.h>
#define HAVE_KD 1
#endif
#endif

#ifdef HAVE_KD

#define FONT_MAXN       512
#define FONT_MAXSTRIDE  128     // 32 rows * 4 bytes (32 px wide) - kernel's maximum per glyph

static unsigned char font_saved[FONT_MAXN * FONT_MAXSTRIDE];
static unsigned char font_new[256 * FONT_MAXSTRIDE];
static unsigned font_w, font_h, font_n;
static int font_swapped = 0;

static int font_op(unsigned op, unsigned w, unsigned h, unsigned n, unsigned char *data) {
    struct console_font_op f;
    memset(&f, 0, sizeof f);
    f.op = op; f.width = w; f.height = h; f.charcount = n; f.data = data;
    int r = ioctl(STDOUT_FILENO, KDFONTOP, &f);
    if (r == 0 && op == KD_FONT_OP_GET) { font_w = f.width; font_h = f.height; font_n = f.charcount; }
    return r;
}

static void rect(unsigned char *g, unsigned bpr, unsigned x0, unsigned y0, unsigned x1, unsigned y1) {
    for (unsigned y = y0; y < y1; y++)
        for (unsigned x = x0; x < x1; x++) g[y * bpr + x / 8] |= (unsigned char)(0x80 >> (x % 8));
}

// glyphs for any cell size: lines are 1/8 of the cell wide, 1/16 high (at least 1px)
static void make_glyphs(unsigned char *font, size_t stride, unsigned w, unsigned h) {
    unsigned bpr = (w + 7) / 8;
    unsigned tv = (w + 4) / 8 ? (w + 4) / 8 : 1, th = (h + 8) / 16 ? (h + 8) / 16 : 1;
    unsigned vx = (w - tv) / 2, hy = (h - th) / 2;
    memset(font + stride, 0, 9 * stride);
    unsigned char *g;
    g = font + 1 * stride; rect(g, bpr, 0, 0, w, h / 2);                    // ▀
    g = font + 2 * stride; rect(g, bpr, 0, h / 2, w, h);                    // ▄
    g = font + 3 * stride; rect(g, bpr, 0, 0, w, h);                        // █
    g = font + 4 * stride; rect(g, bpr, vx, hy, w, hy + th); rect(g, bpr, vx, hy, vx + tv, h);      // ┌
    g = font + 5 * stride; rect(g, bpr, 0, hy, vx + tv, hy + th); rect(g, bpr, vx, hy, vx + tv, h); // ┐
    g = font + 6 * stride; rect(g, bpr, vx, hy, w, hy + th); rect(g, bpr, vx, 0, vx + tv, hy + th); // └
    g = font + 7 * stride; rect(g, bpr, 0, hy, vx + tv, hy + th); rect(g, bpr, vx, 0, vx + tv, hy + th); // ┘
    g = font + 8 * stride; rect(g, bpr, 0, hy, w, hy + th);                 // ─
    g = font + 9 * stride; rect(g, bpr, vx, 0, vx + tv, h);                 // │
}

int vt_init(int enable) {
    const char *e = getenv("CFISH_VT");
    if (e && !strcmp(e, "0")) return 0;
    if (e && !strcmp(e, "force")) { vt_active = 1; return 1; }     // test hook
    if (!enable) return 0;

    const char *term = getenv("TERM");
    if (!term || strcmp(term, "linux")) return 0;
    int mode = 0, kb = 0;
    if (ioctl(STDOUT_FILENO, KDGETMODE, &mode) < 0 || mode != KD_TEXT) return 0;   // not a VT
    if (ioctl(STDIN_FILENO, KDGKBMODE, &kb) < 0 || kb != K_UNICODE) return 0;      // not UTF-8: U+F0xx is not ours

    if (font_op(KD_FONT_OP_GET, 32, 32, FONT_MAXN, font_saved) < 0) return 0;
    if (font_w == 0 || font_w > 32 || font_h == 0 || font_h > 32 || (font_n != 256 && font_n != 512)) return 0;

    size_t stride = (size_t)((font_w + 7) / 8) * 32;
    memcpy(font_new, font_saved, 256 * stride);     // 512 -> 256: otherwise no bright colors
    make_glyphs(font_new, stride, font_w, font_h);
    if (font_op(KD_FONT_OP_SET, font_w, font_h, 256, font_new) < 0) return 0;
    font_swapped = 1;
    vt_active = 1;
    return 1;
}

void vt_restore(void) {
    if (!vt_active) return;
    vt_active = 0;
    if (font_swapped) {
        font_swapped = 0;
        font_op(KD_FONT_OP_SET, font_w, font_h, font_n, font_saved);
    }
    const char *s = "\x1b]R\x1b[0m\x1b[H\x1b[2J";     // palette back, clear (no alt-screen on the console)
    ssize_t r = write(STDOUT_FILENO, s, strlen(s));
    (void)r;
}

uint32_t vt_map_cp(uint32_t cp) {
    if (cp < 0x80) return cp;
    if (font_swapped) {
        switch (cp) {
            case 0x2580: return VT_G_UP;   case 0x2584: return VT_G_LO;   case 0x2588: return VT_G_FULL;
            case 0x250C: return VT_G_TL;   case 0x2510: return VT_G_TR;
            case 0x2514: return VT_G_BL;   case 0x2518: return VT_G_BR;
            case 0x2500: return VT_G_H;    case 0x2502: return VT_G_V;
        }
        return cp;      // anything else (e.g. cyrillic in the shell): the kernel's own unicode map decides
    }
    return cp;
}

#else   // not Linux: console tweaks do not exist

int vt_init(int enable) {
    (void)enable;
    const char *e = getenv("CFISH_VT");
    if (e && !strcmp(e, "force")) { vt_active = 1; return 1; }
    return 0;
}
void vt_restore(void) {
    if (!vt_active) return;
    vt_active = 0;
    ssize_t r = write(STDOUT_FILENO, "\x1b]R\x1b[0m\x1b[H\x1b[2J", 14);
    (void)r;
}
uint32_t vt_map_cp(uint32_t cp) { return cp; }

#endif
