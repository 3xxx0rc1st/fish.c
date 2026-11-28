// A frame bigger than the free space in /dev/shm: must switch the backend off, not die with SIGBUS.
#include <stdio.h>
#include <stdlib.h>
#include "gfx.h"
#include "kgp.h"
#include "term.h"
#include "theme.h"
int main(void) {
    setenv("AQUASH_CELL_SIZE", "40x80", 1);               // 40x10 cells -> 1600x800 px = 3.8 MB; /dev/shm below is 2 MB
    theme_set_now(0); kgp_init(KGP_ON); gfx_resize(40, 10);
    gfx_present();
    fprintf(stderr, "survived: kgp_active=%d\n", kgp_active);
    return kgp_active ? 2 : 0;
}
