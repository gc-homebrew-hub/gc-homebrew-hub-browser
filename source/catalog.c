#include "catalog.h"
#include "fsutil.h"
#include "json.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char datadir[128];
static Source sources[MAX_SOURCES];
static int nsources;
static App *apps;
static int napps;
static Installed inst[MAX_INSTALLED];
static int ninst;

static void scopy(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    while (n + 1 < cap && src[n]) { dst[n] = src[n]; n++; }
    dst[n] = 0;
}

const char *catalog_datadir(void) { return datadir; }

static bool have_card;
void catalog_set_card(bool present) { have_card = present; }
bool catalog_have_card(void) { return have_card; }

/* ---- sources ---- */

int catalog_source_count(void) { return nsources; }
Source *catalog_source(int i) { return (i >= 0 && i < nsources) ? &sources[i] : NULL; }

static int source_find(const char *url) {
    for (int i = 0; i < nsources; i++) if (!strcmp(sources[i].url, url)) return i;
    return -1;
}

static bool url_ok(const char *url) {
    size_t n = strlen(url);
    if (n < 10 || n >= sizeof sources[0].url) return false;
    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) return false;
    for (const char *p = url; *p; p++) if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127) return false;
    return true;
}

static void name_from_url(const char *url, char *out, size_t cap) {
    const char *h = strstr(url, "://");
    h = h ? h + 3 : url;
    size_t n = strcspn(h, "/");
    if (n >= cap) n = cap - 1;
    memcpy(out, h, n); out[n] = 0;
}

bool catalog_source_add(const char *url, char *err, size_t errlen) {
    if (!url_ok(url)) { snprintf(err, errlen, "The address must start with http:// or https://"); return false; }
    if (source_find(url) >= 0) { snprintf(err, errlen, "That source is already in the list"); return false; }
    if (nsources >= MAX_SOURCES) { snprintf(err, errlen, "No room for more sources (%d)", MAX_SOURCES); return false; }
    Source *s = &sources[nsources++];
    memset(s, 0, sizeof *s);
    scopy(s->url, sizeof s->url, url);
    name_from_url(url, s->name, sizeof s->name);
    s->enabled = true;
    s->napps = 0;
    scopy(s->status, sizeof s->status, "Not loaded yet");
    catalog_sources_save();
    return true;
}

bool catalog_source_remove(int i) {
    if (i < 0 || i >= nsources || sources[i].builtin) return false;
    memmove(&sources[i], &sources[i + 1], (nsources - i - 1) * sizeof(Source));
    nsources--;
    for (int k = 0; k < napps; k++) {
        if (apps[k].source == i) apps[k].source = -1;
        else if (apps[k].source > i) apps[k].source--;
    }
    catalog_sources_save();
    return true;
}

void catalog_source_toggle(int i) {
    if (i < 0 || i >= nsources) return;
    sources[i].enabled = !sources[i].enabled;
    catalog_sources_save();
}

bool catalog_sources_save(void) {
    char path[160], buf[MAX_SOURCES * 320];
    size_t o = 0;
    for (int i = 0; i < nsources; i++) {
        int n = snprintf(buf + o, sizeof buf - o, "%s%s\t%s\t%s\n", sources[i].enabled ? "" : "-",
                         sources[i].url, sources[i].name, sources[i].builtin ? "b" : "u");
        if (n < 0 || o + n >= sizeof buf) return false;
        o += n;
    }
    fs_join(path, sizeof path, datadir, "sources.txt");
    return fs_write_file(path, buf, o);
}

static void sources_load(void) {
    char path[160], *text; size_t len;
    fs_join(path, sizeof path, datadir, "sources.txt");
    if (!fs_read_file(path, &text, &len, 65536)) return;
    char *line = text, *next;
    for (; line && nsources < MAX_SOURCES; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        bool enabled = true;
        if (*line == '-') { enabled = false; line++; }
        char *name = strchr(line, '\t');
        if (!name) continue;
        *name++ = 0;
        char *kind = strchr(name, '\t');
        if (kind) { *kind++ = 0; }
        if (!url_ok(line) || source_find(line) >= 0) continue;
        Source *s = &sources[nsources++];
        memset(s, 0, sizeof *s);
        scopy(s->url, sizeof s->url, line);
        scopy(s->name, sizeof s->name, *name ? name : line);
        s->enabled = enabled;
        s->builtin = kind && kind[0] == 'b';
        scopy(s->status, sizeof s->status, "Not loaded yet");
    }
    free(text);
}

