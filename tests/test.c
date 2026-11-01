/* test.c - exercises the portable modules on the host. Runs inside tests/run
 * with a folder literally named "sd:" standing in for the card. */
#include "catalog.h"
#include "digest.h"
#include "fsutil.h"
#include "json.h"
#include "log.h"
#include "memstat.h"
#include "tarxz.h"
#include <lzma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_json(void) {
    const char *t = "{\"format\": 1, \"name\": \"A \\\"b\\\" \\u00e9\", \"apps\": [ {\"x\": true}, {\"y\": -5}, [] ], \"n\": 42}";
    JTok tok[64];
    int n = json_parse(t, strlen(t), tok, 64);
    CHECK(n > 0, "parse failed");
    CHECK(json_int(t, tok, json_find(t, tok, 0, "format"), 0) == 1, "format");
    CHECK(json_int(t, tok, json_find(t, tok, 0, "n"), 0) == 42, "n");
    char s[64];
    CHECK(json_string(t, tok, json_find(t, tok, 0, "name"), s, sizeof s) && !strcmp(s, "A \"b\" ?"), "string unescape: '%s'", s);
    int arr = json_find(t, tok, 0, "apps");
    CHECK(arr >= 0 && tok[arr].type == JT_ARRAY && tok[arr].size == 3, "array size");
    int e = json_first(tok, arr);
    CHECK(json_bool(t, tok, json_find(t, tok, e, "x"), false) == true, "x");
    e = json_next(tok, e);
    CHECK(json_int(t, tok, json_find(t, tok, e, "y"), 0) == -5, "y");
    e = json_next(tok, e);
    CHECK(tok[e].type == JT_ARRAY && tok[e].size == 0, "empty array");
    CHECK(json_next(tok, arr) == json_find(t, tok, 0, "n") - 1, "walking past the array lands on the next key");
    CHECK(json_parse("{\"a\":", 5, tok, 64) < 0, "truncated rejected");
    CHECK(json_parse("[1,]", 4, tok, 64) < 0, "trailing comma rejected");
    CHECK(json_parse("{} x", 4, tok, 64) < 0, "trailing junk rejected");
    CHECK(json_parse("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]", 60, tok, 64) < 0, "depth limit");
    CHECK(json_parse("[1,2,3]", 7, tok, 2) < 0, "token limit");
}

static void test_sha(void) {
    unsigned char d[32]; char hex[65];
    Sha256 s; sha256_init(&s); sha256_update(&s, "abc", 3); sha256_final(&s, d); sha256_hex(d, hex);
    CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "sha abc: %s", hex);
    sha256_init(&s); sha256_final(&s, d); sha256_hex(d, hex);
    CHECK(!strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "sha empty");
    /* one million 'a' in odd chunks */
    char buf[1000]; memset(buf, 'a', sizeof buf);
    sha256_init(&s);
    for (int i = 0; i < 1000; i++) sha256_update(&s, buf, (i % 7) ? 1000 : 1000);
    sha256_final(&s, d); sha256_hex(d, hex);
    CHECK(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), "sha million a: %s", hex);
    fs_write_file("blob.bin", "hello world\n", 12);
    CHECK(sha256_file("blob.bin", hex, NULL) && !strcmp(hex, "a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447"), "sha file: %s", hex);
}

static const char *index_json =
"{ \"format\": 1, \"name\": \"Test Apps\", \"apps\": ["
" {\"slug\":\"demo\",\"name\":\"Demo\",\"author\":\"me\",\"version\":\"1.2\",\"category\":\"demos\",\"short\":\"s\",\"long\":\"l\","
"  \"zip\":\"http://x/demo.zip\",\"zip_size\":123,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
"  \"keep\":[\"config.ini\",\"../bad\"],\"copy_to_root\":[\"demo\",\"apps\",\"a/b\"]},"
" {\"slug\":\"bare\",\"name\":\"Bare\",\"version\":\"b12\",\"dol\":\"http://x/bare.dol\",\"binary\":\"bare.dol\"},"
" {\"slug\":\"BadSlug\",\"zip\":\"http://x/a.zip\"},"
" {\"slug\":\"nourl\",\"name\":\"No url\"},"
" {\"slug\":\"evil\",\"zip\":\"http://x/e.zip\",\"binary\":\"../../boot.dol\"},"
" {\"slug\":\"demo\",\"zip\":\"http://x/dup.zip\"},"
" {\"slug\":\"self\",\"zip\":\"http://x/s.zip\",\"folder\":\"gc-homebrew-hub-browser\"},"
" {\"slug\":\"baredir\",\"dol\":\"http://x/d.dol\",\"binary\":\"sub/x.dol\"}"
"] }";

