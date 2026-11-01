#include "store.h"
#include "catalog.h"
#include "fsutil.h"
#include "net.h"
#include "settings.h"
#include "digest.h"
#include "zipx.h"
#include <ctype.h>
#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum { JOB_NONE, JOB_START, JOB_SOURCES, JOB_INDEXES, JOB_INSTALL, JOB_REMOVE } Job;

#define STACK_SIZE (160 * 1024)
#define MSG_SLOTS 6

static lwp_t thread;
static sem_t wake;
static u8 *stack;

/* request (UI -> worker) */
static volatile Job job;
static App job_app;
static bool job_replace_root;                 /* JOB_INSTALL: replace_at_root agreed to */
static char job_old_version[24], job_old_binary[48];   /* what is installed now, if the store did it */
static Installed job_inst;
static volatile int cancel_flag;

/* live progress (worker -> UI) */
static volatile StoreState state = STORE_STARTING;
static char step[128];
static volatile size_t prog_done, prog_total;
static volatile int prog_known;
static NetReq req;                 /* the current download; progress read by the UI */
static volatile int downloading;

/* result (worker -> UI), consumed in store_poll */
static volatile Job done_job;
static volatile int done_ok;
static char done_err[160];
static char done_note[160];        /* JOB_INSTALL: what happened to the root files */
static volatile int done_note_error;
static char *sources_text;         /* JOB_SOURCES */
static size_t sources_len;
static App *new_apps; static int new_napps;             /* JOB_INDEXES */
static struct { int napps; char status[64]; char name[48]; } sres[MAX_SOURCES];
static int sres_n;

static bool net_up;
static char net_err[128];
static bool ca_loaded;

static struct { char text[160]; bool error; } msgs[MSG_SLOTS];
static int msg_head, msg_count;

static void set_step(const char *s) { snprintf(step, sizeof step, "%s", s); }
static void set_progress(size_t done, size_t total) { prog_done = done; prog_total = total; prog_known = total != 0; }

static void push_msg(const char *text, bool error) {
    int i = (msg_head + msg_count) % MSG_SLOTS;
    if (msg_count == MSG_SLOTS) { msg_head = (msg_head + 1) % MSG_SLOTS; i = (msg_head + MSG_SLOTS - 1) % MSG_SLOTS; }
    else msg_count++;
    snprintf(msgs[i].text, sizeof msgs[i].text, "%s", text);
    msgs[i].error = error;
}

bool store_take_message(char *out, size_t cap, bool *error) {
    if (!msg_count) return false;
    snprintf(out, cap, "%s", msgs[msg_head].text);
    *error = msgs[msg_head].error;
    msg_head = (msg_head + 1) % MSG_SLOTS;
    msg_count--;
    return true;
}

/* ---- worker side ---- */

/* A dropped connection is worth another try (Dolphin's emulated adapter cuts
 * some short, and real networks drop some too); an answer such as 404 is not.
 * Status 0: no answer at all; 2xx: the body was cut short; 5xx: the server's
 * trouble, often brief. */
static bool worth_retry(const NetReq *r) {
    return !cancel_flag && (r->status == 0 || (r->status >= 200 && r->status < 300) || r->status >= 500);
}

#define FETCH_TRIES 3

static bool fetch_text(const char *url, size_t max, char **out, size_t *len, char *err, size_t errlen) {
    for (int t = 1; ; t++) {
        memset(&req, 0, sizeof req);
        req.url = url; req.max_mem = max; req.cancel = &cancel_flag;
        downloading = 1;
        bool ok = net_get(&req);
        downloading = 0;
        if (ok) {
            *out = req.mem; *len = req.len;
            req.mem = NULL;
            return true;
        }
        free(req.mem); req.mem = NULL;
        if (t == FETCH_TRIES || !worth_retry(&req)) {
            snprintf(err, errlen, "%s", req.err);
            return false;
        }
        usleep(t * 1000000);                  /* 1 s, then 2 s */
    }
}