bool catalog_merge_default_sources(const char *json, size_t len, char *err, size_t errlen) {
    JTok t[64];
    int n = json_parse(json, len, t, 64);
    if (n < 1 || t[0].type != JT_OBJECT) { snprintf(err, errlen, "The source list is not valid JSON"); return false; }
    if (json_int(json, t, json_find(json, t, 0, "format"), 0) != 1) { snprintf(err, errlen, "Unknown source list format"); return false; }
    int arr = json_find(json, t, 0, "sources");
    if (arr < 0 || t[arr].type != JT_ARRAY) { snprintf(err, errlen, "The source list has no sources"); return false; }
    bool changed = false;
    for (int k = json_first(t, arr), c = 0; k >= 0 && c < t[arr].size; k = json_next(t, k), c++) {
        char url[256], name[48];
        if (!json_string(json, t, json_find(json, t, k, "url"), url, sizeof url) || !url_ok(url)) continue;
        json_string(json, t, json_find(json, t, k, "name"), name, sizeof name);
        int i = source_find(url);
        if (i < 0) {
            if (nsources >= MAX_SOURCES) continue;
            i = nsources++;
            memset(&sources[i], 0, sizeof sources[i]);
            scopy(sources[i].url, sizeof sources[i].url, url);
            sources[i].enabled = true;
            scopy(sources[i].status, sizeof sources[i].status, "Not loaded yet");
            changed = true;
        }
        if (*name && strcmp(sources[i].name, name)) { scopy(sources[i].name, sizeof sources[i].name, name); changed = true; }
        if (!sources[i].builtin) { sources[i].builtin = true; changed = true; }
    }
    if (changed) catalog_sources_save();
    return true;
}