static void test_catalog(void) {
    fs_remove_tree("data"); fs_remove_tree("sd:"); fs_mkdirs("sd:");
    fs_mkdirs("sd:/apps/other-app"); fs_write_file("sd:/apps/other-app/boot.dol", "x", 1);
    fs_mkdirs("sd:/apps/stamped"); fs_write_file("sd:/apps/stamped/run.dol", "x", 1);
    fs_write_file("sd:/apps/stamped/.gc-homebrew-app", "bare\tb11\tBare app\n", 18);
    catalog_init("data");
    CHECK(catalog_source_count() == 0, "no sources at first");
    char err[96];
    CHECK(!catalog_source_add("ftp://x", err, sizeof err), "ftp rejected");
    CHECK(catalog_source_add("http://127.0.0.1:8765/index.json", err, sizeof err), "add: %s", err);
    CHECK(!catalog_source_add("http://127.0.0.1:8765/index.json", err, sizeof err), "duplicate rejected");
    CHECK(!strcmp(catalog_source(0)->name, "127.0.0.1:8765"), "name from host: %s", catalog_source(0)->name);
    const char *defaults = "{\"format\":1,\"sources\":[{\"name\":\"Main\",\"url\":\"https://example.com/index.json\"},{\"url\":\"bad\"}]}";
    CHECK(catalog_merge_default_sources(defaults, strlen(defaults), err, sizeof err), "merge: %s", err);
    CHECK(catalog_source_count() == 2 && catalog_source(1)->builtin && !strcmp(catalog_source(1)->name, "Main"), "builtin added");
    CHECK(!catalog_source_remove(1), "builtin not removable");
    catalog_source_toggle(1);
    CHECK(!catalog_source(1)->enabled, "toggled off");
    CHECK(!catalog_merge_default_sources("{\"format\":2}", 12, err, sizeof err), "bad format rejected");

    App *arr = malloc(sizeof(App) * MAX_APPS);
    int n = 0; char name[48] = "";
    CHECK(catalog_parse_index(index_json, strlen(index_json), 0, arr, &n, MAX_APPS, name, sizeof name, err, sizeof err), "parse: %s", err);
    CHECK(!strcmp(name, "Test Apps"), "catalog name");
    CHECK(n == 2, "2 valid apps, got %d", n);
    CHECK(!strcmp(arr[0].slug, "demo") && !arr[0].bare && arr[0].size == 123 && arr[0].nkeep == 1 && arr[0].nto_root == 1, "demo fields");
    CHECK(!strcmp(arr[0].folder, "demo") && !strcmp(arr[0].binary, "boot.dol"), "defaults");
    CHECK(!strcmp(arr[1].slug, "bare") && arr[1].bare && !strcmp(arr[1].binary, "bare.dol"), "bare app");
    catalog_replace_apps(arr, n);
    CHECK(catalog_installed_count() == 2, "scan found 2 folders, got %d", catalog_installed_count());
    int si = catalog_installed_find("bare");
    CHECK(si >= 0 && catalog_installed(si)->managed && !strcmp(catalog_installed(si)->version, "b11"), "stamp read");
    CHECK(catalog_app(1)->installed == si && catalog_app(1)->update, "bare has an update (b11 -> b12)");
    CHECK(catalog_update_count() == 1, "one update");
    int oi = catalog_installed_find("other-app");
    CHECK(oi >= 0 && !catalog_installed(oi)->managed && catalog_installed(oi)->app < 0, "unmanaged app listed");

    catalog_installed_set("demo", "Demo", "1.2", "demo", "boot.dol");
    CHECK(catalog_installed_save(), "save installed");
    catalog_recompute();
    CHECK(catalog_app(0)->installed >= 0 && !catalog_app(0)->update, "demo installed, current");
    CHECK(catalog_version_newer("1.10", "1.9") && !catalog_version_newer("1.2", "1.2") && catalog_version_newer("b12", "b11") && !catalog_version_newer("1.2", ""), "version compare");
    CHECK(!catalog_version_newer("1.2", "1.10"), "1.2 is not newer than 1.10");
    CHECK(catalog_sources_save(), "save sources");
    char path[200]; catalog_cache_path(catalog_source(0)->url, path, sizeof path);
    CHECK(fs_write_file(path, index_json, strlen(index_json)), "write cache");

    /* a second start: everything comes back from the files */
    catalog_init("data");
    CHECK(catalog_source_count() == 2 && !catalog_source(1)->enabled && catalog_source(1)->builtin, "sources reloaded");
    CHECK(catalog_app_count() == 2, "apps from cache: %d", catalog_app_count());
    CHECK(catalog_installed_find("demo") >= 0 && catalog_installed_count() == 3, "installed reloaded: %d", catalog_installed_count());
    catalog_installed_remove("demo");
    CHECK(catalog_installed_find("demo") < 0, "removed");
}

