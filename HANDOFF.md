# Handoff: GC Homebrew Hub Browser

Last updated 2026-10-25 (names chosen: GC Homebrew Hub = the catalog, GC Homebrew Hub Browser = the program). Read this before continuing the work; README.md
covers what the program is and how to build it.

## The goal

A Wii-Homebrew-Browser-style app store for the GameCube: browse catalogs of
homebrew from one or more sources, install, update, remove and start apps on
the SD card without pulling the card, with the default sources and apps
maintained through pull requests to a catalog repository. The look is the
GameCube boot menu: a floating cube in the middle, the four sections around
it, the console's own IPL font.

Controls the owner asked for: up = Sources (add sources), down = Installed
(Y filters to apps with an update, confirming again installs it), right = App
store, left = Settings.

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

## The GameCube-menu look (2026-10-26)

The owner asked for the console's own menu aesthetic. What that became:

- Main menu: a see-through glass cube (`gfx_glass_cube` in `video.c`): far
  faces darker, a solid core spinning inside, near faces with a gloss and
  decals drawn on them with the normal 2D calls (`CubeFaceFn`, face units
  0..100), glowing edges and corner pixels. Sections sit on faces: top =
  Sources, bottom = Installed, left = Settings, right = Homebrew Hub (the store), front
  and back = "GC / HOMEBREW / BROWSER". The D-pad turns that face three-quarters on,
  the Control Stick leans the cube live, the C-stick spins it and it springs
  back, opening flies square into the face (18 frames) and coming back zooms
  out of it. Indigo background with a floor grid and drifting sparks.
- Sections: a dark grid with pulsing "pixel" crossings and a scan line, each
  section in its own neon accent (Sources blue, Installed green, Homebrew Hub
  violet, Settings orange; `C_SOURCES` etc. in `ui.h`, `ui_set_accent`).
  Panels, rows, tags, toasts, dialogs and keyboard are glossy translucent
  glass (`ui_glass`). Footers draw the controller's buttons (green A, red B,
  grey X/Y, yellow C-stick, ...) from the hint text (`ui_hints`).
- Installed is a 6x3 grid of little glass cubes like the memory card screen:
  initials on the front with a shine sweeping over them, a colour per app
  (hash of the slug), an arrow on top and a pulsing pixel for an update, free
  blocks drawn empty, an info strip for the selected app. The store list and
  the details page use the same app blocks/cube.
- The cube is painted back to front (convex, so no depth buffer and no
  culling); facing is computed on the CPU, so it does not depend on GX's
  winding convention.
- `make preview` (tools/preview) renders all of this on the host through a
  software GX; that is how it was checked. The devkitPPC build was NOT run
  for these changes (the libogc2 image could not be pulled where they were
  written): build it first and fix any warnings.

## What the Dolphin run on 2026-10-26 showed

First time the program has run anywhere. Dolphin (built from source, no SD
card device, Serial Port 1 set to the built-in Broadband Adapter) with the
`EMU_SD=1` build described in README.md. What worked:

- GX set-up, the 2D primitives, the lit cube and the IPL font
  (`SYS_InitFont`): every screen draws. A stock build without `EMU_SD=1`
  reaches "No SD card found" and draws it correctly.
- The card: libfat mounts the image, and the cube menu, Sources, Installed
  (with the Y updates filter and the confirm dialog), App store, the category
  strip, the details page, the on-screen keyboard, Settings, toasts and the
  update badge all work off it.
- Network: DHCP, DNS, HTTP and HTTPS. TLS was exercised both ways: without a
  CA bundle the fetch returns an HTTP status, and with `cacert.pem` on the
  card the handshake is rejected ("Certificate not trusted") when the
  certificate does not verify, and Settings says "servers verified".
- A whole install over the network from a catalog served on the LAN: the GET,
  the size and SHA-256 checks, unzip, the `root` folder, the folder swap, the
  stamp file and `installed.txt`. The app then shows as "Up to date" under
  Installed and the details page offers Start, Reinstall and Remove.
- Error paths: an unreachable host gives "Download failed: Host not found",
  a source that fails keeps serving its saved copy.

What it found:

1. A source whose refresh failed but that has a saved copy is drawn as a
   success: `store.c` sets `napps = -1` and then overwrites it with the count
   from the cache, so the Sources screen takes the `napps > 0` branch and
   paints "Failed: Certificate not trusted" in `C_GOOD` green
   (`screens.c:263-264`). The second source, with no cache, is red as it
   should be.
2. "1 apps listed" in the toast after a reload: no singular.
3. Text has a thin sliver of the next glyph on its left edge, clearest on
   `I`, `i` and `n` ("iInsert a card", "an SD adapteriin slot A or B"). Looks
   like the glyph's left texture coordinate in `font.c` reaching into its
   neighbour; a half-texel inset would be the usual fix.
4. A 537 KB download never finished, while a 4 KB one installed in seconds.
   The host had sent the whole body and closed (the connection sat in
   TIME_WAIT with empty queues) while the console still said "Downloading"
   with an empty bar, and no timeout fired in about four minutes although
   `READ_TIMEOUT_MS` is 20 s. Dolphin logged "Partial sends might not be
   handled properly" 13 times, so the emulated adapter is the first suspect,
   but it is worth checking on hardware that a stalled body read does time
   out, and that the bar moves while bytes arrive.

