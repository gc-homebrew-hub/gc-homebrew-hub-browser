/* memstat.h - how much main memory is free (24 MB in all on the GameCube). */
#pragma once
#include <stddef.h>

/* Free blocks in the heap plus the room it can still grow into. Cheap. */
size_t mem_free_total(void);
/* The biggest single block malloc can hand out now, to the nearest 64 KB
 * below. Found by trying, so call it now and then, not every frame. */
size_t mem_largest_free(void);