static void test_fs(void) {
    CHECK(fs_safe_relative("a/b.c") && !fs_safe_relative("/a") && !fs_safe_relative("a/../b") && !fs_safe_relative("c:x") && !fs_safe_relative("") && !fs_safe_relative("a\\b"), "safe_relative");
    CHECK(fs_mkdirs("sd:/deep/er/path") && fs_is_dir("sd:/deep/er/path"), "mkdirs");
    CHECK(fs_write_file("sd:/deep/f.txt", "abc", 3) && fs_file_size("sd:/deep/f.txt") == 3, "write");
    CHECK(fs_copy_tree("sd:/deep", "sd:/copy", true) && fs_file_size("sd:/copy/f.txt") == 3 && fs_is_dir("sd:/copy/er/path"), "copy tree");
    fs_write_file("sd:/copy/f.txt", "zz", 2);
    CHECK(fs_copy_tree("sd:/deep", "sd:/copy", true) && fs_file_size("sd:/copy/f.txt") == 2, "keep existing");
    CHECK(fs_copy_tree("sd:/deep", "sd:/copy", false) && fs_file_size("sd:/copy/f.txt") == 3, "overwrite");
    CHECK(fs_remove_tree("sd:/deep") && !fs_exists("sd:/deep"), "remove tree");
    char *t; size_t l;
    CHECK(fs_read_file("sd:/copy/f.txt", &t, &l, 100) && l == 3 && !strcmp(t, "abc"), "read"); free(t);
    CHECK(!fs_read_file("sd:/copy/f.txt", &t, &l, 2), "read max");
    char j[16];
    CHECK(fs_join(j, sizeof j, "sd:/a/", "b") && !strcmp(j, "sd:/a/b") && !fs_join(j, sizeof j, "sd:/aaaaaaaaaa", "bbbbbbbb"), "join");
}

/* ---- .tar.xz: archives built here, then compressed with liblzma ---- */

typedef struct { unsigned char *p; size_t n, cap; } Buf;

static void put(Buf *b, const void *d, size_t n) {
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, d, n); b->n += n;
}

static void tar_add(Buf *b, const char *name, char type, const char *data, size_t len) {
    unsigned char h[512] = { 0 };
    snprintf((char *)h, 100, "%s", name);
    snprintf((char *)h + 100, 8, "%07o", 0644);
    snprintf((char *)h + 124, 12, "%011o", (unsigned)len);
    memset(h + 148, ' ', 8);
    h[156] = (unsigned char)type;
    memcpy(h + 257, "ustar\0" "00", 8);
    unsigned sum = 0;
    for (int i = 0; i < 512; i++) sum += h[i];
    snprintf((char *)h + 148, 8, "%06o", sum);
    put(b, h, 512);
    put(b, data, len);
    static const unsigned char zero[512];
    put(b, zero, (512 - len % 512) % 512);
}

/* Writes `tar` as name, xz-compressed with an LZMA2 dictionary of dict bytes */
static bool write_xz(const char *name, const Buf *tar, uint32_t dict) {
    lzma_options_lzma opt;
    lzma_lzma_preset(&opt, 1);
    opt.dict_size = dict;
    lzma_filter f[2] = { { LZMA_FILTER_LZMA2, &opt }, { LZMA_VLI_UNKNOWN, NULL } };
    size_t cap = tar->n + 4096, n = 0;
    unsigned char *out = malloc(cap);
    bool ok = lzma_stream_buffer_encode(f, LZMA_CHECK_CRC64, NULL, tar->p, tar->n, out, &n, cap) == LZMA_OK &&
              fs_write_file(name, out, n);
    free(out);
    return ok;
}

