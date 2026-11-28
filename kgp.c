#define _POSIX_C_SOURCE 200809L
#include "kgp.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>      // shm_open / shm_unlink / mmap
#include <sys/stat.h>
#include <unistd.h>

#include "gfx.h"       // gfx_tol
#include "kgp_platform.h"
#include "term.h"

int kgp_active = 0;

#define RING 16                 // shm names remembered; whatever the terminal has not taken in time is unlinked
#define MAX_PENDING (48u << 20) // ... and the objects nobody took never add up to more than about this many bytes
#define MAX_EXACT_PIXELS (3840u * 2160u)    // bigger windows are not scaled by us (see kgp_frame)
#define DETECT_TIMEOUT 0.5      // seconds to wait for the answer (DA1 answers everywhere, so this is only for dead ends)

static char     names[RING][40];
static unsigned name_seq[RING];
static unsigned seq = 0;        // frames made so far
static int      q_cw = 0, q_ch = 0;     // cell size in pixels, from the detection query (0 = the terminal did not say)
static int      img_w = 0, img_h = 0;   // pixel size of the frame kgp_frame() prepared
static unsigned char *fbuf = NULL;      // the frame is built here, then written to the shared object
static size_t   fbuf_cap = 0;
static int      cur_id = 2;     // image id used by the last emitted frame (1 and 2 alternate)
static int      prepared = 0;
static int      pending_clear = 0;

// ---- the file transport -----------------------------------------------------------------
// Creating a shared-memory object per frame costs the kernel a fresh 5.7 MB of pages every time (the terminal
// deletes the object after reading it). A plain file (t=f) is NOT deleted by the terminal, so a small ring of them
// can stay mapped and be rewritten in place, and only the canvas pixels that changed since THAT slot was last
// drawn have to be touched: the diff that makes the ANSI renderer cheap, without needing any protocol feature
// beyond reading a file. The directory must be one the terminal agrees to read from (it may refuse /dev, /proc,
// /sys), the detection asks.
#define FRING 3                 // slots: the terminal has FRING-1 frame times to read a file before it is rewritten
#define MAX_CELL_PX 256         // wider cells are not scaled by us
typedef struct {
    char path[160];
    unsigned char *map; size_t size;      // the file, mapped: what the terminal reads
    unsigned char *state; size_t state_size;   // (quantized) canvas this slot shows now, w*h*3
    int fresh;                  // nothing drawn yet: write every block
} FSlot;
static FSlot    fring[FRING];
static int      use_file = 0;
static char     file_dir[128];
static unsigned file_gen = 0;
static int      frame_file_slot = -1;   // >= 0: the prepared frame lives in this ring slot, -1: in shared memory

static const char DEL_ALL[] = "\x1b_Ga=d,d=A,q=2\x1b\\";     // delete every image of this terminal screen

