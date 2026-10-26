#!/bin/sh
# mksdimg.sh - builds data/emusd/sdcard.img, the card image the EMU_SD=1 build
# bakes into the DOL (see source/emusd.c). Dolphin has no SD card device, so
# this is the only way to reach the parts of the program that need a card.
# The image holds a plausible card: two sources, a saved copy of a catalog,
# two installed apps (both with an update waiting) and one folder this
# program did not install. Needs mkfs.vfat (dosfstools) and mtools.
set -eu

cd "$(dirname "$0")/.."
OUT=data/emusd/sdcard.img
DATA=apps/gc-homebrew-hub-browser
SIZE_MB=8
HUB_URL=https://raw.githubusercontent.com/michaellunzer/gc-homebrew-hub/main/index.json
LOCAL_URL=${LOCAL_URL:-http://127.0.0.1:8765/index.json}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# cache/<fnv1a32 of the url>.json, the name catalog_cache_path() builds
hashname() {
    python3 -c 'import sys
h = 2166136261
for b in sys.argv[1].encode(): h = ((h ^ b) * 16777619) & 0xffffffff
print("%08x" % h)' "$1"
}

mkdir -p "$work/$DATA/cache" "$work/apps/flashcube" "$work/apps/gc-mp3" "$work/apps/swiss"

printf '%s\t%s\tb\n' "$HUB_URL" "GC Homebrew Hub"   >  "$work/$DATA/sources.txt"
printf '%s\t%s\tu\n' "$LOCAL_URL" "Local test server" >> "$work/$DATA/sources.txt"

# installed.txt plus the per-folder stamp, as an install leaves them
printf 'flashcube\t1.0\tflashcube\tboot.dol\tFlashcube\n'  >  "$work/$DATA/installed.txt"
printf 'gc-mp3\t0.9\tgc-mp3\tboot.dol\tGC-MP3\n'           >> "$work/$DATA/installed.txt"
printf 'flashcube\t1.0\tFlashcube\n' > "$work/apps/flashcube/.gc-homebrew-app"
printf 'gc-mp3\t0.9\tGC-MP3\n'       > "$work/apps/gc-mp3/.gc-homebrew-app"

# a .dol has to exist in each folder or the card scan skips it
for d in flashcube gc-mp3 swiss; do
    head -c 4096 /dev/zero > "$work/apps/$d/boot.dol"
done

cat > "$work/$DATA/cache/$(hashname "$HUB_URL").json" <<'JSON'
{
  "format": 1,
  "name": "GC Homebrew Hub",
  "updated": 1761350400,
  "apps": [
    { "slug": "cubebrowser", "name": "CubeBrowser", "author": "Arial-Bold", "version": "0.3",
      "category": "utilities", "short": "A web browser for the GameCube",
      "long": "Renders pages over the Broadband Adapter. Needs a keyboard in serial port 1 for typing addresses.",
      "homepage": "https://github.com/Arial-Bold/Cubebrowser",
      "zip": "https://example.invalid/cubebrowser-0.3.zip", "zip_size": 524288,
      "folder": "cubebrowser", "binary": "boot.dol", "updated": 1761264000 },
    { "slug": "flashcube", "name": "Flashcube", "author": "ccrroossss", "version": "1.2",
      "category": "media", "short": "Photo viewer for the SD card",
      "long": "Shows JPEG and PNG files from sd:/flashcube with the D-pad. Thumbnails are cached next to the pictures.",
      "zip": "https://example.invalid/flashcube-1.2.zip", "zip_size": 262144,
      "folder": "flashcube", "binary": "boot.dol", "root": "flashcube",
      "copy_to_root": ["flashcube"], "keep": ["settings.ini"], "updated": 1761177600 },
    { "slug": "gc-mp3", "name": "GC-MP3", "author": "emukidid", "version": "1.1",
      "category": "media", "short": "MP3 player",
      "long": "Plays MP3 files from the card through the console's own DSP.",
      "dol": "https://example.invalid/gc-mp3-1.1.dol", "zip_size": 196608,
      "folder": "gc-mp3", "binary": "boot.dol", "updated": 1760918400 },
    { "slug": "gcmm", "name": "GCMM", "author": "suloku", "version": "1.5.1",
      "category": "utilities", "short": "Memory card manager",
      "long": "Backs up and restores memory card saves to the SD card.",
      "zip": "https://example.invalid/gcmm-1.5.1.zip", "zip_size": 327680,
      "folder": "gcmm", "binary": "boot.dol", "updated": 1760572800 },
    { "slug": "not64", "name": "Not64", "author": "emukidid", "version": "6.2",
      "category": "emulators", "short": "Nintendo 64 emulator",
      "long": "A fork of Wii64 for the GameCube. Needs the full 24 MB of memory, so start it from Swiss.",
      "zip": "https://example.invalid/not64-6.2.zip", "zip_size": 1048576,
      "folder": "not64", "binary": "boot.dol", "updated": 1760313600 },
    { "slug": "quake-gc", "name": "Quake GC", "author": "Izhido", "version": "0.9",
      "category": "games", "short": "Quake, ported to the GameCube",
      "long": "Needs the shareware pak0.pak in sd:/quake/id1.",
      "zip": "https://example.invalid/quake-gc-0.9.zip", "zip_size": 786432,
      "folder": "quake-gc", "binary": "boot.dol", "copy_to_root": ["quake"], "updated": 1759968000 },
    { "slug": "gxdemo", "name": "GX Demo", "author": "shagkur", "version": "1.0",
      "category": "demos", "short": "The libogc GX examples",
      "long": "Spinning cubes and textured quads, the first thing most people build.",
      "zip": "https://example.invalid/gxdemo-1.0.zip", "zip_size": 65536,
      "folder": "gxdemo", "binary": "boot.dol", "updated": 1759708800 },
    { "slug": "sd-remote", "name": "SD Remote", "author": "novenary", "version": "2.0",
      "category": "utilities", "short": "Serves the card over the network",
      "long": "Mounts the SD card on a PC over FTP while the console stays on.",
      "zip": "https://example.invalid/sd-remote-2.0.zip", "zip_size": 147456,
      "folder": "sd-remote", "binary": "boot.dol", "updated": 1759363200 }
  ]
}
JSON

cp data/cacert.pem "$work/$DATA/cacert.pem"

mkdir -p data/emusd
rm -f "$OUT"
truncate -s "${SIZE_MB}M" "$OUT"
mkfs.vfat -F 16 -s 1 -n GCHUBSD "$OUT" >/dev/null

export MTOOLS_SKIP_CHECK=1
mmd -i "$OUT" ::/apps ::/apps/gc-homebrew-hub-browser ::/apps/gc-homebrew-hub-browser/cache \
              ::/apps/flashcube ::/apps/gc-mp3 ::/apps/swiss
for f in $(cd "$work" && find . -type f | sed 's|^\./||'); do
    mcopy -i "$OUT" "$work/$f" "::/$f"
done

echo "$OUT ready (${SIZE_MB} MB):"
mdir -i "$OUT" -/ ::/apps | sed 's/^/  /'
