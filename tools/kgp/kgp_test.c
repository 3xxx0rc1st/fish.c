// Deterministic tests of the kitty graphics backend, no terminal needed: the output goes to a file, the test
// plays the terminal's part (parse the command, read the shared-memory object, check every pixel).
//   cc -std=gnu11 -O2 -I. -o /tmp/kgp_test tools/kgp/kgp_test.c $(ls *.c | grep -v '^main.c$') -lm -lrt
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <signal.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gfx.h"
#include "kgp.h"
#include "term.h"
#include "theme.h"
#include "util.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static char *capture_path;
static int saved_out = -1;
static void cap_begin(void) { fflush(stdout); saved_out = dup(1); int fd = open(capture_path, O_WRONLY | O_CREAT | O_TRUNC, 0600); dup2(fd, 1); close(fd); }
static char *cap_end(size_t *n) {
    dup2(saved_out, 1); close(saved_out);
    FILE *f = fopen(capture_path, "rb"); fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)len + 1); size_t r = fread(b, 1, (size_t)len, f); b[r] = 0; fclose(f); *n = r; return b;
}

static int b64v(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1; }
static size_t b64dec(const char *s, size_t n, unsigned char *o) {
    size_t k = 0; unsigned acc = 0; int bits = 0;
    for (size_t i = 0; i < n; i++) { int v = b64v(s[i]); if (v < 0) break; acc = (acc << 6) | (unsigned)v; bits += 6; if (bits >= 8) { bits -= 8; o[k++] = (unsigned char)(acc >> bits); acc &= (1u << bits) - 1; } }
    return k;
}

static int count_shm(const char *prefix) {
    DIR *d = opendir("/dev/shm"); if (!d) return -1;
    int n = 0; struct dirent *e;
    while ((e = readdir(d))) if (!strncmp(e->d_name, prefix, strlen(prefix))) n++;
    closedir(d); return n;
}

typedef struct { int ok; char keys[256]; char name[200]; int w, h, cols, rows, z, id; } Cmd;

// first a=T command in buf (after position *from); advances *from
static Cmd parse_t(const char *buf, size_t n, size_t *from) {
    Cmd c; memset(&c, 0, sizeof c);
    const char *p = buf + *from, *end = buf + n;
    for (; p + 3 < end; p++) {
        if (p[0] != 0x1b || p[1] != '_' || p[2] != 'G') continue;
        const char *semi = memchr(p, ';', (size_t)(end - p)), *st = NULL;
        for (const char *q = p; q + 1 < end; q++) if (q[0] == 0x1b && q[1] == '\\') { st = q; break; }
        if (!semi || !st || semi > st) continue;
        size_t kl = (size_t)(semi - (p + 3)); if (kl >= sizeof c.keys) continue;
        memcpy(c.keys, p + 3, kl); c.keys[kl] = 0;
        if (!strstr(c.keys, "a=T")) { p = st; continue; }
        unsigned char raw[200]; size_t rl = b64dec(semi + 1, (size_t)(st - semi - 1), raw); raw[rl] = 0;
        snprintf(c.name, sizeof c.name, "%s", (char *)raw);
        sscanf(strstr(c.keys, "s=") , "s=%d", &c.w); sscanf(strstr(c.keys, ",v="), ",v=%d", &c.h);
        sscanf(strstr(c.keys, "i="), "i=%d", &c.id); sscanf(strstr(c.keys, ",c="), ",c=%d", &c.cols);
        sscanf(strstr(c.keys, ",r="), ",r=%d", &c.rows); sscanf(strstr(c.keys, "z="), "z=%d", &c.z);
        c.ok = 1; *from = (size_t)(st - buf) + 2; return c;
    }
    return c;
}

static unsigned char *read_file(const char *path, size_t want) {      // what a terminal does for t=f: read, never delete
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = (size_t)len == want ? malloc(want) : NULL;
    if (b && fread(b, 1, want, f) != want) { free(b); b = NULL; }
    fclose(f); return b;
}
static int count_files(const char *dir, const char *prefix) {
    DIR *d = opendir(dir); if (!d) return -1; int n = 0; struct dirent *e;
    while ((e = readdir(d))) if (!strncmp(e->d_name, prefix, strlen(prefix))) n++;
    closedir(d); return n;
}

