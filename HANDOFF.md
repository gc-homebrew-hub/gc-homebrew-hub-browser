# Handoff: GC Homebrew Hub Browser

Last updated 2026-10-25 (names chosen: GC Homebrew Hub = the catalog, GC Homebrew Hub Browser = the program). Read this before continuing the work; README.md
covers what the program is and how to build it.

## The goal

A Wii-Homebrew-Browser-style app store for the GameCube: browse catalogs of
homebrew from one or more sources, install, update, remove and start apps on
the SD card without pulling the card, with the default sources and apps
maintained through pull requests to a catalog repository. The look on `main` is
the GameCube boot menu (a floating glass cube); on `swiss-ui` it is the Swiss
"System" theme. `main` draws text with the console's own IPL font; `swiss-ui`
bundles Inter, baked at exact pixel sizes (see README.md, "The font").

Controls on the cube menu (`main`) were one direction per section; the
`swiss-ui` branch replaced them with a Swiss-style bar of sections (see "The
Swiss look" below). Y on Installed still filters to apps with an update, and
confirming again installs it.

## The three repositories and their status

| Repo | Where | Remote | Status |
|---|---|---|---|
| gc-homebrew-hub-browser (this one) | `~/Documents/GitHub/gc-homebrew-hub-browser` | private, `michaellunzer/gc-homebrew-hub-browser` | the program; pushing here is fine |
| gc-homebrew-hub (the catalog, formerly cubebrowser-apps) | `~/Documents/GitHub/gc-homebrew-hub` | none yet | local only; the default source URL expects it at `michaellunzer/gc-homebrew-hub`; do not create or push it without being asked |
| CubeBrowser (web browser) | `~/Documents/GitHub/Cubebrowser`, worktree branch `claude/cubebrowser-homebrew-apps-9f7d9c` | origin is upstream `Arial-Bold/Cubebrowser`, never pushed | contains the first, in-browser version of the store (`about:apps`). The owner decided the two should be separate and to leave that branch as it is for now. Never push, fork or open anything upstream from it. |

History: the store was first built inside CubeBrowser (commits a5af82d,
f5cd0e7, a3b3309, b8c210e on that branch, with a host test harness under
`tests/hbstore`). On 2026-10-20 the owner asked for a standalone program
written from scratch with no browser code, which is this repo. The catalog
format is the only thing shared.

## What is done (this repo, commit 7070a52)

- Complete program, about 4,800 lines of C plus `dol_stub.S`, builds
  warning-free in the libogc2 Docker image to a 2.1 MB `boot.dol`.
- Rendering: GX setup, 2D primitives, a CPU-lit cube, text from the IPL font
  (`SYS_InitFont` / `SYS_GetFontTexture`), 640x480 logical coordinates.
- Main menu with the cube, four tiles, hover tilt, roll-in transition, update
  badge; Sources, Installed (with the Y updates filter and confirm), App store
  (category strip), Settings, app details page, on-screen keyboard, toasts,
  confirm dialog.
- Worker thread (`store.c`): network start (DHCP), CA bundle load, source
  list fetch, index fetch per source with on-card cache, install (download to
  `dl/`, size and SHA-256 check, unzip with libzip or place a bare `.dol`,
  `keep` files, atomic folder swap via `.old`, `copy_to_root` once, stamp file
  `.gc-homebrew-app`), remove, messages back to the UI.
- `net.c` (written by a subagent, reviewed only by compile and its report):
  HTTP/1.1 client with chunked transfer, redirects, HTTPS through mbedTLS
  3.6.2 (`hb_mbedtls_config.h`), its own DNS resolver (the libogc2 GameCube
  stack exports neither `net_gethostbyname` nor `net_poll`, although
  `network.h` declares them), SNTP for the clock, `net_select`-based
  timeouts.
- DOL launcher: reads the file, moves the image to the top of RAM, shuts the
  OS down, a position-independent stub at 0x80001800 copies sections, zeroes
  bss, flushes caches and jumps; sets devkitPPC `argv[0]` by filling the
  six-word block at entry+8 when it is all zero in the file.
- Host tests (`make test`): 59 checks over JSON, SHA-256, catalog parsing and
  persistence, version compare, card scan, file helpers. All pass.
- `make package` assembles `package/apps/gc-homebrew-hub-browser/` (boot.dol +
  cacert.pem); `data/cacert.pem` is the curl CA bundle.
- GitHub Actions (`.github/workflows/build.yml`) builds every push in the
  libogc2 image, fails on warnings in `source/`, runs `make test` and
  attaches `package/` as an artifact. A `vX.Y` tag that matches `VERSION`
  in the Makefile also publishes a GitHub Release with the zip.

## The Swiss look (branch `swiss-ui`, 2026-10-30)

The owner found the GameCube-menu look (glass cube, neon accents; still on
`main`, commit 8b036ac) ugly and hard to read, and asked for a complete
revamp after the Swiss "System" theme in
[Arial-Bold/swiss-gc-system-UI](https://github.com/Arial-Bold/swiss-gc-system-UI),
keeping nothing of the old style. That repository asks LLM tools not to work
on it (its AGENTS.md); nothing was copied from it. Only its look was matched,
re-implemented here from scratch.

- Backdrop (`ui_backdrop`): a dark blue-black picture tube with a cool glow,
  scanlines and a tube-shaped dark edge (per-vertex coloured meshes through
  `gfx_strip`).
- Pieces (`ui.c`): `ui_panel` (light outer line, dark inner line, teal-black
  fill), `ui_row` (frosted blue-grey list row), `ui_button` (grey or gold
  glass), `ui_ring` (the gold selection ring, with a glow on buttons),
  `ui_desc_bar` (indigo strip with a fading grid), `ui_track`/`ui_gold_fill`
  (progress and scroll bars). Text is plain white/grey, no drop shadow.
- Frame (`menu.c`): the title bar (name, version, network, clock or job
  progress) and the bar of five sections (Hub, Installed, Sources, Settings,
  Exit) along the bottom. The section on screen is the gold button; B moves
  into the bar, where the gold ring shows the choice.
- Sections (`screens.c`): a side pane (heading, facts, button hints) and six
  frosted rows with an initials plate, the selection ringed in gold, and the
  description bar under them. Installed is a list now (no block grid).
- Navigation changed with it: the program opens on the Hub; the old
  direction-per-section cube controls are gone.
- The 3D cube renderer was removed from `video.c`.
- Checked with `make preview` only; the devkitPPC build was NOT run for this
  branch (Docker was not running): build it first and fix any warnings.
- Preview fixes for macOS: `stub/gctypes.h` was empty, a `stub/malloc.h`,
  `wait` renamed (clashes with libc).
- Text is Inter, not the IPL font: `tools/font/bake_font.py` bakes 13/15/18/22
  px into `source/font_data.c` as IA4 textures, drawn 1:1 with GX_NEAR. Text is
  about half again as large as before, the list shows 5 rows (was 6), the side
  pane is wider and the dim text colours are brighter. The preview now reads
  the same textures in their tiled GX layout (the stand-in font is gone).

## What has NOT been verified

Nothing has run on hardware or in an emulator (Dolphin is not installed on
this Mac; it cannot emulate the SD card anyway, but would show the UI). In
order of risk:

1. GX rendering and the text (`video.c`, `font.c`): first thing to look at
   on the console. If the screen stays black, suspect `video_init`; if there
   is no text or it is garbled, the IA4 texture setup in `font_init` (the
   tile layout in `bake_font.py` is what the preview reads, not hardware). On
   PAL (528-line EFB) the 1:1 drawing is stretched a little vertically. The glass cube draws text and shapes through a perspective
   projection (face decals): if those are missing or misplaced on hardware
   but the cube itself shows, look at `face_space` in `video.c`. Also check
   the frame rate on the Installed grid (18 small cubes).
2. TLS on the console: entropy comes from the time base; certificate dates
   depend on SNTP or the console clock. Failures show as toasts such as
   "TLS handshake failed (-0x....)" (mbedTLS error strings are compiled out).
3. DNS: resolver asks the gateway, then 1.1.1.1 and 8.8.8.8 over UDP 53.
4. The DOL stub and `SYS_ResetSystem(SYS_SHUTDOWN)` sequence (`dol.c`).
5. libfat `rename` of directories during the folder swap.

## How to test on the GameCube

1. `./get-deps.sh` once, then
   `docker run --rm -v "$PWD:/src" -w /src ghcr.io/extremscorner/libogc2:latest make`
   and `make package`; copy `package/apps` to the root of the card.
2. Start the catalog server on the Mac from the catalog repo:
   `~/Documents/GitHub/gc-homebrew-hub/scripts/serve.sh` (port 8765; it
   prints the LAN address). The catalog has CubeBrowser, Flashcube (zip with a
   root data folder) and GC-MP3 (bare .dol release).
3. The default source list URL (`settings.ini` key `sources_url`, default in
   `settings.c`) points at `raw.githubusercontent.com/michaellunzer/gc-homebrew-hub`,
   which does not exist yet, so expect "Source list not loaded".
   Add the LAN source on the console: Sources, X, type
   `http://<mac-ip>:8765/index.json`.
4. Then: refresh, install Flashcube and GC-MP3, check `sd:/apps/<folder>`,
   `installed.txt` and the stamp file, start an app, bump a version in the
   catalog and confirm the Installed screen's Y filter offers the update,
   remove an app.

## Next steps (suggested order)

1. Hardware test and fix what it finds.
2. Publish the catalog repo as `michaellunzer/gc-homebrew-hub` (the default
   source URL in `settings.c` already expects that; raw.githubusercontent.com
   needs the repo to be public, or the URL must point at a server you run).
3. Phase 2 ideas from the original plan: app icons in the catalog (JSON
   field + download + texture LRU), search, a sound on menu moves, argv for
   extra arguments, a quiet start-up update count shown on the cube screen
   (the badge already shows it once catalogs are loaded).
4. Possibly strip `about:apps` from the CubeBrowser branch (the owner said
   to leave it for now).

## Decisions and gotchas worth knowing

- Data folder on the card is `sd:/apps/gc-homebrew-hub-browser/`; apps go to
  `sd:/apps/<folder>/` so Swiss sees them. The program refuses catalog
  entries whose folder is `gc-homebrew-hub-browser`.
- Unmanaged folders in `sd:/apps` (not installed by this program) are listed
  under Installed with start and remove only; a folder matching a catalog
  app's folder is offered Reinstall.
- `copy_to_root` files are copied only when missing, never overwritten, and
  left in place on removal (user data such as Flashcube's thumbnails).
- Version compare: dotted numbers compare numerically; anything else counts
  as an update when the strings differ.
- A source file must not be called `sha256.c`: mbedTLS has one and both land
  in the same build folder (hence `digest.c`).
- The Makefile's DEVKITPPC and mbedTLS checks are skipped for the `test`,
  `package` and `clean` goals so they run on the host.
- Worker thread priority is 40 (below the main thread's 64) so the UI keeps
  animating during unzip and hashing; stack 160 KB because mbedTLS X.509
  verification is stack-hungry.
- Catalog format 1 field reference: `scripts/validate.py` and README in the
  catalog repo. Fields the store understands: slug, name, author, version,
  category (games/emulators/media/utilities/demos), short, long, homepage,
  zip or dol, zip_size, sha256, root, binary, folder, keep[], copy_to_root[],
  updated. Limits: 256 apps per source, index at most 256 KB, 16 sources.
- Memory for the next session: see
  `~/.claude/projects/-Users-michael-lunzer-Documents-GitHub-Cubebrowser/memory/`.
