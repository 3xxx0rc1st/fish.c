#!/usr/bin/env python3
"""Measure how much CPU aquash and the terminal burn over a time window, from /proc (no htop, nothing in the aquarium).

    python3 tools/kgp/cpu_sample.py                      # 20 s, targets: ghostty aquash
    python3 tools/kgp/cpu_sample.py --secs 30 --fps 30 ghostty 12345

A target is a process name (every match is summed) or a pid. CPU is process-wide (all threads, also the ones that
exited during the window) in percent of ONE core, so 100 = one core fully busy. With --fps the cost of one frame is
printed too (cpu time / frames), the number that can be compared between modes and machines.

Compare two runs under the same conditions: same terminal, `-f 30`, same theme/seed, panel closed:
    ./build/aquash -f 30 -t ocean -s 2 -g on      ->  run this, note the numbers
    ./build/aquash -f 30 -t ocean -s 2 -g off     ->  run this again
"""
import argparse, os, sys, time

TICK = os.sysconf('SC_CLK_TCK')

def pids_of(target):
    if target.isdigit(): return [int(target)] if os.path.exists('/proc/%s' % target) else []
    out = []
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try:
            with open('/proc/%s/comm' % p) as f: comm = f.read().strip()
        except OSError: continue
        if comm == target[:15]: out.append(int(p))
    return out

def ticks(pid):
    try:
        with open('/proc/%d/stat' % pid) as f: s = f.read()
    except OSError: return None
    rest = s[s.rindex(')') + 2:].split()          # fields after "pid (comm) "
    return int(rest[11]) + int(rest[12])           # utime + stime (process-wide)

def snapshot(targets):
    snap = {}
    for t in targets:
        for pid in pids_of(t):
            v = ticks(pid)
            if v is not None: snap[(t, pid)] = v
    return snap

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('targets', nargs='*', default=['ghostty', 'aquash'])
    ap.add_argument('--secs', type=float, default=20.0)
    ap.add_argument('--fps', type=float, default=0.0, help='the -f of the aquarium: print the cost of one frame too')
    a = ap.parse_args()
    a0 = snapshot(a.targets); t0 = time.time()
    if not a0: sys.exit('nothing found for: %s' % ' '.join(a.targets))
    time.sleep(a.secs)
    a1 = snapshot(a.targets); el = time.time() - t0
    print('window %.1f s' % el)
    total = {}
    for (t, pid), v0 in sorted(a0.items()):
        if (t, pid) not in a1: print('  %-10s pid %-7d  exited during the window, skipped' % (t, pid)); continue
        d = (a1[(t, pid)] - v0) / TICK
        total[t] = total.get(t, 0.0) + d
        print('  %-10s pid %-7d  %6.2f %% of a core' % (t, pid, 100 * d / el))
    print('sum by target:')
    for t, d in total.items():
        line = '  %-10s %6.2f %% of a core' % (t, 100 * d / el)
        if a.fps: line += '   = %.2f ms of CPU per frame at %g fps' % (1000 * d / el / a.fps, a.fps)
        print(line)

if __name__ == '__main__':
    main()
