#include "memstat.h"
#include <stdlib.h>

#ifdef GEKKO
#include <gccore.h>
#include <malloc.h>
/* sbrk grows the heap by moving the arena's low end up (libogc's sbrk.c) */
static size_t arena_room(void) {
    char *lo = SYS_GetArenaLo(), *hi = SYS_GetArenaHi();
    return hi > lo ? (size_t)(hi - lo) : 0;
}
size_t mem_free_total(void) { return arena_room() + (size_t)mallinfo().fordblks; }
#else
/* on the host (tests, preview) memory is not the constraint */
size_t mem_free_total(void) { return (size_t)256 << 20; }
#endif

#define STEP ((size_t)64 << 10)

size_t mem_largest_free(void) {
    size_t lo = 0, hi = mem_free_total() / STEP;    /* in steps */
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        void *p = malloc(mid * STEP);
        if (p) { free(p); lo = mid; }
        else hi = mid - 1;
    }
    return lo * STEP;
}
