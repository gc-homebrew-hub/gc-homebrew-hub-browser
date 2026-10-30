/* Host stand-in for libogc2's time base: ticks are microseconds. */
#pragma once
#include <stdint.h>
#include <time.h>

static inline uint64_t gettime(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000u + (uint64_t) ts.tv_nsec / 1000u;
}
static inline uint32_t gettick(void) { return (uint32_t) gettime(); }
static inline uint64_t ticks_to_millisecs(uint64_t t) { return t / 1000u; }
static inline uint64_t diff_ticks(uint64_t a, uint64_t b) { return b - a; }
static inline uint64_t secs_to_ticks(uint64_t s) { return s * 1000000u; }
