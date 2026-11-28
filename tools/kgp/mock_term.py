#!/usr/bin/env python3
"""A mock terminal for the kitty graphics backend: runs the real fish in a pty and plays the terminal's part
(answers the detection queries, reads and deletes the shared-memory objects, replays the text), then checks what fish did.

    make && python3 tools/kgp/mock_term.py [--fish build/fish] [--png composite.png] [scenario ...]
"""
import argparse, glob, os, pty, re, select, signal, struct, sys, termios, fcntl, time
import fcntl as _f

COLS, ROWS = 100, 30
APC = re.compile(rb'\x1b_G([^;\x1b]*)(?:;([^\x1b]*))?\x1b\\', re.S)
TOK = re.compile(rb'\x1b_G[^\x1b]*\x1b\\|\x1b\[0?c|\x1b\[16t', re.S)
CSI = re.compile(r'\x1b\[([0-9;?]*)([A-Za-z])|([^\x1b]+)')

def keys_of(s):
    d = {}
    for kv in s.decode().split(','):
        if '=' in kv: k, v = kv.split('=', 1); d[k] = v
    return d

class Mock:
    """mode: full | nokgp | remote | silent"""
    def __init__(self, mode, accept_files=False):
        self.mode = mode
        self.accept_files = accept_files    # does this terminal read t=f files (outside /dev, /proc, /sys)?
        self.paths = []                     # file paths seen in a=T commands
        self.buf = b''
        self.plain = bytearray()            # everything that is not a graphics command
        self.cmds = []                      # parsed a=T commands
        self.errors = []                    # problems found in commands
        self.queries = []                   # a=q commands
        self.da1 = 0
        self.cols, self.rows = COLS, ROWS   # the window the mock currently has
        self.sizes = {(COLS, ROWS)}         # every size it ever had: frames made before a resize are still in the pty
        self.q16 = 0                        # CSI 16 t asked
        self.cell_reply = None              # (w, h) the mock answers to CSI 16 t, None = does not answer
        self.images = {}                    # id -> (w, h, bytes) currently placed
        self.last_image = None
        self.deletes = []
        self.t_first_T = None
        self.frames_t = 0
        self.snap = {}                      # moment (s) -> length of the non-graphics stream then

    def shm_read(self, name, want):
        path = '/dev/shm' + name
        try:
            data = open(path, 'rb').read()
        except OSError:
            return None
        try: os.unlink(path)               # the terminal deletes it after reading
        except OSError: pass
        return data if len(data) == want else b'BADSIZE:%d' % len(data)

    def feed(self, chunk, now, reply):
        self.buf += chunk
        pos = 0
        for m in TOK.finditer(self.buf):
            self.plain += self.buf[pos:m.start()]; pos = m.end()
            tok = m.group(0)
            if tok == b'\x1b[16t':                       # cell size in pixels
                self.q16 += 1
                if self.cell_reply and self.mode != 'silent': reply(b'\x1b[6;%d;%dt' % (self.cell_reply[1], self.cell_reply[0]))
                continue
            if tok.startswith(b'\x1b['):
                self.da1 += 1
                if self.mode != 'silent': reply(b'\x1b[?62;4c')
                continue
            mm = APC.match(tok); keys = keys_of(mm.group(1)); payload = mm.group(2) or b''
            self.handle(keys, payload, now, reply)
        rest = self.buf[pos:]
        # keep an unfinished graphics command or a cut-off ESC [ at the end for the next chunk
        cut = rest.rfind(b'\x1b_G')
        if cut < 0:
            m = re.search(rb'\x1b(\[[0-9]*)?$', rest)
            cut = m.start() if m else len(rest)
        self.plain += rest[:cut]
        self.buf = rest[cut:]

    def handle(self, k, payload, now, reply):
        import base64
        a = k.get('a'); i = k.get('i')
        if a == 'q':
            self.queries.append(k)
            if self.mode in ('nokgp', 'silent'): return
            if k.get('t') == 'd':
                reply(b'\x1b_Gi=%s;OK\x1b\\' % i.encode())
            elif k.get('t') == 'f':
                path = base64.b64decode(payload).decode()
                ok = self.accept_files and not path.startswith(('/dev/', '/proc/', '/sys/')) and os.path.isfile(path)
                reply((b'\x1b_Gi=%s;OK\x1b\\' if ok else b'\x1b_Gi=%s;EPERM:refused\x1b\\') % i.encode())
            elif k.get('t') == 's':
                if self.mode == 'remote':
                    reply(b'\x1b_Gi=%s;ENOENT:could not open shared memory\x1b\\' % i.encode()); return
                name = base64.b64decode(payload).decode()
                data = self.shm_read(name, int(k['s']) * int(k['v']) * 3)
                reply((b'\x1b_Gi=%s;OK\x1b\\' if data is not None and not data.startswith(b'BADSIZE') else b'\x1b_Gi=%s;EBADF\x1b\\') % i.encode())
            return
        if a == 'T':
            self.cmds.append(k)
            if self.t_first_T is None: self.t_first_T = now
            for key, want in (('f', '24'), ('C', '1'), ('q', '2'), ('p', '1')):
                if k.get(key) != want: self.errors.append('key %s=%r, want %r' % (key, k.get(key), want))
            if k.get('t') not in ('s', 'f'): self.errors.append('key t=%r, want s or f' % k.get('t'))
            if int(k.get('z', '0')) >= 0: self.errors.append('z must be negative, got %s' % k.get('z'))
            w, h = int(k['s']), int(k['v'])
            if (int(k['c']), int(k['r'])) not in self.sizes: self.errors.append('c/r %s/%s is no size this window ever had: %s' % (k['c'], k['r'], sorted(self.sizes)))
            if self.mode in ('nokgp', 'silent', 'remote'): return        # a terminal without the protocol ignores it
            if k.get('t') == 'f':           # a plain file: read it, never delete it
                path = base64.b64decode(payload).decode(); self.paths.append(path)
                try: data = open(path, 'rb').read()
                except OSError: data = None
                if data is not None and len(data) != w * h * 3: data = b'BADSIZE:%d' % len(data)
                if data is None: self.errors.append('file %s not readable' % path)
                elif data.startswith(b'BADSIZE'): self.errors.append('file size wrong: ' + data.decode())
                else: self.images[k['i']] = (w, h, data); self.last_image = (w, h, data)
                return
            data = self.shm_read(base64.b64decode(payload).decode(), w * h * 3)
            if data is None: self.errors.append('shm object %s not readable' % base64.b64decode(payload).decode())
            elif data.startswith(b'BADSIZE'): self.errors.append('shm size wrong: ' + data.decode())
            else:
                self.images[k['i']] = (w, h, data); self.last_image = (w, h, data)
            return
        if a == 'd':
            self.deletes.append(k)
            if k.get('d') in ('I', 'i'): self.images.pop(k.get('i'), None)
            if k.get('d') in ('A', 'a'): self.images.clear()

