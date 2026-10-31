/* malloc.h - host stand-in: macOS has no <malloc.h> and no memalign. */
#pragma once
#include <stdlib.h>

static inline void *memalign(size_t align, size_t size) {
    void *p = NULL;
    return posix_memalign(&p, align < sizeof(void *) ? sizeof(void *) : align, size) ? NULL : p;
}
