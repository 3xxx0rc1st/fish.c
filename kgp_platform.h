
#ifndef KGP_PLATFORM_H
#define KGP_PLATFORM_H

#include <sys/types.h>
#include <fcntl.h>

int kgp_shm_available(void);

int kgp_shm_open(const char *name, int flags, mode_t mode);

int kgp_shm_unlink(const char *name);

#endif