def run(fish, mock, args, env_extra=None, secs=5.0, script=(), snaps=(), cell=(8, 16)):
    env = dict(os.environ, TERM='xterm-256color', SHELL='/bin/sh', LC_ALL='C.UTF-8')
    for k in ('TMUX', 'STY'): env.pop(k, None)
    env.update(env_extra or {})
    pid, fd = pty.fork()
    if pid == 0:
        os.execve(fish, [fish] + args, env)
    def setsize(c, r):
        mock.cols, mock.rows = c, r; mock.sizes.add((c, r))
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', r, c, c * cell[0] if cell else 0, r * cell[1] if cell else 0))
        os.kill(pid, signal.SIGWINCH)
    setsize(COLS, ROWS)
    t0 = time.time(); sent = 0; exited = None; total = 0
    shm_peak = 0
    def reply(b):
        try: os.write(fd, b)
        except OSError: pass
    while time.time() - t0 < secs:
        r, _, _ = select.select([fd], [], [], 0.005)
        if r:
            try: d = os.read(fd, 1 << 20)
            except OSError: break
            if not d: break
            total += len(d); mock.feed(d, time.time() - t0, reply)
        el = time.time() - t0
        for st in snaps:
            if st not in mock.snap and el >= st: mock.snap[st] = len(mock.plain)
        while sent < len(script) and el >= script[sent][0]:
            act = script[sent][1]
            if isinstance(act, tuple):          # ('resize', cols, rows)
                setsize(act[1], act[2])
            else:
                os.write(fd, act)
            sent += 1
        shm_peak = max(shm_peak, len(glob.glob('/dev/shm/fish-%d-*' % pid)))
    os.kill(pid, signal.SIGTERM)
    t1 = time.time()
    while time.time() - t1 < 6:
        r, _, _ = select.select([fd], [], [], 0.02)
        if r:
            try:
                d = os.read(fd, 1 << 20)
                if d: mock.feed(d, time.time() - t0, reply)
            except OSError: pass
        p, st = os.waitpid(pid, os.WNOHANG)
        if p: exited = os.WEXITSTATUS(st) if os.WIFEXITED(st) else 'signal %d' % os.WTERMSIG(st); break
    else:
        os.kill(pid, 9); os.waitpid(pid, 0); exited = 'HUNG'
    left = glob.glob('/dev/shm/fish-%d-*' % pid) + glob.glob('/dev/shm/fish-probe-%d*' % pid)
    for d in {'/tmp', '/dev/shm', env.get('XDG_RUNTIME_DIR') or ''} - {''}:
        left += glob.glob('%s/fish-%d-*' % (d, pid)) + glob.glob('%s/fish-probe-%d-*' % (d, pid))
    left = sorted(set(left))
    for p in left:
        try: os.unlink(p)
        except OSError: pass
    san = b'AddressSanitizer' in bytes(mock.plain) or b'runtime error:' in bytes(mock.plain)
    return dict(pid=pid, exited=exited, left=left, shm_peak=shm_peak, bytes=total, san=san)