Not covered by this run: the DOL launcher (`dol.c`) and
`SYS_ResetSystem(SYS_SHUTDOWN)` — the app that would have been started is the
537 KB download above, so Start was never pressed; real SD2SP2/slot A/B
hardware; libfat `rename` of directories on a real card (the image in memory
does exercise the code path); `copy_to_root` with an existing folder.

## What the second Dolphin run on 2026-10-27 showed

Same setup, on this branch merged with main, so this covers the GameCube-menu
look above. The devkitPPC build the look was never compiled with: it builds
warning-free, `boot.dol` is 1,062,752 bytes. `make test` 59 checks, 0
failures. `make preview` renders its 17 screens.

On the console side, the new look draws correctly on real GX: the glass cube
with its face decals (so `face_space` is right), the section tiles with their
icons, the neon section backgrounds, the memory-card grid of app cubes under
Installed, the app cube on the details page, and the controller button glyphs
in the footers. Catalogs loaded over the emulated adapter again and a 4 KB
and a 100 KB app both installed.

Findings 1, 2 and 3 above all still reproduce. Two of them now have a line
to change:

- Finding 1 also affects the new status LED: `screens.c:338` picks its colour
  from the same `napps`, so a source that failed but has a saved copy gets a
  green light as well as a green pill.
- Finding 3's cause is in hand: the font sheets are sampled with `GX_LINEAR`
  (`font.c:29`) and `gfx_tex_quad` maps the cell's texel coordinates straight
  to UVs with no half-texel inset, so every glyph blends in the column of the
  one to its left. Insetting by half a texel, or `GX_NEAR` for the sheets,
  should settle it. It is clearest on the X button glyph in every footer,
  which reads `'X`.
- Finding 4 needs correcting: the program does recover. A 542 KB download
  failed and the details page came back with its Install button, so the read
  timeout does fire; yesterday's "no timeout in four minutes" was the
  emulator, not the program. The stall is Dolphin's adapter: while it hung,
  the server socket sat in FIN_WAIT1 with 203 KB still queued and Dolphin's
  own socket was ESTABLISHED with 88 KB unread, i.e. the bytes never reached
  the guest. 100 KB is fine, 542 KB is not.

New, and the most serious thing found so far:

5. The network is brought up at start whatever the settings say, and nothing
   bounds it. `store_init` submits `JOB_START` unconditionally
   (`store.c:330-335`); "Load the catalogs when starting" is only consulted
   afterwards, to decide whether to refresh (`store.c:349`); and `net_start`
   calls libogc2's `if_config`, which takes no timeout and no retry count.
   With no adapter the worker never comes back: it was still "Connecting to
   the network" after three and a half minutes. While it runs, the details
   page draws the progress row in place of Start/Reinstall/Remove and ignores
   A, so an installed app cannot be started at all. Most GameCubes have no
   Broadband Adapter, so this is worth settling before hardware testing:
   either start the network only when something needs it, or run it so the
   buttons stay live while it tries.
6. An app listed in `installed.txt` whose folder is gone is still listed, with
   its version and path, and offered for starting. `catalog_scan_card` drops
   only the unmanaged entries before rescanning, so a folder deleted on a PC
   leaves a phantom app behind.

`dol.c` is still unverified. Dolphin ends the session the moment Start is
pressed: twice in a row, once with a host-side "double free or corruption"
and once with a fatal X error after the render window went away. The program
does get as far as shutting the OS down, which is what `dol.c` does right
before the jump, but whether the stub's copy-and-jump lands cannot be seen
here. A few-KB test program is ready for a card (`tools/` has no copy of it;
it is three dozen lines against libogc2 that paint the screen and print
`argv[0]`), which is the quickest way to settle it on hardware.

## What has NOT been verified on hardware

The Dolphin runs above cover the drawing, the card and the network as far as
an emulator can. In order of risk, what is left:

1. The DOL stub and `SYS_ResetSystem(SYS_SHUTDOWN)` sequence (`dol.c`).
   Untouched by the emulator run and the only large piece still unexercised.
2. The SD card itself: `__io_gcsd2` / `__io_gcsda` / `__io_gcsdb` and libfat
   `rename` of directories during the folder swap. The run used a card in
   memory, so everything above the driver has been through its paces, and the
   driver has not.
3. A download of real size over a real adapter, after the stall in finding 4.
4. TLS on the console: entropy comes from the time base and certificate dates
   depend on SNTP or the console clock, both of which Dolphin makes easy.
   Failures show as toasts such as "TLS handshake failed (-0x....)" (mbedTLS
   error strings are compiled out).
5. GX and the IPL font on a real video encoder: both draw correctly in
   Dolphin, so a black screen on the console points at `video_init`, and
   missing text at `SYS_InitFont` (returns 1 on success).
6. The glass cube's face decals: they draw through a perspective projection,
   so if the cube shows but the drawing on its faces is missing or in the
   wrong place, look at `face_space` in `video.c`. Worth watching the frame
   rate on the Installed grid (18 small cubes) too.

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

1. Hardware test and fix what it finds; start with `dol.c` and the card
   driver, and with the four findings from the Dolphin run above.
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
