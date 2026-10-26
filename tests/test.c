/* test.c - exercises the portable modules on the host. Runs inside tests/run
 * with a folder literally named "sd:" standing in for the card. */
#include "catalog.h"
#include "digest.h"
#include "fsutil.h"
#include "json.h"
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

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    fs_mkdirs("sd:");   /* the host stand-in for the card root */
    test_json();
    test_sha();
    test_fs();
    test_catalog();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