def screen_text(plain):
    """replay the non-graphics stream into a character grid (enough for 'did the text arrive')"""
    grid = [[' '] * COLS for _ in range(ROWS)]; fg = {}; x = y = 0; cur = None
    for m in CSI.finditer(plain.decode('utf-8', 'replace')):
        if m.group(3) is not None:
            for ch in m.group(3):
                if 0 <= y < ROWS and 0 <= x < COLS: grid[y][x] = ch; fg[(x, y)] = cur
                x = min(x + 1, COLS)
            continue
        a, c = m.group(1), m.group(2)
        if c == 'H':
            r, cc = (a.split(';') + ['1'])[:2]; y = int(r or 1) - 1; x = int(cc or 1) - 1
        elif c == 'C': x += int(a or 1)
        elif c == 'J': grid = [[' '] * COLS for _ in range(ROWS)]; fg = {}
        elif c == 'm':
            v = [int(t) for t in a.split(';') if t]
            if 38 in v: j = v.index(38); cur = tuple(v[j + 2:j + 5])
            elif v == [0]: cur = None
    return grid, fg

def composite(mock, grid, fg, path):
    from PIL import Image, ImageDraw, ImageFont
    w, h, data = mock.last_image
    im = Image.frombytes('RGB', (w, h), data).resize((COLS * 8, ROWS * 16), Image.NEAREST)
    d = ImageDraw.Draw(im)
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 13)
    for yy in range(ROWS):
        for xx in range(COLS):
            ch = grid[yy][xx]
            if ch != ' ': d.text((xx * 8, yy * 16 + 12), ch, font=font, fill=fg.get((xx, yy)) or (220, 220, 220), anchor='ls')
    im.save(path)

