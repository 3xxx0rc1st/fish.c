
#define _POSIX_C_SOURCE 200809L

#include "kgp_platform.h"

#include <errno.h>

#if defined(__ANDROID__)

// Android Bionic does not expose POSIX shm_open/shm_unlink.
// KGP shared-memory transport is unavailable on this platform.

int kgp_shm_available(void)
{
    return 0;
}

int kgp_shm_open(const char *name, int flags, mode_t mode)
{
    (void)name;
    (void)flags;
    (void)mode;

    errno = ENOTSUP;
    return -1;
}

int kgp_shm_unlink(const char *name)
{
    (void)name;

    errno = ENOTSUP;
    return -1;
}

#else

#include <sys/mman.h>

int kgp_shm_available(void)
{
    return 1;
}

int kgp_shm_open(const char *name, int flags, mode_t mode)
{
    return shm_open(name, flags, mode);
}

int kgp_shm_unlink(const char *name)
{
    return shm_unlink(name);
}

#endif
