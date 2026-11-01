/* log.h - a plain-text log on the card (log.txt in the data folder), so what
 * went wrong can be read afterwards: toasts go away. Each line is appended and
 * the file closed again, so the log survives a crash or a reset. Safe to call
 * from both threads; does nothing until log_open. */
#pragma once

#define LOG_MAX_BYTES (128 * 1024)   /* past this, log.txt becomes log.old.txt */

void log_open(const char *path);
void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