static unsigned char *read_shm(const char *name, size_t want) {
    int fd = shm_open(name, O_RDONLY, 0); if (fd < 0) return NULL;
    struct stat st; if (fstat(fd, &st) != 0 || (size_t)st.st_size != want) { close(fd); return NULL; }
    unsigned char *b = malloc(want); size_t got = 0;
    while (got < want) { ssize_t r = read(fd, b + got, want - got); if (r <= 0) break; got += (size_t)r; }
    close(fd); shm_unlink(name);                         // what the terminal does after reading
    return got == want ? b : (free(b), NULL);
}

static unsigned char expect(int x, int y, int ch, int w, int h) { return (unsigned char)(ch == 0 ? x * 255 / (w - 1) : ch == 1 ? y * 255 / (h - 1) : (x * 7 + y * 13) & 255); }

int main(void) {
    #define UNUSED_FILES_TEST 1
    char tmp[] = "/tmp/kgp_test_XXXXXX"; int tfd = mkstemp(tmp); close(tfd); capture_path = tmp;
    const int cols = 40, rows = 10;
    theme_set_now(0);

    // ---- 1. canvas -> image, for several cell sizes: every pixel, command keys ----------------------------
    cap_begin(); kgp_init(KGP_ON); gfx_resize(cols, rows);
    size_t n; char *out = cap_end(&n); free(out);
    CHECK(kgp_active == 1, "KGP_ON must activate");
    int W = gfx_w, H = gfx_h; CHECK(W == cols && H == rows * 2, "canvas size %dx%d", W, H);
    int first_id = 0;
    struct { const char *env; int cw, ch, exact; const char *label; } cases[] = {
        { "8x16",    8, 16, 1, "8x16 cell" },
        { "10x21",  10, 21, 1, "odd 10x21 cell (half a cell is 10.5 px: rows alternate 11/10)" },
        { NULL,      8, 16, 1, "no information at all: the 8x16 guess" },
        { "200x400", 0,  0, 0, "window too big to scale ourselves: the bare canvas" },
    };
    for (unsigned ci = 0; ci < sizeof cases / sizeof *cases; ci++) {
        if (cases[ci].env) setenv("CFISH_CELL_SIZE", cases[ci].env, 1); else unsetenv("CFISH_CELL_SIZE");
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
            gfx_px[y * W + x] = col(expect(x, y, 0, W, H) + 0.3f, expect(x, y, 1, W, H) - 0.2f, expect(x, y, 2, W, H));   // exercises rounding
        cap_begin(); gfx_present(); out = cap_end(&n);
        size_t from = 0; Cmd c1 = parse_t(out, n, &from);
        int IW = cases[ci].exact ? cols * cases[ci].cw : W, IH = cases[ci].exact ? rows * cases[ci].ch : H;
        CHECK(c1.ok, "[%s] no a=T command in the frame", cases[ci].label);
        CHECK(c1.w == IW && c1.h == IH, "[%s] image %dx%d, want %dx%d", cases[ci].label, c1.w, c1.h, IW, IH);
        CHECK(c1.cols == cols && c1.rows == rows, "[%s] placement c=%d r=%d", cases[ci].label, c1.cols, c1.rows);
        CHECK(c1.z < 0, "z-index must be negative (text above the image), got %d", c1.z);
        CHECK(strstr(c1.keys, "t=s") && strstr(c1.keys, "f=24") && strstr(c1.keys, "C=1") && strstr(c1.keys, "q=2") && strstr(c1.keys, "p=1"),
              "keys: %s", c1.keys);
        CHECK(strstr(out, "\x1b[?2026h") && strstr(out, "\x1b[?2026l"), "synchronized update markers missing");
        unsigned char *img = c1.ok ? read_shm(c1.name, (size_t)IW * IH * 3) : NULL;
        CHECK(img != NULL, "[%s] shared-memory object '%s' missing or of the wrong size", cases[ci].label, c1.name);
        long bad = 0;
        if (img) for (int Y = 0; Y < IH; Y++) for (int X = 0; X < IW; X++) {
            // the canvas pixel this screen pixel belongs to: x = X / cw, y = 2Y / ch (identity for the bare canvas)
            int cx = cases[ci].exact ? X / cases[ci].cw : X, cy = cases[ci].exact ? (2 * Y) / cases[ci].ch : Y;
            for (int ch = 0; ch < 3; ch++) {
                float v = ch == 0 ? expect(cx, cy, 0, W, H) + 0.3f : ch == 1 ? expect(cx, cy, 1, W, H) - 0.2f : expect(cx, cy, 2, W, H);
                if (img[((size_t)Y * IW + X) * 3 + ch] != (unsigned char)(v + 0.5f)) bad++;
            }
        }
        CHECK(img && bad == 0, "[%s] %ld wrong bytes in the image (of %d)", cases[ci].label, bad, IW * IH * 3);
        free(img);
        CHECK(!strstr(out, "48;2;"), "no explicit cell backgrounds in image mode (the image must show through)");
        first_id = c1.id;                    // the id of the latest frame: the next one must differ
        free(out);
    }
    setenv("CFISH_CELL_SIZE", "8x16", 1);                 // the rest of the tests: a small, known image

    // ---- 2. two ids alternate, the previous one is deleted after the new one is shown ---------------
    cap_begin(); gfx_present(); out = cap_end(&n);
    size_t from = 0; Cmd c2 = parse_t(out, n, &from);
    CHECK(c2.ok && first_id && c2.id != first_id && (c2.id == 1 || c2.id == 2), "ids must alternate 1/2: %d then %d", first_id, c2.id);
    char del[64]; snprintf(del, sizeof del, "a=d,d=I,i=%d", first_id);
    const char *dp = strstr(out, del), *tp = strstr(out, "a=T");
    CHECK(dp && tp && dp > tp, "old image %d must be deleted AFTER the new one is placed", first_id);
    if (c2.ok) { unsigned char *t = read_shm(c2.name, (size_t)c2.w * c2.h * 3); free(t); }
    free(out);

    // ---- 3. text over the image: glyph + foreground only, blank cells are not painted ---------------
    // white text, alpha 1 at column 5, row 3 (0-based) -> CUP 4;6, fg quantized to 252, default background
    text_clear(); text_put(2, 5, 3, "Hi", col(255, 255, 255), 1.0f);
    cap_begin(); gfx_present(); out = cap_end(&n);
    CHECK(strstr(out, "\x1b[4;6H\x1b[38;2;252;252;252mHi") != NULL, "text cells: expected 'CUP 4;6, fg 252, Hi'");
    CHECK(!strstr(out, "48;2;"), "text cells must keep the default background");
    { Cmd t; size_t from = 0; t = parse_t(out, n, &from); CHECK(t.ok, "every frame sends its image"); if (t.ok) { unsigned char *x = read_shm(t.name, (size_t)t.w * t.h * 3); free(x); } }
    free(out);
    text_clear();
    cap_begin(); gfx_present(); out = cap_end(&n);       // text gone: exactly those two cells are blanked
    CHECK(strstr(out, "\x1b[4;6H  ") != NULL, "a cell that lost its text must be overwritten with blanks");
    CHECK(!strstr(out, "Hi"), "old text must not be redrawn");
    { Cmd t; size_t from = 0; t = parse_t(out, n, &from); if (t.ok) { unsigned char *x = read_shm(t.name, (size_t)t.w * t.h * 3); free(x); } }
    free(out);
    // nothing changed in the cells: the frame is the image commands and nothing else
    cap_begin(); gfx_present(); out = cap_end(&n);
    { size_t sync_end = (size_t)(strstr(out, "\x1b[?2026l") - out);
      const char *last_apc_end = out; for (const char *q = out; q + 1 < out + n; q++) if (q[0] == 0x1b && q[1] == '\\') last_apc_end = q + 2;
      CHECK(strstr(out, "\x1b[?2026l") && (size_t)(last_apc_end - out) == sync_end, "an unchanged text layer must add no cell output (%zu vs %zu)", (size_t)(last_apc_end - out), sync_end); }
    { Cmd t; size_t from = 0; t = parse_t(out, n, &from); if (t.ok) { unsigned char *x = read_shm(t.name, (size_t)t.w * t.h * 3); free(x); } }
    free(out);

    // ---- 4. objects nobody takes do not pile up: at most KEEP stay ------------------------------------
    char pre[40]; snprintf(pre, sizeof pre, "cfish-%d-", (int)getpid());
    cap_begin(); for (int i = 0; i < 60; i++) gfx_present(); out = cap_end(&n); free(out);
    int left = count_shm(pre);
    CHECK(left > 0 && left <= 16, "unconsumed shm objects after 60 frames: %d (want <= 16)", left);

    // ---- 4b. big images: the unclaimed ones are bounded by bytes, not by count -------------------------------
    setenv("CFISH_CELL_SIZE", "60x120", 1);                 // 40x10 cells -> 2400x1200 px = 8.6 MB per frame
    cap_begin(); for (int i = 0; i < 30; i++) gfx_present(); out = cap_end(&n); free(out);
    left = count_shm(pre);
    CHECK(left > 0 && left <= 6, "unconsumed 8.6 MB objects after 30 frames: %d (the 48 MB budget allows 5)", left);
    setenv("CFISH_CELL_SIZE", "8x16", 1);
    cap_begin(); kgp_shutdown(); out = cap_end(&n); free(out);
    cap_begin(); kgp_init(KGP_ON); gfx_resize(cols, rows); out = cap_end(&n); free(out);

    // ---- 5. shutdown removes everything and deletes the image -----------------------------------------
    cap_begin(); kgp_shutdown(); out = cap_end(&n);
    CHECK(strstr(out, "a=d,d=A"), "shutdown must delete the images");
    CHECK(count_shm(pre) == 0, "objects left after shutdown: %d", count_shm(pre));
    CHECK(kgp_active == 0, "shutdown must deactivate");
    free(out);

    // ---- 6. failure path: shared memory cannot be created -> backend off, half-blocks resume ---------
    cap_begin(); kgp_init(KGP_ON); gfx_resize(cols, rows); out = cap_end(&n); free(out);
    cap_begin(); gfx_present(); out = cap_end(&n); free(out);                    // frame 0 fine
    // occupy the name of the next frame so shm_open(O_EXCL) fails; the counter is reset by a fresh process,
    // so find it from the object that was just made
    DIR *d = opendir("/dev/shm"); struct dirent *e; unsigned maxseq = 0; int any = 0;
    while (d && (e = readdir(d))) if (!strncmp(e->d_name, pre, strlen(pre))) { unsigned s = (unsigned)atoi(e->d_name + strlen(pre)); if (!any || s > maxseq) maxseq = s; any = 1; }
    if (d) closedir(d);
    char blocker[64]; snprintf(blocker, sizeof blocker, "/cfish-%d-%u", (int)getpid(), maxseq + 1);
    int bfd = shm_open(blocker, O_RDWR | O_CREAT | O_EXCL, 0600); close(bfd);
    cap_begin(); gfx_present(); out = cap_end(&n);
    CHECK(kgp_active == 0, "a frame that cannot be made must switch the backend off");
    CHECK(strstr(out, "a=d,d=A"), "the image must be deleted when falling back");
    CHECK(strstr(out, "48;2;") != NULL, "after falling back the cells paint their own backgrounds again");
    free(out);
    shm_unlink(blocker);
    cap_begin(); kgp_shutdown(); out = cap_end(&n); free(out);

    // ---- 6b. file size limit hit while making the image (the same code path as a full /dev/shm) ----------
    cap_begin(); kgp_init(KGP_ON); gfx_resize(cols, rows); out = cap_end(&n); free(out);
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit old, lim; getrlimit(RLIMIT_FSIZE, &old); lim = old; lim.rlim_cur = 1000;     // only the soft limit: the hard one cannot be raised back
    setrlimit(RLIMIT_FSIZE, &lim);
    cap_begin(); gfx_present(); out = cap_end(&n);
    CHECK(setrlimit(RLIMIT_FSIZE, &old) == 0, "the file size limit must be restorable");
    CHECK(kgp_active == 0, "no room for the image must switch the backend off (not crash)");
    CHECK(strstr(out, "48;2;") != NULL, "and the frame is drawn with half-blocks instead");
    free(out);
    cap_begin(); kgp_shutdown(); out = cap_end(&n); free(out);
    CHECK(count_shm(pre) == 0, "a failed frame must not leave its object behind: %d", count_shm(pre));

    // ---- 8. the file ring: in-place frames, every slot must show the CURRENT canvas, even though it was last written
    //         FRING frames ago and the canvas changed only in a few places in between ---------------------------------
    char fdir[] = "/tmp/kgp_files_XXXXXX"; if (!mkdtemp(fdir)) { perror("mkdtemp"); return 2; }
    setenv("CFISH_KGP_FILEDIR", fdir, 1); setenv("CFISH_CELL_SIZE", "10x21", 1);
    cap_begin(); kgp_init(KGP_ON); gfx_resize(cols, rows); out = cap_end(&n); free(out);
    CHECK(kgp_uses_files(), "CFISH_KGP_FILEDIR must select the file transport");
    char first_path[200] = ""; int path_reuse = 0; long bad_total = 0; int frames_checked = 0;
    for (int fr = 0; fr < 10; fr++) {
        // a base picture plus a small square that moves: most of the canvas stays the same between frames
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            int in = abs(x - (3 + fr * 3)) < 2 && abs(y - (4 + fr)) < 2;
            gfx_px[y * W + x] = in ? col(250, 20 + fr * 20, 5) : col(expect(x, y, 0, W, H), expect(x, y, 1, W, H), expect(x, y, 2, W, H));
        }
        cap_begin(); gfx_present(); out = cap_end(&n);
        size_t from8 = 0; Cmd c = parse_t(out, n, &from8);
        CHECK(c.ok && strstr(c.keys, "t=f"), "frame %d: file transport command expected, keys: %s", fr, c.keys);
        if (!c.ok) { free(out); continue; }
        unsigned char *img = read_file(c.name, (size_t)cols * 10 * rows * 21 * 3);
        CHECK(img != NULL, "frame %d: file '%s' missing or of the wrong size", fr, c.name);
        if (img) {
            long bad = 0; int IW = cols * 10, IH = rows * 21;
            for (int Y = 0; Y < IH; Y++) for (int X = 0; X < IW; X++) {
                int cx = X / 10, cy = (2 * Y) / 21;
                int in = abs(cx - (3 + fr * 3)) < 2 && abs(cy - (4 + fr)) < 2;
                for (int ch = 0; ch < 3; ch++) {
                    unsigned char want = in ? (unsigned char)(ch == 0 ? 250 : ch == 1 ? 20 + fr * 20 : 5) : (unsigned char)(expect(cx, cy, ch, W, H) + 0.5f);
                    if (img[((size_t)Y * IW + X) * 3 + ch] != want) bad++;
                }
            }
            bad_total += bad; frames_checked++;
            CHECK(bad == 0, "frame %d (slot %d of 3): %ld wrong bytes in the file", fr, fr % 3, bad);
            free(img);
        }
        if (fr == 0) snprintf(first_path, sizeof first_path, "%s", c.name);
        if (fr == 3) path_reuse = !strcmp(first_path, c.name);
        CHECK(!strstr(out, "48;2;"), "no cell backgrounds");
        free(out);
    }
    CHECK(path_reuse, "frame 3 must reuse frame 0's file (in-place ring of 3): '%s'", first_path);
    CHECK(count_files(fdir, "cfish-") == 3, "the ring is exactly 3 files, found %d", count_files(fdir, "fish-"));
    CHECK(frames_checked == 10 && bad_total == 0, "all 10 frames exact");

    // quantization with -e: values come out as multiples of the step
    gfx_tol = 4;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) gfx_px[y * W + x] = col(x * 3 % 256, y * 5 % 256, 77);
    char *qout = NULL; size_t qn = 0;
    for (int k = 0; k < 3; k++) { cap_begin(); gfx_present(); free(qout); qout = cap_end(&qn); }     // all three slots see it
    { size_t f8 = 0; Cmd c = parse_t(qout, qn, &f8); unsigned char *img = c.ok ? read_file(c.name, (size_t)cols * 10 * rows * 21 * 3) : NULL; int ok = img != NULL;
      for (int Y = 0; img && Y < rows * 21; Y += 7) for (int X = 0; X < cols * 10; X += 3) {
          int cx = X / 10, cy = (2 * Y) / 21; const unsigned char *p = img + ((size_t)Y * cols * 10 + X) * 3;
          if (p[0] != (cx * 3 % 256) / 4 * 4 || p[1] != (cy * 5 % 256) / 4 * 4 || p[2] != 77 / 4 * 4) ok = 0;
      }
      CHECK(ok, "-e 4 must quantize the file transport's pixels to multiples of 4"); free(img); free(qout); }
    gfx_tol = 0;

    // resize: new size -> new files of the right size, the old ones are gone
    cap_begin(); gfx_resize(30, 8); out = cap_end(&n); free(out);
    for (int k = 0; k < 4; k++) { cap_begin(); gfx_present(); out = cap_end(&n);
        if (k == 3) { size_t f8 = 0; Cmd c = parse_t(out, n, &f8); unsigned char *img = c.ok ? read_file(c.name, (size_t)30 * 10 * 8 * 21 * 3) : NULL;
                      CHECK(img != NULL && c.w == 300 && c.h == 168, "after a resize the file has the new size (%dx%d)", c.w, c.h); free(img); }
        free(out); }
    CHECK(count_files(fdir, "cfish-") == 3, "a resize must not leave old files behind: %d", count_files(fdir, "fish-"));

    // shutdown unlinks the ring
    cap_begin(); kgp_shutdown(); out = cap_end(&n); free(out);
    CHECK(count_files(fdir, "cfish-") == 0, "shutdown must remove the ring files: %d left", count_files(fdir, "fish-"));

    // a directory that cannot be used: the frame goes through shared memory instead, the backend stays on
    setenv("CFISH_KGP_FILEDIR", "/nonexistent/dir/for/kgp", 1);
    cap_begin(); kgp_init(KGP_ON); gfx_resize(cols, rows); out = cap_end(&n); free(out);
    cap_begin(); gfx_present(); out = cap_end(&n);
    { size_t f8 = 0; Cmd c = parse_t(out, n, &f8);
      CHECK(c.ok && strstr(c.keys, "t=s") && kgp_active && !kgp_uses_files(), "an unusable directory must fall back to shm (keys: %s)", c.keys);
      if (c.ok) { unsigned char *x = read_shm(c.name, (size_t)c.w * c.h * 3); free(x); } }
    free(out);
    cap_begin(); kgp_shutdown(); out = cap_end(&n); free(out);
    unsetenv("CFISH_KGP_FILEDIR"); setenv("CFISH_CELL_SIZE", "8x16", 1);
    rmdir(fdir);

    // ---- 7. -g off does nothing at all --------------------------------------------------------------------
    cap_begin(); kgp_init(KGP_OFF); gfx_resize(cols, rows); gfx_present(); out = cap_end(&n);
    CHECK(kgp_active == 0 && !strstr(out, "\x1b_G"), "KGP_OFF must not emit any graphics command");
    free(out);

    unlink(tmp);
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