static void job_start(void) {
    state = STORE_STARTING;
    set_step("Connecting to the network");
    char err[128];
    net_up = net_start(err, sizeof err);
    if (!net_up) {
        snprintf(net_err, sizeof net_err, "%s", err);
        state = STORE_OFFLINE;
        done_ok = 0; snprintf(done_err, sizeof done_err, "No network: %s", err);
        return;
    }
    net_err[0] = 0;
    if (g_settings.verify_tls) {
        set_step("Loading certificates");
        char path[160], *pem; size_t len;
        fs_join(path, sizeof path, catalog_datadir(), "cacert.pem");
        if (fs_read_file(path, &pem, &len, 400 * 1024)) {
            ca_loaded = net_set_ca(pem, len, err, sizeof err);
            free(pem);
            if (!ca_loaded) snprintf(done_err, sizeof done_err, "Certificates not loaded: %s", err);
        } else if (!catalog_have_card()) snprintf(done_err, sizeof done_err, "No SD card, so no cacert.pem: servers are not verified");
        else snprintf(done_err, sizeof done_err, "cacert.pem is missing next to boot.dol; servers are not verified");
    }
    done_ok = 1;
}

static void job_sources(void) {
    state = STORE_REFRESHING;
    set_step("Loading the source list");
    set_progress(0, 0);
    char err[128];
    sources_text = NULL;
    if (!g_settings.sources_url[0]) { done_ok = 1; return; }
    done_ok = fetch_text(g_settings.sources_url, 64 * 1024, &sources_text, &sources_len, err, sizeof err);
    if (!done_ok) snprintf(done_err, sizeof done_err, "Source list not loaded: %s", err);
}

static void job_indexes(void) {
    state = STORE_REFRESHING;
    new_apps = malloc(sizeof(App) * MAX_APPS);
    new_napps = 0;
    sres_n = catalog_source_count();
    if (!new_apps) { done_ok = 0; snprintf(done_err, sizeof done_err, "Out of memory"); return; }
    for (int i = 0; i < sres_n; i++) {
        Source *s = catalog_source(i);
        sres[i].napps = s->napps;
        snprintf(sres[i].status, sizeof sres[i].status, "%s", s->status);
        snprintf(sres[i].name, sizeof sres[i].name, "%s", s->name);
        if (!s->enabled) { sres[i].napps = 0; snprintf(sres[i].status, sizeof sres[i].status, "Off"); continue; }
        if (cancel_flag) break;
        char msg[96];
        snprintf(msg, sizeof msg, "Loading %s", s->name);
        set_step(msg);
        set_progress(i, sres_n);
        char err[128], *text; size_t len;
        if (!fetch_text(s->url, INDEX_MAX_BYTES, &text, &len, err, sizeof err)) {
            sres[i].napps = -1;
            snprintf(sres[i].status, sizeof sres[i].status, "Failed: %.54s", err);
            /* keep the saved copy so the apps stay listed */
            char path[200];
            catalog_cache_path(s->url, path, sizeof path);
            if (fs_read_file(path, &text, &len, INDEX_MAX_BYTES)) {
                int before = new_napps;
                if (catalog_parse_index(text, len, i, new_apps, &new_napps, MAX_APPS, NULL, 0, err, sizeof err))
                    sres[i].napps = new_napps - before;
                free(text);
            }
            continue;
        }
        int before = new_napps;
        if (!catalog_parse_index(text, len, i, new_apps, &new_napps, MAX_APPS, sres[i].name, sizeof sres[i].name, err, sizeof err)) {
            sres[i].napps = -1;
            snprintf(sres[i].status, sizeof sres[i].status, "Failed: %.54s", err);
        } else {
            sres[i].napps = new_napps - before;
            snprintf(sres[i].status, sizeof sres[i].status, "%d app%s", sres[i].napps, sres[i].napps == 1 ? "" : "s");
            char path[200];
            catalog_cache_path(s->url, path, sizeof path);
            fs_write_file(path, text, len);
        }
        free(text);
    }
    done_ok = 1;
}

static bool replace_dir(const char *dest, const char *fresh, char *err, size_t errlen) {
    char old[208];
    snprintf(old, sizeof old, "%.200s.old", dest);
    fs_remove_tree(old);
    bool had = fs_exists(dest);
    if (had && rename(dest, old) != 0) { snprintf(err, errlen, "Cannot replace the old version"); return false; }
    if (rename(fresh, dest) != 0) {
        if (had) rename(old, dest);
        snprintf(err, errlen, "Cannot move the app into place");
        return false;
    }
    if (had) fs_remove_tree(old);
    return true;
}

