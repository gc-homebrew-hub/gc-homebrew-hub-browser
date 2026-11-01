/* catalog.h - sources, the apps they list, and what is installed on the card.
 *
 * Catalog format 1 (JSON), shared with the gc-homebrew-hub-catalog repository:
 *   sources.json: { "format": 1, "sources": [ { "name", "url" } ] }
 *   index.json:   { "format": 1, "name", "updated", "apps": [ { "slug", "name",
 *                   "author", "version", "category", "short", "long", "homepage",
 *                   "zip" | "dol", "zip_size", "sha256", "root", "binary",
 *                   "folder", "keep": [], "copy_to_root": [], "updated" } ] }
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define MAX_SOURCES   16
#define MAX_APPS      256
#define MAX_INSTALLED 128
#define INDEX_MAX_BYTES (256 * 1024)
#define APPS_DIR "sd:/apps"

typedef struct {
    char name[48];
    char url[256];
    bool builtin;        /* came from the default source list */
    bool enabled;
    int  napps;          /* after the last refresh; -1 = failed */
    char status[64];     /* "12 apps" / "Failed: ..." */
} Source;

/* A file from the download that replaces one at the root of the card (a boot
 * file such as ipl.dol), when the user agrees and one is there. `from` is a
 * path in the archive, or "inner.zip!path" for a file in a zip inside it; for
 * a bare .dol it is the binary. */
typedef struct { char from[112], to[48], archive[48]; } RootFile;

typedef struct {
    char slug[32], name[48], author[40], version[24], category[16];
    char summary[96], description[1024], homepage[160];
    char url[256];
    bool bare;                       /* url is a .dol, not a zip */
    unsigned long long size;         /* download size, 0 = unknown */
    char sha256[65];                 /* "" = not given */
    char root[64], binary[48], folder[48];
    char keep[4][64]; int nkeep;
    char to_root[4][48]; int nto_root;
    RootFile replace[2]; int nreplace;   /* replace_at_root */
    long long updated;
    int  source;
    /* links to the card, filled by catalog_recompute() */
    int  installed;                  /* index into the installed list or -1 */
    bool update;                     /* installed with an older version */
} App;

typedef struct {
    char slug[32], name[48], version[24], folder[48], binary[48];
    bool managed;                    /* installed by this program (has a stamp or a record) */
    int  app;                        /* catalog index or -1 */
} Installed;

/* The running program's own folder under sd:/apps, which also holds its data */
#define SELF_SLUG   "gc-homebrew-hub-browser"
#define SELF_FOLDER "gc-homebrew-hub-browser"

void catalog_init(const char *datadir);
/* Lists the running program as installed at this version, so the catalog
 * can offer its updates; kept across catalog_init. */
void catalog_set_self(const char *version);
bool catalog_is_self(const char *folder);
const char *catalog_datadir(void);
/* Whether the SD card is mounted. Without one (or in Dolphin, which has no
 * GameCube SD adapter) the catalogs can be browsed but nothing installed. */
void catalog_set_card(bool present);
bool catalog_have_card(void);

/* Sources (UI thread) */
int     catalog_source_count(void);
Source *catalog_source(int i);
bool    catalog_source_add(const char *url, char *err, size_t errlen);
bool    catalog_source_remove(int i);        /* user sources only */
void    catalog_source_toggle(int i);
bool    catalog_sources_save(void);
/* Merges the default list (sources.json text). */
bool    catalog_merge_default_sources(const char *json, size_t len, char *err, size_t errlen);
void    catalog_cache_path(const char *url, char *out, size_t cap);

/* Apps */
int  catalog_app_count(void);
App *catalog_app(int i);
int  catalog_find_slug(const char *slug);
/* Parses an index into out[*n..]; the source's catalog name goes to srcname. */
bool catalog_parse_index(const char *json, size_t len, int source, App *out, int *n, int cap,
                         char *srcname, size_t namecap, char *err, size_t errlen);
/* Takes ownership of a malloc'd array (UI thread). */
void catalog_replace_apps(App *arr, int n);
bool catalog_version_newer(const char *candidate, const char *have);

/* Installed */
int        catalog_installed_count(void);
Installed *catalog_installed(int i);
int        catalog_installed_find(const char *slug);
void       catalog_installed_set(const char *slug, const char *name, const char *version,
                                 const char *folder, const char *binary);
void       catalog_installed_remove(const char *slug);
bool       catalog_installed_save(void);
/* Looks through sd:/apps for apps not installed by this program. */
void       catalog_scan_card(void);
void       catalog_recompute(void);
int        catalog_update_count(void);