static bool has(const char *path, const char *want) {
    char *d; size_t n;
    if (!fs_read_file(path, &d, &n, 1 << 20)) return false;
    bool ok = n == strlen(want) && !memcmp(d, want, n);
    free(d);
    return ok;
}

static void test_tarxz(void) {
    char err[128], longname[200];
    Buf b = { 0 };
    tar_add(&b, "pkg/", '5', "", 0);
    tar_add(&b, "pkg/app/boot.dol", '0', "DOL", 3);
    tar_add(&b, "./pkg/app/data/x.txt", '0', "x", 1);
    snprintf(longname, sizeof longname, "pkg/app/%0120d.txt", 7);
    tar_add(&b, "././@LongLink", 'L', longname, strlen(longname) + 1);
    tar_add(&b, "truncated-by-tar", '0', "long", 4);
    const char *pax = "29 path=pkg/app/pax-name.txt\n";
    tar_add(&b, "PaxHeaders/x", 'x', pax, strlen(pax));
    tar_add(&b, "pkg/app/short-name", '0', "pax", 3);
    tar_add(&b, "pkg/app/link", '2', "", 0);
    tar_add(&b, "pkg/app/empty", '0', "", 0);
    tar_add(&b, "pkg/other/big.iso", '0', "ISO", 3);
    tar_add(&b, "pkg/appendix.txt", '0', "no", 2);
    static const unsigned char end[1024];
    put(&b, end, sizeof end);
    CHECK(write_xz("a.tar.xz", &b, 1 << 20), "encode");
    size_t done = 0, total = 0;
    fs_remove_tree("out");
    CHECK(untarxz_to_dir("a.tar.xz", "out", (const char *[]){ "pkg/app", NULL }, NULL, &done, &total, err, sizeof err), "unpack: %s", err);
    CHECK(has("out/pkg/app/boot.dol", "DOL") && has("out/pkg/app/data/x.txt", "x"), "files written");
    char path[300];
    snprintf(path, sizeof path, "out/%s", longname);
    CHECK(has(path, "long"), "GNU long name");
    CHECK(has("out/pkg/app/pax-name.txt", "pax") && !fs_exists("out/pkg/app/short-name"), "pax path");
    CHECK(has("out/pkg/app/empty", ""), "empty file");
    CHECK(!fs_exists("out/pkg/app/link"), "link skipped");
    CHECK(!fs_exists("out/pkg/other") && !fs_exists("out/pkg/appendix.txt"), "only the root folder is written");
    fs_remove_tree("out");
    CHECK(untarxz_to_dir("a.tar.xz", "out", (const char *[]){ "pkg/app", "pkg/other/big.iso", NULL }, NULL, NULL, NULL,
                         err, sizeof err) && has("out/pkg/other/big.iso", "ISO") && !fs_exists("out/pkg/appendix.txt"),
          "a listed file outside the root folder: %s", err);
    CHECK(total > 0 && done == total, "progress %zu/%zu", done, total);
    fs_remove_tree("out");
    CHECK(untarxz_to_dir("a.tar.xz", "out", NULL, NULL, NULL, NULL, err, sizeof err) &&
          has("out/pkg/other/big.iso", "ISO"), "everything without a root: %s", err);

    /* a cut-off download */
    char *d; size_t n;
    fs_read_file("a.tar.xz", &d, &n, 1 << 20);
    fs_write_file("cut.tar.xz", d, n / 2);
    free(d);
    CHECK(!untarxz_to_dir("cut.tar.xz", "out2", NULL, NULL, NULL, NULL, err, sizeof err), "cut archive refused");

    Buf bad = { 0 };
    tar_add(&bad, "../evil", '0', "x", 1);
    put(&bad, end, sizeof end);
    write_xz("bad.tar.xz", &bad, 1 << 20);
    CHECK(!untarxz_to_dir("bad.tar.xz", "out3", NULL, NULL, NULL, NULL, err, sizeof err) && !fs_exists("evil"),
          "unsafe path refused");

    /* a dictionary too big for the console is refused up front, with a reason */
    write_xz("huge.tar.xz", &b, 64u << 20);
    CHECK(!untarxz_to_dir("huge.tar.xz", "out4", NULL, NULL, NULL, NULL, err, sizeof err) && strstr(err, "MB of memory"),
          "memory limit: %s", err);

    fs_write_file("junk.tar.xz", "PK\3\4 not xz", 11);
    CHECK(!untarxz_to_dir("junk.tar.xz", "out5", NULL, NULL, NULL, NULL, err, sizeof err), "not xz refused");
    free(b.p); free(bad.p);

    /* a catalog entry names a .tar.xz with "archive" */
    const char *idx = "{\"format\":1,\"apps\":[{\"slug\":\"swiss\",\"archive\":\"https://x.org/s.tar.xz\","
                      "\"root\":\"s/DOL\",\"binary\":\"s.dol\"}]}";
    App app; int na = 0;
    CHECK(catalog_parse_index(idx, strlen(idx), 0, &app, &na, 1, NULL, 0, err, sizeof err) && na == 1 &&
          !app.bare && !strcmp(app.url, "https://x.org/s.tar.xz"), "archive field: %s", err);
}