static void job_install(void) {
    state = STORE_INSTALLING;
    App *a = &job_app;
    char err[128], dl[200], stage[200], appdir[260], dest[200], path[300];
    const char *data = catalog_datadir();
    snprintf(dl, sizeof dl, "%s/dl/%.31s.%s", data, a->slug, a->bare ? "dol" : "pkg");
    snprintf(stage, sizeof stage, "%s/stage/%.31s", data, a->slug);
    snprintf(dest, sizeof dest, APPS_DIR "/%.47s", a->folder);
    done_ok = 0;

    char dir[200];
    snprintf(dir, sizeof dir, "%s/dl", data); fs_mkdirs(dir);
    snprintf(dir, sizeof dir, "%s/stage", data); fs_mkdirs(dir);
    fs_remove_tree(stage);

    unsigned long long freeb;
    if (a->size && fs_free_space("sd:/", &freeb) && freeb < a->size * 3 + (4u << 20)) {
        snprintf(done_err, sizeof done_err, "Not enough room on the card (%llu MB needed)", (a->size * 3 + (4u << 20)) >> 20);
        return;
    }

    set_step("Downloading");
    memset(&req, 0, sizeof req);
    req.url = a->url; req.path = dl; req.cancel = &cancel_flag;
    downloading = 1;
    bool ok = net_get(&req);
    downloading = 0;
    if (!ok) { snprintf(done_err, sizeof done_err, "Download failed: %s", req.err); remove(dl); return; }
    if (a->size && (unsigned long long)fs_file_size(dl) != a->size) {
        snprintf(done_err, sizeof done_err, "The download is not the size the catalog says; try again later");
        remove(dl); return;
    }
    if (*a->sha256) {
        set_step("Checking the download");
        set_progress(0, (size_t)fs_file_size(dl));
        char hex[65];
        if (!sha256_file(dl, hex, &prog_done) || strcasecmp(hex, a->sha256)) {
            snprintf(done_err, sizeof done_err, "The download is damaged or not what the catalog expects");
            remove(dl); return;
        }
    }
    if (cancel_flag) { snprintf(done_err, sizeof done_err, "Cancelled"); remove(dl); return; }

    if (a->bare) {
        set_step("Preparing");
        set_progress(0, 0);
        snprintf(appdir, sizeof appdir, "%s", stage);
        if (!fs_mkdirs(appdir) || !fs_join(path, sizeof path, appdir, a->binary) ||
            (rename(dl, path) != 0 && !fs_copy_file(dl, path))) {
            snprintf(done_err, sizeof done_err, "Cannot write the program to the card");
            remove(dl); fs_remove_tree(stage); return;
        }
        remove(dl);
    } else {
        set_step("Unpacking");
        set_progress(0, 0);
        /* the app's folder, plus the files it may put at the root of the card */
        const char *only[4] = { a->root };
        char outer[2][sizeof a->replace[0].from];
        for (int i = 0; *a->root && i < a->nreplace; i++) {
            snprintf(outer[i], sizeof outer[i], "%s", a->replace[i].from);
            char *bang = strchr(outer[i], '!');
            if (bang) *bang = 0;
            only[1 + i] = outer[i];
        }
        if (!unpack_to_dir(dl, stage, *a->root ? only : NULL, &cancel_flag, &prog_done, &prog_total, err, sizeof err)) {
            snprintf(done_err, sizeof done_err, "%s", err);
            remove(dl); fs_remove_tree(stage); return;
        }
        prog_known = 1;
        remove(dl);
        if (*a->root) fs_join(appdir, sizeof appdir, stage, a->root);
        else snprintf(appdir, sizeof appdir, "%s", stage);
    }
    if (!fs_join(path, sizeof path, appdir, a->binary) || !fs_exists(path)) {
        snprintf(done_err, sizeof done_err, "The download has no %.47s inside", a->binary);
        fs_remove_tree(stage); return;
    }
    set_step("Installing");
    /* files the app keeps across updates */
    for (int i = 0; i < a->nkeep; i++) {
        char from[300], to[300];
        if (fs_join(from, sizeof from, dest, a->keep[i]) && fs_join(to, sizeof to, appdir, a->keep[i]) && fs_exists(from)) {
            char *slash = strrchr(to, '/');
            if (slash) { *slash = 0; fs_mkdirs(to); *slash = '/'; }
            fs_copy_tree(from, to, false);
        }
    }
    /* stamp so the folder is recognised even without installed.txt */
    char stamp[160];
    int sl = snprintf(stamp, sizeof stamp, "%s\t%s\t%s\n", a->slug, a->version, a->name);
    if (fs_join(path, sizeof path, appdir, ".gc-homebrew-app")) fs_write_file(path, stamp, (size_t)sl);
    /* root files come out of the download now, before its folder goes: each
     * is copied, or unpacked from a zip inside the download, to a file beside
     * the stage */
    char hold[2][220], hold_err[2][80];
    bool held[2] = { false, false };
    for (int i = 0; i < a->nreplace; i++) {
        char target[64], outer[sizeof a->replace[0].from], src[330];
        snprintf(hold[i], sizeof hold[i], "%.200s.root%d", stage, i);
        hold_err[i][0] = 0;
        remove(hold[i]);
        snprintf(target, sizeof target, "sd:/%s", a->replace[i].to);
        if (!job_replace_root || !fs_exists(target)) continue;
        snprintf(outer, sizeof outer, "%s", a->replace[i].from);
        char *member = strchr(outer, '!');
        if (member) *member++ = 0;
        if (!fs_join(src, sizeof src, stage, outer) || !fs_exists(src))
            snprintf(hold_err[i], sizeof hold_err[i], "the download has no %.50s", outer);
        else if (member)
            held[i] = unzip_file(src, member, hold[i], hold_err[i], sizeof hold_err[i]);
        else if (!(held[i] = fs_copy_file(src, hold[i])))
            snprintf(hold_err[i], sizeof hold_err[i], "could not copy it out of the download");
    }
    /* one archive folder per install, named after the version the root files
     * are: the installed one when a root file is that version's program, which
     * can only be told before the old folder goes */
    char label[32];
    bool known = false;
    for (int i = 0; i < a->nreplace && *job_old_version && *job_old_binary; i++) {
        char target[64], old[300];
        snprintf(target, sizeof target, "sd:/%s", a->replace[i].to);
        if (fs_join(old, sizeof old, dest, job_old_binary) && fs_files_equal(target, old)) known = true;
    }
    snprintf(label, sizeof label, known ? "%s" : "before-%s", known ? job_old_version : a->version);
    for (char *c = label; *c; c++)
        if (!(isalnum((unsigned char)*c) || *c == '.' || *c == '-' || *c == '_')) *c = '_';
    fs_mkdirs(APPS_DIR);
    if (!replace_dir(dest, appdir, err, sizeof err)) {
        snprintf(done_err, sizeof done_err, "%s", err);
        fs_remove_tree(stage);
        for (int i = 0; i < a->nreplace; i++) remove(hold[i]);
        return;
    }
    fs_remove_tree(stage);
    /* data folders the app wants at the root of the card; existing files win */
    for (int i = 0; i < a->nto_root; i++) {
        char from[300], to[300];
        if (fs_join(from, sizeof from, dest, a->to_root[i]) && fs_exists(from)) {
            snprintf(to, sizeof to, "sd:/%s", a->to_root[i]);
            fs_copy_tree(from, to, true);
        }
    }
    /* boot files at the root, only those already there and only when asked */
    done_note[0] = 0; done_note_error = 0;
    for (int i = 0; job_replace_root && i < a->nreplace; i++) {
        RootFile *r = &a->replace[i];
        char target[64], arch[100], moved[300], e[80], note[200];
        snprintf(target, sizeof target, "sd:/%s", r->to);
        if (!fs_exists(target)) continue;
        if (!held[i]) {
            snprintf(note, sizeof note, "%s was not updated: %.79s", target, hold_err[i]);
            done_note_error = 1;
        } else if (fs_files_equal(hold[i], target)) {
            remove(hold[i]);
            continue;                                   /* already this version */
        } else {
            set_step("Updating the boot files");
            snprintf(arch, sizeof arch, "sd:/%s/%s", r->archive, label);
            bool ok = fs_replace_archiving(hold[i], target, arch, moved, sizeof moved, e, sizeof e);
            remove(hold[i]);
            if (ok) {
                char *slash = strrchr(moved, '/');
                if (slash) *slash = 0;
                snprintf(note, sizeof note, "%s updated; the old one is in %.100s", target, moved);
            } else {
                snprintf(note, sizeof note, "%s was not updated: %s", target, e);
                done_note_error = 1;
            }
        }
        /* appended by hand: one sentence per file, cut at the end if need be */
        size_t l = strlen(done_note), n = strlen(note);
        if (l && l + 2 < sizeof done_note) { memcpy(done_note + l, ". ", 2); l += 2; }
        if (n > sizeof done_note - 1 - l) n = sizeof done_note - 1 - l;
        memcpy(done_note + l, note, n);
        done_note[l + n] = 0;
    }
    for (int i = 0; i < a->nreplace; i++) remove(hold[i]);
    done_ok = 1;
}