// ---- helpers -----------------------------------------------------------------------------
static size_t b64(const unsigned char *in, size_t n, char *out) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)in[i] << 16;
        if (i + 1 < n) v |= (unsigned)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = t[(v >> 18) & 63];
        out[o++] = t[(v >> 12) & 63];
        out[o++] = i + 1 < n ? t[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

static inline unsigned char to8(float v) {
    int i = (int)(v + 0.5f);
    return (unsigned char)(i < 0 ? 0 : i > 255 ? 255 : i);
}

static void put_all(const char *s, size_t n) {
    while (n) {
        ssize_t w = write(STDOUT_FILENO, s, n);
        if (w < 0) return;
        s += w; n -= (size_t)w;
    }
}

// ---- detection ---------------------------------------------------------------------------
// A complete DA1 answer (ESC [ ? ... c) in b?
static int da1_seen(const char *b, size_t n) {
    for (size_t i = 0; i + 3 < n; i++) {
        if (b[i] != 0x1b || b[i + 1] != '[' || b[i + 2] != '?') continue;
        size_t j = i + 3;
        while (j < n && ((b[j] >= '0' && b[j] <= '9') || b[j] == ';')) j++;
        if (j < n && b[j] == 'c') return 1;
    }
    return 0;
}

// Ask: (1) can you do the kitty graphics protocol at all (tiny direct image, query only), (2) can you
// read a shared-memory object of mine (= same machine). Then DA1: every terminal answers it, and it
// answers after the graphics replies, so its arrival ends the wait without a guess at a timeout.
// Returns 1 only if (2) worked.
static int detect(void) {
    const char *term = getenv("TERM");
    if (getenv("TMUX") || getenv("STY")) return 0;          // multiplexers eat the commands
    if (term && (!strcmp(term, "linux") || !strncmp(term, "screen", 6) || !strncmp(term, "tmux", 4))) return 0;

    char probe[40], enc[96];
    snprintf(probe, sizeof probe, "/aquash-probe-%d", (int)getpid());
    int fd = kgp_shm_open(probe, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return 0;
    static const unsigned char px[3] = {0, 0, 0};
    if (ftruncate(fd, 3) != 0 || write(fd, px, 3) != 3) { close(fd); kgp_shm_unlink(probe); return 0; }
    close(fd);
    b64((const unsigned char *)probe, strlen(probe), enc);

    // directories to offer the terminal for the file transport, best first
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    const char *dirs[3] = { xdg && *xdg ? xdg : NULL, "/dev/shm", "/tmp" };
    char fpath[3][160], fenc[3][256];
    int offered[3] = {0, 0, 0};
    char q[2048];
    int n = snprintf(q, sizeof q,
        "\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\"
        "\x1b_Gi=32,s=1,v=1,a=q,t=s,f=24;%s\x1b\\", enc);
    if (n < 0) n = 0;
	if ((size_t)n >= sizeof q) n = (int)(sizeof q - 1);
    for (int i = 0; i < 3; i++) {
        if (!dirs[i] || strlen(dirs[i]) > 100) continue;
        snprintf(fpath[i], sizeof fpath[i], "%s/aquash-probe-%d-%d", dirs[i], (int)getpid(), i);
        int pf = open(fpath[i], O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (pf < 0) continue;
        ssize_t wr = write(pf, px, 3); close(pf);
        if (wr != 3) { unlink(fpath[i]); continue; }
        b64((const unsigned char *)fpath[i], strlen(fpath[i]), fenc[i]);
        offered[i] = 1;
        if ((size_t)n >= sizeof q) break;
        size_t rem = sizeof q - (size_t)n;
        int m = snprintf(q + n, rem, "\x1b_Gi=%d,s=1,v=1,a=q,t=f,f=24;%s\x1b\\", 40 + i, fenc[i]);
        if (m < 0) break;
        if ((size_t)m >= rem) { n = (int)(sizeof q - 1); break; }
        n += m;
    }
    if ((size_t)n < sizeof q) {
        size_t rem = sizeof q - (size_t)n;
        int m = snprintf(q + n, rem,
            "\x1b[16t"                  // cell size in pixels, answered as ESC [ 6 ; height ; width t
            "\x1b[c");
        if (m > 0) {
            if ((size_t)m >= rem) n = (int)(sizeof q - 1);
            else n += m;
        }
    }
    put_all(q, (size_t)n);

    char buf[1024];
    size_t have = 0;
    const double end = now_s() + DETECT_TIMEOUT;
    while (have < sizeof buf - 1) {
        double left = end - now_s();
        if (left <= 0) break;
        struct pollfd pf = {STDIN_FILENO, POLLIN, 0};
        int r = poll(&pf, 1, (int)ceil(left * 1000.0));
        if (r < 0) continue;            // EINTR (SIGWINCH): try again until the deadline
        if (r == 0) break;
        ssize_t k = read(STDIN_FILENO, buf + have, sizeof buf - 1 - have);
        if (k <= 0) break;
        have += (size_t)k;
        buf[have] = 0;
        if (da1_seen(buf, have)) break;
    }
    buf[have] = 0;
    for (size_t i = 0; i + 4 < have; i++) {
        int h, w;
        if (buf[i] == 0x1b && buf[i + 1] == '[' && buf[i + 2] == '6' && buf[i + 3] == ';' && sscanf(buf + i + 4, "%d;%d", &h, &w) == 2) { q_ch = h; q_cw = w; }
    }
    kgp_shm_unlink(probe);                  // normally the terminal already took it
    int shm_ok = strstr(buf, "i=32;OK") != NULL;
    use_file = 0;
    for (int i = 0; i < 3; i++) {
        if (!offered[i]) continue;
        unlink(fpath[i]);               // the terminal never deletes a plain file
        char want[32]; snprintf(want, sizeof want, "i=%d;OK", 40 + i);
        if (!use_file && strstr(buf, want)) { use_file = 1; snprintf(file_dir, sizeof file_dir, "%s", dirs[i]); }
    }
    return shm_ok || use_file;          // any local transport will do; the file ring is preferred when both work
}

void kgp_init(KgpMode mode) {
    kgp_active = 0;
    if (mode == KGP_OFF) return;
    use_file = 0;
    if (mode == KGP_ON) {
        kgp_active = 1;
        const char *d = getenv("AQUASH_KGP_FILEDIR");     // forced: no question asked, so the file transport is opt-in here
        if (d && *d && strlen(d) < sizeof file_dir) { use_file = 1; snprintf(file_dir, sizeof file_dir, "%s", d); }
        return;
    }
    kgp_active = detect();
}

int kgp_uses_files(void) { return kgp_active && use_file; }

// ---- frames ------------------------------------------------------------------------------
static void forget(unsigned slot) {
    if (names[slot][0]) kgp_shm_unlink(names[slot]);    // ENOENT = the terminal took it, which is the normal case
    names[slot][0] = 0;
}

static void fail(void) {
    kgp_active = 0;                     // fall back to half-blocks for good
    pending_clear = 1;
    prepared = 0;
}

// Pixel size of one cell. A terminal draws a smaller image stretched with a smooth filter (Ghostty does), which
// blurs pixel art, so we scale the canvas ourselves to exactly the window's pixels. Where from, best first:
// AQUASH_CELL_SIZE=WxH (for a terminal that says nothing or the wrong thing), the window size ioctl, the
// terminal's answer to CSI 16 t, a guess.
static void cell_size(int *cw, int *ch) {
    const char *e = getenv("AQUASH_CELL_SIZE");
    int a, b;
    if (e && sscanf(e, "%dx%d", &a, &b) == 2 && a > 0 && b > 1) { *cw = a; *ch = b; return; }
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row &&
        ws.ws_xpixel >= ws.ws_col && ws.ws_ypixel >= 2 * ws.ws_row) {
        *cw = ws.ws_xpixel / ws.ws_col;
        *ch = ws.ws_ypixel / ws.ws_row;
        return;
    }
    if (q_cw > 0 && q_ch > 1) { *cw = q_cw; *ch = q_ch; return; }
    *cw = 8; *ch = 16;                  // a guess: if it is wrong the terminal rescales a little
}

static unsigned keep_frames(size_t size) {
    size_t k = MAX_PENDING / (size ? size : 1);
    return k < 2 ? 2 : k > RING ? RING : (unsigned)k;
}

static void slot_free(FSlot *s) {
    if (s->map) munmap(s->map, s->size);
    if (s->path[0]) unlink(s->path);
    free(s->state);
    memset(s, 0, sizeof *s);
}

// One frame through the file ring: bring the slot's file up to the canvas, touching only what differs from what the
// slot showed last. 1 = prepared, 0 = the file transport does not work here (the caller falls back to shm).
static int frame_file(const Col *px, int w, int h, int cw, int chh, size_t W, size_t H) {
    const size_t size = W * H * 3;
    const unsigned slot = seq % FRING;
    FSlot *s = &fring[slot];
    if (s->map && (s->size != size || s->state_size != (size_t)w * (size_t)h * 3)) slot_free(s);      // resized
    if (!s->map) {
        snprintf(s->path, sizeof s->path, "%s/aquash-%d-%u-%u.frame", file_dir, (int)getpid(), slot, ++file_gen);
        int fd = open(s->path, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0) { s->path[0] = 0; return 0; }
        if (ftruncate(fd, (off_t)size) != 0) { close(fd); slot_free(s); return 0; }
#ifdef __linux__
        int fr = posix_fallocate(fd, 0, (off_t)size);   // a full tmpfs would be a SIGBUS at the first touch
        if (fr != 0 && fr != EOPNOTSUPP && fr != EINVAL) { close(fd); slot_free(s); return 0; }
#endif
        void *m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (m == MAP_FAILED) { slot_free(s); return 0; }
        s->map = m; s->size = size;
        s->state_size = (size_t)w * (size_t)h * 3;
        s->state = calloc(1, s->state_size);          // zeros, like the fresh file: black pixels need no write
        if (!s->state) { slot_free(s); return 0; }
        s->fresh = 1;
    }
    // -e N: the canvas is quantized to steps of N (like the ANSI renderer does), fewer pixels differ between frames
    const int q = gfx_tol > 1 ? gfx_tol : 1;
    const size_t stride = W * 3, bw = (size_t)cw * 3;
    unsigned char pat[MAX_CELL_PX * 3];
    unsigned char *st = s->state;
    for (int y = 0; y < h; y++) {
        const size_t Y0 = ((size_t)y * (size_t)chh + 1) / 2, Y1 = ((size_t)(y + 1) * (size_t)chh + 1) / 2;
        const Col *src = px + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++, st += 3) {
            unsigned char r = to8(src[x].r), g = to8(src[x].g), b = to8(src[x].b);
            if (q > 1) { r = (unsigned char)(r / q * q); g = (unsigned char)(g / q * q); b = (unsigned char)(b / q * q); }
            if (!s->fresh && r == st[0] && g == st[1] && b == st[2]) continue;
            st[0] = r; st[1] = g; st[2] = b;
            for (int k = 0; k < cw; k++) { pat[3 * k] = r; pat[3 * k + 1] = g; pat[3 * k + 2] = b; }
            unsigned char *o = s->map + Y0 * stride + (size_t)x * bw;
            for (size_t Y = Y0; Y < Y1; Y++, o += stride) memcpy(o, pat, bw);
        }
    }
    s->fresh = 0;
    img_w = (int)W; img_h = (int)H;
    frame_file_slot = (int)slot;
    seq++;
    prepared = 1;
    return 1;
}

int kgp_frame(const Col *px, int w, int h, int cols, int rows) {
    prepared = 0;
    int cw, chh;
    cell_size(&cw, &chh);
    // canvas pixel (x, y) covers the block of cw x (chh / 2) screen pixels (chh may be odd: rows alternate)
    size_t W = (size_t)cols * (size_t)cw, H = (size_t)rows * (size_t)chh;
    int exact = w == cols && h == 2 * rows && W * H <= MAX_EXACT_PIXELS;
    if (!exact) { W = (size_t)w; H = (size_t)h; }   // too big or odd: send the canvas as is, the terminal stretches it
    size_t size = W * H * 3;

    if (use_file && exact && cw <= MAX_CELL_PX) {
        if (frame_file(px, w, h, cw, chh, W, H)) return 1;
        use_file = 0;                   // this directory does not work after all: shared memory from here on
        for (int i = 0; i < FRING; i++) slot_free(&fring[i]);
    }
    frame_file_slot = -1;

    unsigned keep = keep_frames(size);
    for (unsigned i = 0; i < RING; i++)
        if (names[i][0] && seq - name_seq[i] >= keep) forget(i);
    unsigned slot = seq % RING;
    forget(slot);
    snprintf(names[slot], sizeof names[slot], "/aquash-%d-%u", (int)getpid(), seq);
    name_seq[slot] = seq;
    // Build the frame in ordinary memory and hand it over with one write(): cheaper than filling a mapping of
    // the shared object (every page of that costs a fault), and a full /dev/shm is a plain ENOSPC from write()
    // (through mmap it would be a SIGBUS in the middle of the fill loop).
    if (size > fbuf_cap) {
        unsigned char *nb = realloc(fbuf, size);
        if (!nb) { names[slot][0] = 0; fail(); return 0; }
        fbuf = nb; fbuf_cap = size;
    }
    unsigned char *m = fbuf;
    if (exact) {
        const size_t stride = W * 3;
        for (int y = 0; y < h; y++) {
            size_t Y0 = ((size_t)y * (size_t)chh + 1) / 2, Y1 = ((size_t)(y + 1) * (size_t)chh + 1) / 2;
            unsigned char *row = m + Y0 * stride, *o = row;
            const Col *src = px + (size_t)y * (size_t)w;
            for (int x = 0; x < w; x++) {
                unsigned char r = to8(src[x].r), g = to8(src[x].g), b = to8(src[x].b);
                for (int k = 0; k < cw; k++) { *o++ = r; *o++ = g; *o++ = b; }
            }
            for (size_t Y = Y0 + 1; Y < Y1; Y++) memcpy(m + Y * stride, row, stride);
        }
    } else {
        for (size_t i = 0, n = (size_t)w * (size_t)h; i < n; i++) {
            m[3 * i]     = to8(px[i].r);
            m[3 * i + 1] = to8(px[i].g);
            m[3 * i + 2] = to8(px[i].b);
        }
    }
    int fd = kgp_shm_open(names[slot], O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) { names[slot][0] = 0; fail(); return 0; }
    for (size_t done = 0; done < size;) {
        ssize_t k = write(fd, m + done, size - done);
        if (k <= 0) { close(fd); forget(slot); fail(); return 0; }
        done += (size_t)k;
    }
    close(fd);
    img_w = (int)W; img_h = (int)H;
    seq++;
    prepared = 1;
    return 1;
}

void kgp_emit(int cols, int rows) {
    if (!prepared) return;
    prepared = 0;
    const char *name = frame_file_slot >= 0 ? fring[frame_file_slot].path : names[(seq - 1) % RING];
    const char medium = frame_file_slot >= 0 ? 'f' : 's';
    char enc[256], cmd[512];
    b64((const unsigned char *)name, strlen(name), enc);
    // the image is exactly the window's pixels (or the bare canvas when that was too big); c/r pin it to the
    // cell area either way.
    //   a=T transmit + show   t=s shared memory / t=f file   f=24 RGB   i/p ids   z=-1 below text
    //   C=1 cursor stays      q=2 no replies (we do not read the terminal during the frame)
    int next = 3 - cur_id;
    int n = snprintf(cmd, sizeof cmd,
        "\x1b[1;1H\x1b_Ga=T,t=%c,f=24,s=%d,v=%d,i=%d,p=1,c=%d,r=%d,z=-1,C=1,q=2;%s\x1b\\",
        medium, img_w, img_h, next, cols, rows, enc);
    ob_str(cmd, (size_t)n);
    // Show the new image first and drop the old one after it: inside the synchronized update the two
    // never show alone, and this does not depend on how a terminal treats re-sending an id.
    n = snprintf(cmd, sizeof cmd, "\x1b_Ga=d,d=I,i=%d,q=2\x1b\\", cur_id);
    ob_str(cmd, (size_t)n);
    cur_id = next;
}

void kgp_emit_clear(void) {
    if (!pending_clear && !kgp_active) return;
    pending_clear = 0;
    ob_str(DEL_ALL, sizeof DEL_ALL - 1);
}

void kgp_shutdown(void) {
    int had = kgp_active || pending_clear || seq;
    if (had) put_all(DEL_ALL, sizeof DEL_ALL - 1);
    for (unsigned i = 0; i < RING; i++) forget(i);
    free(fbuf); fbuf = NULL; fbuf_cap = 0;
    for (int i = 0; i < FRING; i++) slot_free(&fring[i]);
    kgp_active = 0;
    pending_clear = 0;
}
