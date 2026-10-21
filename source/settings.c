#include "settings.h"
#include "fsutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DEFAULT_SOURCES_URL
#define DEFAULT_SOURCES_URL "https://raw.githubusercontent.com/Arial-Bold/cubebrowser-apps/main/sources.json"
#endif

Settings g_settings;

void settings_defaults(void) {
    memset(&g_settings, 0, sizeof g_settings);
    g_settings.auto_refresh = 1;
    g_settings.verify_tls = 1;
    g_settings.sounds = 1;
    g_settings.exit_action = 1;
    snprintf(g_settings.sources_url, sizeof g_settings.sources_url, "%s", DEFAULT_SOURCES_URL);
}

void settings_load(const char *path) {
    settings_defaults();
    char *text; size_t len;
    if (!fs_read_file(path, &text, &len, 16384)) return;
    char *line = text, *next;
    for (; line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#' || line[0] == ';') continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        size_t vl = strlen(v);
        while (vl && (v[vl - 1] == '\r' || v[vl - 1] == ' ')) ((char *)v)[--vl] = 0;
        if (!strcmp(k, "auto_refresh")) g_settings.auto_refresh = atoi(v) != 0;
        else if (!strcmp(k, "verify_tls")) g_settings.verify_tls = atoi(v) != 0;
        else if (!strcmp(k, "sounds")) g_settings.sounds = atoi(v) != 0;
        else if (!strcmp(k, "exit_action")) g_settings.exit_action = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "sources_url") && *v) snprintf(g_settings.sources_url, sizeof g_settings.sources_url, "%s", v);
    }
    free(text);
}

bool settings_save(const char *path) {
    char buf[600];
    int n = snprintf(buf, sizeof buf,
        "# GC Homebrew Browser settings\n"
        "auto_refresh=%d\nverify_tls=%d\nsounds=%d\nexit_action=%d\nsources_url=%s\n",
        g_settings.auto_refresh, g_settings.verify_tls, g_settings.sounds, g_settings.exit_action,
        g_settings.sources_url);
    return n > 0 && n < (int)sizeof buf && fs_write_file(path, buf, (size_t)n);
}