void catalog_cache_path(const char *url, char *out, size_t cap) {
    unsigned h = 2166136261u;
    for (const char *p = url; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
    snprintf(out, cap, "%s/cache/%08x.json", datadir, h);
}

/* ---- apps ---- */

int catalog_app_count(void) { return napps; }
App *catalog_app(int i) { return (i >= 0 && i < napps) ? &apps[i] : NULL; }

int catalog_find_slug(const char *slug) {
    for (int i = 0; i < napps; i++) if (!strcmp(apps[i].slug, slug)) return i;
    return -1;
}

static bool slug_ok(const char *s) {
    size_t n = strlen(s);
    if (n < 1 || n > 31) return false;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-')) return false;
    return true;
}

static bool name_ok(const char *s) {
    size_t n = strlen(s);
    if (n < 1 || n > 40) return false;
    for (; *s; s++) if (!(isalnum((unsigned char)*s) || strchr("._- ", *s))) return false;
    return true;
}

static void str_field(const char *json, const JTok *t, int obj, const char *key, char *out, size_t cap) {
    json_string(json, t, json_find(json, t, obj, key), out, cap);
}

bool catalog_parse_index(const char *json, size_t len, int source, App *out, int *n, int cap,
                         char *srcname, size_t namecap, char *err, size_t errlen) {
    int maxtok = 64 + MAX_APPS * 48;
    JTok *t = malloc(sizeof(JTok) * maxtok);
    if (!t) { snprintf(err, errlen, "Out of memory"); return false; }
    int nt = json_parse(json, len, t, maxtok);
    if (nt < 1 || t[0].type != JT_OBJECT) { free(t); snprintf(err, errlen, "Not a valid catalog (bad JSON)"); return false; }
    if (json_int(json, t, json_find(json, t, 0, "format"), 0) != 1) { free(t); snprintf(err, errlen, "Unknown catalog format"); return false; }
    char nm[48] = "";
    str_field(json, t, 0, "name", nm, sizeof nm);
    if (*nm && srcname) scopy(srcname, namecap, nm);
    int arr = json_find(json, t, 0, "apps");
    if (arr < 0 || t[arr].type != JT_ARRAY) { free(t); snprintf(err, errlen, "The catalog lists no apps"); return false; }
    int start = *n;
    for (int k = json_first(t, arr), c = 0; k >= 0 && c < t[arr].size && *n < cap; k = json_next(t, k), c++) {
        if (t[k].type != JT_OBJECT) continue;
        App *a = &out[*n];
        memset(a, 0, sizeof *a);
        a->source = source;
        a->installed = -1;
        str_field(json, t, k, "slug", a->slug, sizeof a->slug);
        str_field(json, t, k, "name", a->name, sizeof a->name);
        str_field(json, t, k, "author", a->author, sizeof a->author);
        str_field(json, t, k, "version", a->version, sizeof a->version);
        str_field(json, t, k, "category", a->category, sizeof a->category);
        str_field(json, t, k, "short", a->summary, sizeof a->summary);
        str_field(json, t, k, "long", a->description, sizeof a->description);
        str_field(json, t, k, "homepage", a->homepage, sizeof a->homepage);
        str_field(json, t, k, "zip", a->url, sizeof a->url);
        if (!*a->url) str_field(json, t, k, "archive", a->url, sizeof a->url);   /* zip or .tar.xz */
        if (!*a->url) { str_field(json, t, k, "dol", a->url, sizeof a->url); a->bare = *a->url != 0; }
        a->size = (unsigned long long)json_int(json, t, json_find(json, t, k, "zip_size"), 0);
        str_field(json, t, k, "sha256", a->sha256, sizeof a->sha256);
        str_field(json, t, k, "root", a->root, sizeof a->root);
        str_field(json, t, k, "binary", a->binary, sizeof a->binary);
        str_field(json, t, k, "folder", a->folder, sizeof a->folder);
        a->updated = json_int(json, t, json_find(json, t, k, "updated"), 0);
        if (!*a->binary) scopy(a->binary, sizeof a->binary, "boot.dol");
        if (!*a->folder) scopy(a->folder, sizeof a->folder, a->slug);
        if (!*a->name) scopy(a->name, sizeof a->name, a->slug);
        if (!*a->version) scopy(a->version, sizeof a->version, "0");
        int keep = json_find(json, t, k, "keep");
        if (keep >= 0 && t[keep].type == JT_ARRAY)
            for (int e = json_first(t, keep), i = 0; e >= 0 && i < t[keep].size && a->nkeep < 4; e = json_next(t, e), i++) {
                char rel[64];
                if (json_string(json, t, e, rel, sizeof rel) && fs_safe_relative(rel))
                    scopy(a->keep[a->nkeep++], sizeof a->keep[0], rel);
            }
        int root = json_find(json, t, k, "copy_to_root");
        if (root >= 0 && t[root].type == JT_ARRAY)
            for (int e = json_first(t, root), i = 0; e >= 0 && i < t[root].size && a->nto_root < 4; e = json_next(t, e), i++) {
                char rel[48];
                if (json_string(json, t, e, rel, sizeof rel) && fs_safe_relative(rel) && !strchr(rel, '/') &&
                    strcasecmp(rel, "apps"))
                    scopy(a->to_root[a->nto_root++], sizeof a->to_root[0], rel);
            }
        /* reject entries that could not be installed safely */
        bool ok = slug_ok(a->slug) && url_ok(a->url) && name_ok(a->folder) && fs_safe_relative(a->binary) &&
                  (!*a->root || fs_safe_relative(a->root)) && strcasecmp(a->folder, "gc-homebrew-hub-browser") != 0;
        if (ok && a->bare) {
            size_t bl = strlen(a->binary);
            ok = bl > 4 && !strcasecmp(a->binary + bl - 4, ".dol") && !strchr(a->binary, '/');
        }
        if (ok && *a->sha256 && strlen(a->sha256) != 64) a->sha256[0] = 0;
        if (!ok) continue;
        bool dup = false;
        for (int i = start; i < *n; i++) if (!strcmp(out[i].slug, a->slug)) dup = true;
        if (dup) continue;
        (*n)++;
    }
    free(t);
    return true;
}

void catalog_replace_apps(App *arr, int n) {
    free(apps);
    apps = arr;
    napps = n;
    catalog_recompute();
}

bool catalog_version_newer(const char *cand, const char *have) {
    if (!*have) return false;
    const char *a = cand, *b = have;
    bool numeric = true;
    while (*a || *b) {
        if (!isdigit((unsigned char)*a) && *a) { numeric = false; break; }
        if (!isdigit((unsigned char)*b) && *b) { numeric = false; break; }
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x > y;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if ((*a && !isdigit((unsigned char)*a)) || (*b && !isdigit((unsigned char)*b))) { numeric = false; break; }
    }
    if (numeric) return false;
    return strcmp(cand, have) != 0;
}

/* ---- installed ---- */

int catalog_installed_count(void) { return ninst; }
Installed *catalog_installed(int i) { return (i >= 0 && i < ninst) ? &inst[i] : NULL; }

int catalog_installed_find(const char *slug) {
    for (int i = 0; i < ninst; i++) if (!strcmp(inst[i].slug, slug)) return i;
    return -1;
}

void catalog_installed_set(const char *slug, const char *name, const char *version, const char *folder, const char *binary) {
    int i = catalog_installed_find(slug);
    if (i < 0) {
        if (ninst >= MAX_INSTALLED) return;
        i = ninst++;
        memset(&inst[i], 0, sizeof inst[i]);
    }
    scopy(inst[i].slug, sizeof inst[i].slug, slug);
    scopy(inst[i].name, sizeof inst[i].name, name);
    scopy(inst[i].version, sizeof inst[i].version, version);
    scopy(inst[i].folder, sizeof inst[i].folder, folder);
    scopy(inst[i].binary, sizeof inst[i].binary, binary);
    inst[i].managed = true;
    inst[i].app = -1;
}

void catalog_installed_remove(const char *slug) {
    int i = catalog_installed_find(slug);
    if (i < 0) return;
    memmove(&inst[i], &inst[i + 1], (ninst - i - 1) * sizeof(Installed));
    ninst--;
}

bool catalog_installed_save(void) {
    char path[160];
    size_t cap = (size_t)MAX_INSTALLED * 200 + 1;
    char *buf = malloc(cap);
    if (!buf) return false;
    size_t o = 0;
    for (int i = 0; i < ninst; i++) {
        if (!inst[i].managed) continue;
        int n = snprintf(buf + o, cap - o, "%s\t%s\t%s\t%s\t%s\n", inst[i].slug, inst[i].version,
                         inst[i].folder, inst[i].binary, inst[i].name);
        if (n < 0 || o + n >= cap) break;
        o += n;
    }
    fs_join(path, sizeof path, datadir, "installed.txt");
    bool ok = fs_write_file(path, buf, o);
    free(buf);
    return ok;
}

static void installed_load(void) {
    char path[160], *text; size_t len;
    fs_join(path, sizeof path, datadir, "installed.txt");
    if (!fs_read_file(path, &text, &len, 65536)) return;
    char *line = text, *next;
    for (; line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *f[5] = { line };
        int nf = 1;
        for (char *p = line; *p && nf < 5; p++) if (*p == '\t') { *p = 0; f[nf++] = p + 1; }
        if (nf < 4 || !slug_ok(f[0]) || !name_ok(f[2])) continue;
        catalog_installed_set(f[0], nf > 4 ? f[4] : f[0], f[1], f[2], f[3]);
    }
    free(text);
}

/* Reads "slug\tversion\tname" left by an install. */
static bool read_stamp(const char *folder, char *slug, char *version, char *name) {
    char path[320], *text; size_t len;
    snprintf(path, sizeof path, APPS_DIR "/%s/.gc-homebrew-app", folder);
    if (!fs_read_file(path, &text, &len, 512)) return false;
    char *f[3] = { text, "", "" };
    int nf = 1;
    for (char *p = text; *p && nf < 3; p++) if (*p == '\t') { *p = 0; f[nf++] = p + 1; }
    char *nl = strchr(f[nf - 1], '\n'); if (nl) *nl = 0;
    bool ok = slug_ok(f[0]);
    if (ok) {
        scopy(slug, 32, f[0]); scopy(version, 24, f[1]); scopy(name, 48, *f[2] ? f[2] : folder);
    }
    free(text);
    return ok;
}

void catalog_scan_card(void) {
    DIR *d = opendir(APPS_DIR);
    if (!d) return;
    struct dirent *e;
    /* drop unmanaged entries first; they are rediscovered below */
    for (int i = 0; i < ninst;) {
        if (!inst[i].managed) { memmove(&inst[i], &inst[i + 1], (ninst - i - 1) * sizeof(Installed)); ninst--; }
        else i++;
    }
    while ((e = readdir(d)) != NULL && ninst < MAX_INSTALLED) {
        if (e->d_name[0] == '.' || !name_ok(e->d_name)) continue;
        char dir[280];
        snprintf(dir, sizeof dir, APPS_DIR "/%s", e->d_name);
        if (!fs_is_dir(dir)) continue;
        if (!strcasecmp(e->d_name, "gc-homebrew-hub-browser")) continue;
        bool known = false;
        for (int i = 0; i < ninst; i++) if (!strcasecmp(inst[i].folder, e->d_name)) known = true;
        if (known) continue;
        /* find a .dol: boot.dol first, else the first one */
        char binary[48] = "";
        DIR *ad = opendir(dir);
        if (!ad) continue;
        struct dirent *f;
        while ((f = readdir(ad)) != NULL) {
            size_t n = strlen(f->d_name);
            if (n > 4 && n < sizeof binary && !strcasecmp(f->d_name + n - 4, ".dol")) {
                if (!*binary || !strcasecmp(f->d_name, "boot.dol")) scopy(binary, sizeof binary, f->d_name);
            }
        }
        closedir(ad);
        if (!*binary) continue;
        char slug[32], version[24], name[48];
        if (read_stamp(e->d_name, slug, version, name)) {
            catalog_installed_set(slug, name, version, e->d_name, binary);
        } else {
            Installed *in = &inst[ninst++];
            memset(in, 0, sizeof *in);
            /* slug from the folder name, lower-cased */
            size_t n = 0;
            for (const char *p = e->d_name; *p && n < 31; p++) {
                char c = (char)tolower((unsigned char)*p);
                in->slug[n++] = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? c : '-';
            }
            in->slug[n] = 0;
            scopy(in->name, sizeof in->name, e->d_name);
            scopy(in->folder, sizeof in->folder, e->d_name);
            scopy(in->binary, sizeof in->binary, binary);
            in->managed = false;
            in->app = -1;
        }
    }
    closedir(d);
}

void catalog_recompute(void) {
    for (int i = 0; i < ninst; i++) inst[i].app = -1;
    for (int k = 0; k < napps; k++) {
        App *a = &apps[k];
        a->installed = -1; a->update = false;
        int i = catalog_installed_find(a->slug);
        if (i < 0)
            for (int j = 0; j < ninst; j++) if (!inst[j].managed && !strcasecmp(inst[j].folder, a->folder)) { i = j; break; }
        if (i < 0) continue;
        if (inst[i].app >= 0) continue;        /* first source wins */
        a->installed = i;
        inst[i].app = k;
        a->update = inst[i].managed && catalog_version_newer(a->version, inst[i].version);
    }
}

int catalog_update_count(void) {
    int n = 0;
    for (int k = 0; k < napps; k++) if (apps[k].update) n++;
    return n;
}

void catalog_init(const char *dir) {
    scopy(datadir, sizeof datadir, dir);
    char sub[160];
    fs_join(sub, sizeof sub, datadir, "cache"); fs_mkdirs(sub);
    sources_load();
    installed_load();
    catalog_scan_card();
    /* cached indexes make the lists usable before (or without) the network */
    App *arr = malloc(sizeof(App) * MAX_APPS);
    int n = 0;
    if (arr) {
        for (int i = 0; i < nsources; i++) {
            if (!sources[i].enabled) continue;
            char path[200], *text; size_t len;
            catalog_cache_path(sources[i].url, path, sizeof path);
            if (!fs_read_file(path, &text, &len, INDEX_MAX_BYTES)) continue;
            char e[64];
            int before = n;
            if (catalog_parse_index(text, len, i, arr, &n, MAX_APPS, sources[i].name, sizeof sources[i].name, e, sizeof e)) {
                sources[i].napps = n - before;
                snprintf(sources[i].status, sizeof sources[i].status, "%d apps (saved copy)", n - before);
            }
            free(text);
        }
        catalog_replace_apps(arr, n);
    }
}
