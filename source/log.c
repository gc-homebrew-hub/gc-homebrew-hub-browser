#include "log.h"
#include "fsutil.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef GEKKO
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
static mutex_t lock;
static bool lock_made;
#define LOCK()   do { if (lock_made) LWP_MutexLock(lock); } while (0)
#define UNLOCK() do { if (lock_made) LWP_MutexUnlock(lock); } while (0)
static unsigned long long now_ms(void) { return ticks_to_millisecs(gettime()); }
#else
#include <time.h>
#define LOCK()
#define UNLOCK()
static unsigned long long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000 + (unsigned long long)t.tv_nsec / 1000000;
}
#endif

static char log_path[160];
static long long log_size;
static unsigned long long start_ms;

/* log.txt -> log.old.txt, replacing the older one */
static void rotate(void) {
    char old[176];
    size_t n = strlen(log_path);
    if (n > 4 && !strcmp(log_path + n - 4, ".txt")) snprintf(old, sizeof old, "%.*s.old.txt", (int)(n - 4), log_path);
    else snprintf(old, sizeof old, "%s.old", log_path);
    remove(old);
    rename(log_path, old);
    log_size = 0;
}

void log_open(const char *path) {
#ifdef GEKKO
    if (!lock_made) lock_made = LWP_MutexInit(&lock, false) == 0;
#endif
    LOCK();
    snprintf(log_path, sizeof log_path, "%s", path);
    start_ms = now_ms();
    log_size = fs_file_size(log_path);
    if (log_size < 0) log_size = 0;
    if (log_size >= LOG_MAX_BYTES) rotate();
    UNLOCK();
}

void log_printf(const char *fmt, ...) {
    if (!log_path[0]) return;
    char line[400];
    unsigned long long ms = now_ms() - start_ms;
    int n = snprintf(line, sizeof line, "[%5llu.%03llu] ", ms / 1000, ms % 1000);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap);
    va_end(ap);
    for (char *c = line; *c; c++)
        if (*c == '\n' || *c == '\r') *c = ' ';    /* one line per call */
    strcat(line, "\n");
    LOCK();
    if (log_size >= LOG_MAX_BYTES) rotate();
    FILE *f = fopen(log_path, "a");
    if (f) {
        fputs(line, f);
        fclose(f);
        log_size += (long long)strlen(line);
    }
    UNLOCK();
}