static void job_remove(void) {
    state = STORE_REMOVING;
    set_step("Removing");
    set_progress(0, 0);
    char dest[200];
    snprintf(dest, sizeof dest, APPS_DIR "/%.47s", job_inst.folder);
    done_ok = fs_remove_tree(dest);
    if (!done_ok) snprintf(done_err, sizeof done_err, "Could not remove every file of %s", job_inst.name);
}

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        LWP_SemWait(wake);
        Job j = job;
        if (j == JOB_NONE) continue;
        done_err[0] = 0;
        set_progress(0, 0);
        switch (j) {
        case JOB_START: job_start(); break;
        case JOB_SOURCES: job_sources(); break;
        case JOB_INDEXES: job_indexes(); break;
        case JOB_INSTALL: job_install(); break;
        case JOB_REMOVE: job_remove(); break;
        default: break;
        }
        state = net_up ? STORE_IDLE : STORE_OFFLINE;
        set_step("");
        job = JOB_NONE;
        done_job = j;
    }
    return NULL;
}

/* ---- UI side ---- */

static bool submit(Job j) {
    if (job != JOB_NONE || done_job != JOB_NONE) return false;
    cancel_flag = 0;
    job = j;
    LWP_SemPost(wake);
    return true;
}

void store_init(void) {
    LWP_SemInit(&wake, 0, 8);
    stack = memalign(32, STACK_SIZE);
    LWP_CreateThread(&thread, worker, NULL, stack, STACK_SIZE, 40);
    submit(JOB_START);
}

