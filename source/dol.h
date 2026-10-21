/* dol.h - starts another program (.dol) from the card. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Checks that the file is a .dol this launcher can start. */
bool dol_check(const char *path, char *err, size_t errlen);
/* Shuts the system down and jumps to the program. Only returns on failure. */
bool dol_launch(const char *path, char *err, size_t errlen);
