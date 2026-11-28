#!/bin/sh
# A frame that does not fit into /dev/shm must switch the backend off, not kill fish with SIGBUS (tmpfs hands out its
# pages lazily, so without posix_fallocate the crash comes while the pixels are written). Needs root or a user
# namespace: it mounts a 2 MB tmpfs over /dev/shm inside a private mount namespace, the real one is not touched.
#   sh tools/kgp/enospc_test.sh      (from the fish.c directory)
set -e
cc -std=gnu11 -O2 -I. -o /tmp/fish_enospc tools/kgp/enospc.c $(ls *.c | grep -v '^main.c$') -lm -lrt
unshare -m sh -c 'mount -t tmpfs -o size=2m tmpfs /dev/shm && /tmp/fish_enospc >/dev/null'
echo "ok: survived a full /dev/shm"