static bool refresh_queued;

void store_poll(void) {
    Job j = done_job;
    if (j == JOB_NONE) {
        if (refresh_queued && job == JOB_NONE) { refresh_queued = false; submit(JOB_SOURCES); }
        return;
    }
    switch (j) {
    case JOB_START:
        if (done_err[0]) push_msg(done_err, !done_ok);
        if (done_ok && g_settings.auto_refresh) refresh_queued = true;
        break;
    case JOB_SOURCES: {
        if (sources_text) {
            char err[96];
            if (!catalog_merge_default_sources(sources_text, sources_len, err, sizeof err)) push_msg(err, true);
            free(sources_text); sources_text = NULL;
        } else if (done_err[0]) push_msg(done_err, true);
        done_job = JOB_NONE;
        submit(JOB_INDEXES);
        return;
    }
    case JOB_INDEXES:
        if (done_ok) {
            int failed = 0, first = -1;
            for (int i = 0; i < sres_n && i < catalog_source_count(); i++) {
                Source *s = catalog_source(i);
                s->napps = sres[i].napps;
                snprintf(s->status, sizeof s->status, "%s", sres[i].status);
                if (sres[i].name[0] && !s->builtin) snprintf(s->name, sizeof s->name, "%s", sres[i].name);
                if (s->enabled && sres[i].napps < 0 && !failed++) first = i;
            }
            catalog_replace_apps(new_apps, new_napps);
            new_apps = NULL;
            char m[120];
            int up = catalog_update_count();
            if (failed == 1) {               /* say why; the status reads "Failed: <reason>" */
                const char *why = catalog_source(first)->status;
                if (!strncmp(why, "Failed: ", 8)) why += 8;
                snprintf(m, sizeof m, "%.36s not loaded: %.66s", catalog_source(first)->name, why);
            } else if (failed) snprintf(m, sizeof m, "%d sources could not be loaded", failed);
            else if (up) snprintf(m, sizeof m, "%d update%s available", up, up == 1 ? "" : "s");
            else snprintf(m, sizeof m, "%d apps listed", catalog_app_count());
            push_msg(m, failed != 0);
        } else {
            free(new_apps); new_apps = NULL;
            push_msg(done_err, true);
        }
        break;
    case JOB_INSTALL: {
        char m[120];
        if (done_ok) {
            catalog_installed_set(job_app.slug, job_app.name, job_app.version, job_app.folder, job_app.binary);
            catalog_installed_save();
            catalog_recompute();
            snprintf(m, sizeof m, "%s %s installed", job_app.name, job_app.version);
            push_msg(m, false);
            if (*done_note) push_msg(done_note, done_note_error);
        } else push_msg(done_err, true);
        break;
    }
    case JOB_REMOVE:
        if (done_ok) {
            char m[120];
            catalog_installed_remove(job_inst.slug);
            catalog_installed_save();
            catalog_scan_card();
            catalog_recompute();
            snprintf(m, sizeof m, "%s removed", job_inst.name);
            push_msg(m, false);
        } else push_msg(done_err, true);
        break;
    default: break;
    }
    done_job = JOB_NONE;
}

