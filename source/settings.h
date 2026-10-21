/* settings.h - the few user settings, kept in settings.ini next to boot.dol. */
#pragma once
#include <stdbool.h>

typedef struct {
    int auto_refresh;      /* load the catalogs when the program starts */
    int verify_tls;        /* check server certificates against cacert.pem */
    int sounds;            /* reserved for later */
    int exit_action;       /* 0 reboot, 1 return to loader (Swiss) */
    char sources_url[256]; /* where the default source list comes from */
} Settings;

extern Settings g_settings;

void settings_defaults(void);
void settings_load(const char *path);
bool settings_save(const char *path);