def nearest_ok(img, cols, rows, cw, ch):
    """is the image exactly the canvas scaled by pixel replication (no interpolation, nothing blurred)?"""
    import numpy as np
    w, h, data = img
    if (w, h) != (cols * cw, rows * ch): return False
    a = np.frombuffer(data, np.uint8).reshape(h, w, 3)
    canvas = a[np.ix_([(y * ch + 1) // 2 for y in range(2 * rows)], [x * cw for x in range(cols)])]
    rebuilt = canvas[np.ix_((2 * np.arange(h)) // ch, np.arange(w) // cw)]
    return bool(np.array_equal(rebuilt, a))

# ---- scenarios ----------------------------------------------------------------------------------------
SHELL_KEYS = [(1.0, b' '), (2.0, b's'), (2.6, b'echo hello-from-shell\r'), (3.6, b'\x1b[24~')]      # dismiss the intro, open the panel, type, close it

def check(name, ok, detail=''):
    print('  %s %s%s' % ('ok  ' if ok else 'FAIL', name, (' - ' + detail) if detail and not ok else ''))
    return ok

def scenario(fish, name, png=None):
    good = True
    def ck(n, ok, d=''):
        nonlocal good; good = check(n, ok, d) and good
    print('[%s]' % name)
    if name == 'full':
        m = Mock('full'); r = run(fish, m, [], script=SHELL_KEYS, secs=5.0, snaps=(3.4,), cell=(10, 21))
        ck('queried: direct + shared-memory + file directories, then DA1', 3 <= len(m.queries) <= 5 and m.da1 == 1, 'queries=%d da1=%d' % (len(m.queries), m.da1))
        ck('image frames arrive (>= 60 in 5 s)', len(m.cmds) >= 60, str(len(m.cmds)))
        ck('first image within 1 s of start', m.t_first_T is not None and m.t_first_T < 1.0, str(m.t_first_T))
        ck('every command well-formed', not m.errors, '; '.join(sorted(set(m.errors))[:3]))
        ck('every image read from shm OK', not any('shm' in e for e in m.errors))
        w, h, _ = m.last_image or (0, 0, b'')
        ck('image is exactly the window: %dx%d px (10x21 cells)' % (COLS * 10, ROWS * 21), (w, h) == (COLS * 10, ROWS * 21), '%dx%d' % (w, h))
        ck('...and a pure pixel replication of the canvas (nothing for the terminal to blur)', m.last_image and nearest_ok(m.last_image, COLS, ROWS, 10, 21))
        ck('no cell backgrounds in the stream (image shows through)', b'48;2;' not in bytes(m.plain))
        ck('no half-blocks in the stream', '\u2580'.encode() not in bytes(m.plain))
        grid, fg = screen_text(bytes(m.plain))
        rows = [''.join(rw) for rw in grid]
        ck('HUD text is there', any('q quit' in rw and 'space feed' in rw for rw in rows))
        # the panel is open at 3.4 s (typed text arrives as text over the image) and closed after 3.6 s (its cells are blanked)
        g_open, _ = screen_text(bytes(m.plain)[:m.snap[3.4]])
        ck('shell text is on screen while the panel is open', any('hello-from-shell' in ''.join(rw) for rw in g_open))
        ck('and gone after the panel is closed (cells blanked)', not any('hello-from-shell' in rw or '$' in rw for rw in rows),
           ' | '.join(rw.strip() for rw in rows if '$' in rw or 'hello' in rw)[:80])
        ck('exit is clean', r['exited'] == 0, str(r['exited']))
        ck('no shared memory left behind', not r['left'], str(r['left']))
        ck('images deleted on exit', any(k.get('d') == 'A' for k in m.deletes), str(m.deletes[-2:]))
        ck('old images are deleted as new ones appear (<= 2 alive)', len(m.images) <= 2, str(len(m.images)))
        if png and m.last_image: composite(m, grid, fg, png); print('  composite ->', png)
    elif name in ('csi16', 'nopix', 'envcell'):
        m = Mock('full'); env = {}
        if name == 'csi16': m.cell_reply = (10, 21); want = (10, 21)
        elif name == 'envcell': env = {'FISH_CELL_SIZE': '12x25'}; want = (12, 25)
        else: want = (8, 16)
        r = run(fish, m, [], env_extra=env, secs=3.5, cell=(10, 21) if name == 'envcell' else None)   # envcell: the ioctl says 10x21, the env 12x25: the env must win
        w, h, _ = m.last_image or (0, 0, b'')
        ck('%s: image is %dx%d px (%dx%d cells)' % (name, COLS * want[0], ROWS * want[1], want[0], want[1]), (w, h) == (COLS * want[0], ROWS * want[1]), '%dx%d' % (w, h))
        ck('and a clean replication', m.last_image and nearest_ok(m.last_image, COLS, ROWS, *want))
        if name == 'csi16': ck('the cell size was asked with CSI 16 t', m.q16 == 1, str(m.q16))
        ck('every command well-formed', not m.errors, '; '.join(sorted(set(m.errors))[:3]))
        ck('exit is clean', r['exited'] == 0, str(r['exited']))
        ck('no shared memory left behind', not r['left'], str(r['left']))
    elif name in ('file', 'file-nodev', 'file-denied'):
        import tempfile, shutil
        rundir = tempfile.mkdtemp(prefix='mockrun-')
        m = Mock('full', accept_files=(name != 'file-denied'))
        env = {'XDG_RUNTIME_DIR': rundir if name != 'file-nodev' else ''}
        r = run(fish, m, [], env_extra=env, secs=5.0, script=[(2.0, ('resize', 120, 40))], cell=(10, 21))
        left_in_run = glob.glob(rundir + '/*')
        shutil.rmtree(rundir, ignore_errors=True)
        if name == 'file-denied':
            ck('files refused: frames go through shared memory', not m.paths and len(m.cmds) >= 60, 'paths=%d cmds=%d' % (len(m.paths), len(m.cmds)))
        else:
            want_dir = rundir if name == 'file' else '/tmp'
            ck('file transport chosen (%s)' % want_dir, len(m.paths) >= 60 and all(p.startswith(want_dir + '/') for p in m.paths), '%d paths, first %s' % (len(m.paths), m.paths[:1]))
            ck('/dev/shm was offered and refused (mock policy), not used', not any(p.startswith('/dev/') for p in m.paths))
            ck('the ring is 3 files per window size (3 before the resize + 3 after)', len(set(m.paths)) == 6, str(len(set(m.paths))))
            ck('the same file is reused in place', len(m.paths) > 20 and m.paths[0] == m.paths[3] == m.paths[6])
        ck('every command well-formed', not m.errors, '; '.join(sorted(set(m.errors))[:3]))
        ck('the last image is a clean replication of a 120x40 canvas', m.last_image and nearest_ok(m.last_image, 120, 40, 10, 21))
        ck('exit is clean', r['exited'] == 0, str(r['exited']))
        ck('no file or shm object left behind (ring, probes)', not r['left'] and not left_in_run, str(r['left'] + left_in_run))
    elif name == 'resize':
        m = Mock('full'); r = run(fish, m, [], secs=5.0, script=[(2.0, ('resize', 120, 40)), (3.5, ('resize', 80, 24))], cell=(10, 21))
        sizes = []
        for k in m.cmds:
            sz = (int(k['s']), int(k['v']))
            if not sizes or sizes[-1] != sz: sizes.append(sz)
        ck('image follows the window: 100x30 -> 120x40 -> 80x24 cells, 10x21 px each', sizes == [(1000, 630), (1200, 840), (800, 504)], str(sizes))
        ck('the last one is still a clean replication', m.last_image and nearest_ok(m.last_image, 80, 24, 10, 21))
        ck('every command well-formed (c/r always match the image)', not m.errors, '; '.join(sorted(set(m.errors))[:3]))
        ck('a resize clears the old images (<= 2 alive at the end)', len(m.images) <= 2, str(len(m.images)))
        ck('exit is clean', r['exited'] == 0, str(r['exited']))
        ck('no shared memory left behind', not r['left'], str(r['left']))
    elif name in ('nokgp', 'remote', 'silent'):
        m = Mock(name); t0 = time.time(); r = run(fish, m, [], secs=3.5)
        ck('falls back: no image commands', not m.cmds, str(len(m.cmds)))
        ck('half-block frames are drawn instead', b'48;2;' in bytes(m.plain) and '\u2580'.encode() in bytes(m.plain))
        ck('no shared memory left (probe object removed)', not r['left'], str(r['left']))
        ck('exit is clean', r['exited'] == 0, str(r['exited']))
        if name == 'silent': ck('still starts: bytes keep coming after the detection timeout', r['bytes'] > 100000, str(r['bytes']))
    elif name == 'tmux':
        m = Mock('full'); r = run(fish, m, [], env_extra={'TMUX': '/tmp/tmux-0/default,1,0'}, secs=3.0)
        ck('asks nothing', not m.queries and m.da1 == 0 and b'\x1b_G' not in bytes(m.plain), 'queries=%d da1=%d' % (len(m.queries), m.da1))
        ck('half-block frames are drawn', b'48;2;' in bytes(m.plain))
        ck('exit is clean', r['exited'] == 0, str(r['exited']))
    elif name == 'off':
        m = Mock('full'); r = run(fish, m, ['-g', 'off'], secs=3.0)
        ck('asks nothing and sends no images', not m.queries and m.da1 == 0 and not m.cmds)
        ck('half-block frames are drawn', b'48;2;' in bytes(m.plain))
        ck('exit is clean', r['exited'] == 0, str(r['exited']))
    elif name == 'on':
        m = Mock('nokgp'); r = run(fish, m, ['-g', 'on'], secs=4.0)
        ck('forced: images are sent without asking', not m.queries and len(m.cmds) >= 50, 'queries=%d cmds=%d' % (len(m.queries), len(m.cmds)))
        ck('a terminal that takes none of them: shm stays bounded (<= 17 objects)', r['shm_peak'] <= 17, str(r['shm_peak']))
        ck('and is cleaned up at exit', not r['left'], str(r['left']))
        ck('exit is clean', r['exited'] == 0, str(r['exited']))
    else:
        raise SystemExit('unknown scenario ' + name)
    ck('no sanitizer reports', not r['san'])
    return good

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--fish', default='build/fish'); ap.add_argument('--png')
    ap.add_argument('scenarios', nargs='*', default=['full', 'file', 'file-nodev', 'file-denied', 'resize', 'csi16', 'nopix', 'envcell', 'nokgp', 'remote', 'silent', 'tmux', 'off', 'on'])
    a = ap.parse_args(); fish = os.path.abspath(a.fish)
    results = {s: scenario(fish, s, a.png if s == 'full' else None) for s in a.scenarios}
    bad = [s for s, ok in results.items() if not ok]
    print('\n%s: %d/%d scenarios' % ('PASS' if not bad else 'FAIL (%s)' % ', '.join(bad), len(results) - len(bad), len(results)))
    sys.exit(1 if bad else 0)