StoreState store_state(void) { return state; }
bool store_busy(void) { return job != JOB_NONE || done_job != JOB_NONE || refresh_queued; }
const char *store_step(void) { return step; }
const char *store_job_name(void) {
    if (job == JOB_INSTALL) return job_app.name;
    if (job == JOB_REMOVE) return job_inst.name;
    return "";
}

float store_progress(void) {
    if (downloading) {
        if (req.total) return (float)req.received / (float)req.total;
        if (job == JOB_INSTALL && job_app.size) return (float)req.received / (float)job_app.size;
        return -1;
    }
    if (prog_known && prog_total) return (float)prog_done / (float)prog_total;
    return -1;
}

void store_request_refresh(void) {
    if (store_busy() || !net_up) return;
    refresh_queued = true;
}

void store_request_install(int i, bool replace_root) {
    App *a = catalog_app(i);
    if (!a || store_busy() || !net_up || !catalog_have_card()) return;
    job_app = *a;
    job_replace_root = replace_root;
    Installed *in = a->installed >= 0 ? catalog_installed(a->installed) : NULL;
    snprintf(job_old_version, sizeof job_old_version, "%s", in && in->managed ? in->version : "");
    snprintf(job_old_binary, sizeof job_old_binary, "%s", in && in->managed ? in->binary : "");
    submit(JOB_INSTALL);
}

int store_root_targets(int i, char *list, size_t cap) {
    App *a = catalog_app(i);
    int n = 0;
    if (cap) list[0] = 0;
    if (!a || !catalog_have_card()) return 0;
    for (int k = 0; k < a->nreplace; k++) {
        char target[64];
        snprintf(target, sizeof target, "sd:/%s", a->replace[k].to);
        if (!fs_exists(target)) continue;
        size_t l = strlen(list);
        snprintf(list + l, cap - l, "%s%s", n ? " and " : "", target);
        n++;
    }
    return n;
}

void store_request_remove(int i) {
    Installed *in = catalog_installed(i);
    if (!in || store_busy() || !catalog_have_card()) return;
    job_inst = *in;
    submit(JOB_REMOVE);
}

void store_cancel(void) { if (job != JOB_NONE) cancel_flag = 1; }
bool store_net_up(void) { return net_up; }
const char *store_net_error(void) { return net_err; }
bool store_tls_verified(void) { return ca_loaded; }