/* replace_at_root: a boot file is swapped and the old one archived */
static void test_root_replace(void) {
    char err[128], moved[300];
    const char *idx = "{\"format\":1,\"apps\":[{\"slug\":\"swiss\",\"zip\":\"https://x.org/s.zip\",\"replace_at_root\":["
                      "{\"file\":\"s/DOL/s.dol\",\"to\":\"ipl.dol\",\"archive\":\"swiss_archive\"},"
                      "{\"file\":\"s/X.zip!../evil\",\"to\":\"evil\"},"
                      "{\"file\":\"s/Apploader/X.zip!swiss/patches/apploader.img\",\"to\":\"swiss/patches/apploader.img\"},"
                      "{\"file\":\"x\",\"to\":\"apps/other/boot.dol\"}]}]}";
    App app; int na = 0;
    CHECK(catalog_parse_index(idx, strlen(idx), 0, &app, &na, 1, NULL, 0, err, sizeof err) && na == 1, "parse: %s", err);
    CHECK(app.nreplace == 2 && !strcmp(app.replace[0].to, "ipl.dol") && !strcmp(app.replace[0].archive, "swiss_archive") &&
          !strcmp(app.replace[1].from, "s/Apploader/X.zip!swiss/patches/apploader.img"),
          "root files (an unsafe inner path is dropped): %d", app.nreplace);
    CHECK(!strcmp(app.replace[1].archive, "swiss_archive"), "default archive is slug_archive: %s", app.replace[1].archive);
    CHECK(fs_path_wanted("a/b/c", (const char *[]){ "x", "a/b", NULL }) && fs_path_wanted("a/b", (const char *[]){ "a/b", NULL }) &&
          !fs_path_wanted("a/bc", (const char *[]){ "a/b", NULL }) && fs_path_wanted("q", NULL), "path_wanted");
    const char *bare = "{\"format\":1,\"apps\":[{\"slug\":\"b\",\"dol\":\"https://x.org/b.dol\",\"binary\":\"b.dol\","
                       "\"replace_at_root\":[{\"file\":\"other.dol\",\"to\":\"ipl.dol\"}]}]}";
    na = 0;
    CHECK(catalog_parse_index(bare, strlen(bare), 0, &app, &na, 1, NULL, 0, err, sizeof err) && na == 1 && app.nreplace == 0,
          "a bare .dol can only place its own binary");

    fs_mkdirs("rr");
    fs_write_file("rr/new.dol", "NEW", 3);
    fs_write_file("rr/ipl.dol", "OLD1", 4);
    CHECK(fs_files_equal("rr/new.dol", "rr/new.dol") && !fs_files_equal("rr/new.dol", "rr/ipl.dol") &&
          !fs_files_equal("rr/new.dol", "rr/missing"), "files_equal");
    CHECK(fs_replace_archiving("rr/new.dol", "rr/ipl.dol", "rr/arch/r1", moved, sizeof moved, err, sizeof err), "replace: %s", err);
    CHECK(has("rr/ipl.dol", "NEW") && has("rr/arch/r1/ipl.dol", "OLD1") && !strcmp(moved, "rr/arch/r1/ipl.dol"), "swapped");
    CHECK(!fs_exists("rr/ipl.dol.new"), "no leftovers");
    /* the same archive folder again: a second one beside it, nothing overwritten */
    fs_write_file("rr/ipl.dol", "OLD2", 4);
    CHECK(fs_replace_archiving("rr/new.dol", "rr/ipl.dol", "rr/arch/r1", moved, sizeof moved, err, sizeof err) &&
          has("rr/arch/r1/ipl.dol", "OLD1") && has("rr/arch/r1-2/ipl.dol", "OLD2"), "archive kept: %s", moved);
    /* a missing source changes nothing */
    fs_write_file("rr/ipl.dol", "OLD3", 4);
    CHECK(!fs_replace_archiving("rr/none.dol", "rr/ipl.dol", "rr/arch/r3", moved, sizeof moved, err, sizeof err) &&
          has("rr/ipl.dol", "OLD3") && !fs_exists("rr/arch/r3") && !fs_exists("rr/ipl.dol.new"), "failure leaves it alone");
}

#ifdef HAVE_LIBZIP
#include "zipx.h"
#include <zip.h>

/* zips are built with libzip; the console reads them with the same library */
static void test_zip(void) {
    char err[128];
    int ze;
    remove("inner.zip");
    zip_t *z = zip_open("inner.zip", ZIP_CREATE | ZIP_TRUNCATE, &ze);
    zip_file_add(z, "swiss/patches/apploader.img", zip_source_buffer(z, "IMG", 3, 0), 0);
    CHECK(z && zip_close(z) == 0, "write inner zip");
    CHECK(unzip_file("inner.zip", "swiss/patches/apploader.img", "img.out", err, sizeof err) && has("img.out", "IMG"),
          "member out of a zip: %s", err);
    CHECK(!unzip_file("inner.zip", "nope", "nope.out", err, sizeof err) && !fs_exists("nope.out"), "missing member refused");

    remove("outer.zip");
    z = zip_open("outer.zip", ZIP_CREATE | ZIP_TRUNCATE, &ze);
    zip_file_add(z, "s/DOL/s.dol", zip_source_buffer(z, "DOL", 3, 0), 0);
    zip_file_add(z, "s/Apploader/X.zip", zip_source_file(z, "inner.zip", 0, -1), 0);
    zip_file_add(z, "s/ISO/big.iso", zip_source_buffer(z, "ISO", 3, 0), 0);
    CHECK(zip_close(z) == 0, "write outer zip");
    fs_remove_tree("zout");
    CHECK(unpack_to_dir("outer.zip", "zout", (const char *[]){ "s/DOL", "s/Apploader/X.zip", NULL }, NULL, NULL, NULL,
                        err, sizeof err), "unpack zip: %s", err);
    CHECK(has("zout/s/DOL/s.dol", "DOL") && fs_exists("zout/s/Apploader/X.zip") && !fs_exists("zout/s/ISO"),
          "zip: root folder and listed file only");
}
#endif

static void test_log(void) {
    fs_mkdirs("logt");
    log_printf("before open: dropped");
    remove("logt/log.txt"); remove("logt/log.old.txt");
    log_open("logt/log.txt");
    log_printf("first %d", 1);
    log_printf("two\nlines");
    char *text; size_t len;
    CHECK(fs_read_file("logt/log.txt", &text, &len, 4096), "log written");
    int lines = 0;
    for (size_t i = 0; i < len; i++) lines += text[i] == '\n';
    CHECK(lines == 2, "one line per call, got %d", lines);
    CHECK(strstr(text, "] first 1\n") && strstr(text, "] two lines\n"), "text: %s", text);
    CHECK(!strstr(text, "dropped"), "nothing before log_open");
    free(text);
    /* a full log becomes log.old.txt, at open and while writing */
    char big[300];
    memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = 0;
    for (int i = 0; i < LOG_MAX_BYTES / 200; i++) log_printf("%s", big);
    CHECK(fs_exists("logt/log.old.txt"), "rotated while writing");
    CHECK(fs_file_size("logt/log.txt") < LOG_MAX_BYTES, "log stays under the cap");
    CHECK(fs_file_size("logt/log.old.txt") >= LOG_MAX_BYTES, "old log holds the full one");
    CHECK(mem_largest_free() > 0 && mem_largest_free() <= mem_free_total(), "memory figures");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    fs_mkdirs("sd:");   /* the host stand-in for the card root */
    test_json();
    test_sha();
    test_fs();
    test_catalog();
    test_tarxz();
    test_root_replace();
    test_log();
#ifdef HAVE_LIBZIP
    test_zip();
#else
    printf("(no libzip: zip checks skipped)\n");
#endif
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
