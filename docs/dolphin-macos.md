# Running in Dolphin on a Mac

This guide covers running GC Homebrew Hub Browser in the
[Dolphin](https://dolphin-emu.org) emulator on macOS. It covers getting
`boot.dol`, installing Dolphin, setting up the controller and the emulated
Broadband Adapter, starting the program, and the SD card (SD2SP2) question.

> **Read this first: Dolphin cannot emulate an SD2SP2.**
> Dolphin (checked against its `master` branch, October 2026) has no SD card
> adapter of any kind for GameCube games: not the SD2SP2 in serial port 2
> and not an SD adapter in memory card slot A or B. Its Serial Port 2 is
> hard-wired to nothing, and Slot A/B only offer a memory card, a GCI folder,
> USB Gecko, the Advance Game Port, the microphone or nothing. The SD card
> options under **Config > Wii** are for Wii software only and the GameCube
> side does not see them.
>
> This program needs a card before it shows anything else. So in today's
> Dolphin it stops at **"No SD card found"**. The [SD2SP2 section](#4-the-sd2sp2-sd-card)
> explains how to check whether your Dolphin build has an SD adapter, what
> the setup would be if it does, and what you can test without one.

## 1. Get `boot.dol`

Either:

- **Download it.** Open the latest run under the repository's **Actions** tab
  (or a pull request's **Checks**), download the
  `gc-homebrew-hub-browser-<commit>` artifact and unzip it. You get
  `apps/gc-homebrew-hub-browser/boot.dol` and `cacert.pem`.
- **Build it on the Mac** with [Docker Desktop](https://www.docker.com/products/docker-desktop/):

  ```bash
  ./get-deps.sh
  docker run --rm -v "$PWD:/src" -w /src ghcr.io/extremscorner/libogc2:latest make
  make package
  ```

  On Apple Silicon, if Docker says the image has no `linux/arm64` variant,
  add `--platform linux/amd64` after `--rm`. The image then runs under
  Rosetta, which is slower but works. The result is `boot.dol` in the
  repository root and the card folder in `package/apps/`.

## 2. Install Dolphin

1. Download the latest **Release** (or **Development**) build for macOS from
   <https://dolphin-emu.org/download/>. It is a universal build for Intel and
   Apple Silicon Macs. With Homebrew installed you can instead run
   `brew install --cask dolphin`.
2. Open the `.dmg` and drag **Dolphin** into **Applications**.
3. On first launch, if macOS refuses to open it, go to **System Settings >
   Privacy & Security** and click **Open Anyway**. Alternatively, Control-click
   the app and choose **Open**.
4. Optional: in **Graphics**, choose the **Metal** backend. It is the default
   on recent builds.

Dolphin keeps its settings in `~/Library/Application Support/Dolphin/`
(`Config/Dolphin.ini`, `Config/GCPadNew.ini`, `GC/` for memory cards and IPL
files).

### The console font

The program draws its text with the console's own IPL font (`SYS_InitFont`).
Without a BIOS dump, Dolphin supplies its own replacement font, so the text
appears but looks slightly different from a real GameCube. For the real font,
copy an IPL dump from your own console to
`~/Library/Application Support/Dolphin/GC/<USA|EUR|JAP>/IPL.bin`. This is
optional. Leave **Config > GameCube > Skip Main Menu** on either way, because
`boot.dol` boots directly.

## 3. Set up Dolphin for this program

### Controller

**Controllers > GameCube Controllers > Port 1: Standard Controller >
Configure**. Pick a connected gamepad under **Device**, or use the keyboard.
The default keyboard mapping is usually:

| GameCube | Key |
|---|---|
| A / B / X / Y | X / Z / C / S |
| Start | Return |
| D-pad | T / G / F / H (up / down / left / right) |
| Control Stick | arrow keys |
| C-stick | I / K / J / L |

Check the dialog, because the defaults can differ between versions. The
program uses the D-pad, A, B, X, Y, Start, the Control Stick and the C-stick.

### Network: the emulated Broadband Adapter

The program needs a Broadband Adapter with DHCP. On macOS, use Dolphin's
built-in (HLE) adapter. It needs no TAP driver, no tapserver and no admin
rights, and it routes traffic through the Mac's own network connection.

1. **Config > GameCube > Device Settings > SP1:** choose
   **Broadband Adapter (HLE)**.
2. Click the **…** button next to it and set the DNS server to `8.8.8.8` or
   `1.1.1.1`. Dolphin answers DHCP itself, hands out its virtual router as
   the gateway, and forwards DNS questions sent to that router to the server
   you enter here. The program asks the gateway first, so this setting
   decides how host names resolve.
3. Leave **Slot A** and **Slot B** as they are. See the next section about SD.

The other SP1 choices (`tapserver`, `XLink Kai`, `TAP`) also work on a Mac
but need extra software. HLE is the simplest.

To use a catalog server on the same Mac (the catalog repository's
`scripts/serve.sh`, port 8765), add it as a source using the Mac's LAN
address, which you can get with `ipconfig getifaddr en0`. For example,
`http://192.168.1.20:8765/index.json`. Do not use `127.0.0.1` or
`localhost`, because inside the emulated console those name the console
itself. If macOS asks whether Python or Dolphin may accept incoming
connections, allow it.

### Start the program

**File > Open…** (⌘O), choose `boot.dol`. You can also drag `boot.dol` onto
the Dolphin window. To make it show in the game list, put it in a folder you
add under **Config > Paths**.

## 4. The SD2SP2 (SD card)

### How it works on a real console

On hardware the program mounts the card as `sd:`. It tries the SD2SP2 in
serial port 2 first, then an SD adapter in slot A, then slot B
(`mount_card` in `source/main.c`). It expects a FAT32 card that contains:

```
sd:/apps/gc-homebrew-hub-browser/boot.dol
sd:/apps/gc-homebrew-hub-browser/cacert.pem
```

### In Dolphin today

Dolphin has no device the program can mount, so all three attempts fail and
the screen says **"No SD card found — Insert a card (SD2SP2, or an SD
adapter in slot A or B)"**. The program tries again every two seconds and
whenever you press A. Every other screen (cube menu, Sources, Installed,
Homebrew Hub, Settings) is behind this one.

What this run still tests in Dolphin:

- that `boot.dol` loads and starts;
- video setup, the background, the cube and the IPL font (`video.c`,
  `font.c`);
- controller input (A retries the mount).

What does not work there: anything that needs the card, which is every
section, the network and installing.

### Check whether your Dolphin build has an SD adapter

Dolphin's developers have discussed GameCube SD adapter emulation (SD Gecko,
the official SD adapter and SD2SP2), but it had not been merged as of this
writing. To check your build:

1. Open **Config > GameCube > Device Settings**.
2. Open the **Slot A** and **Slot B** drop-down lists and look for an SD
   entry, such as "SD Adapter" or "SD Gecko".
3. Look for an **SP2** row below **SP1**.

If none of these exist, which is the case in current builds, your build
cannot emulate an SD card for GameCube software.

### If your build has an SD adapter

If a future or forked build adds one, the setup follows from how the program
looks for a card:

1. **Prepare a card image.** Create a raw FAT32 disk image with an MBR, like
   a real card. A 512 MB image is enough:

   ```bash
   dd if=/dev/zero of=gcsd.raw bs=1m count=512
   hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount gcsd.raw
   # it prints the device, e.g. /dev/disk4; use that below
   diskutil eraseDisk FAT32 GCSD MBRFormat /dev/disk4
   cp -R package/apps /Volumes/GCSD/
   hdiutil detach /dev/disk4
   ```

   Use whatever file name and format the build asks for. If it can sync a
   folder instead of using an image, as the Wii SD option can, point it at a
   folder that contains `apps/gc-homebrew-hub-browser/`.
2. **Choose the slot.** Pick **SP2: SD Adapter** for an SD2SP2. Otherwise
   pick the SD adapter in **Slot B** and keep the memory card in Slot A. All
   three work because the program tries SP2, then A, then B.
3. **Point it at the image** with the slot's **…** button, then start
   `boot.dol` again.
4. **Reload the image after a session.** Close Dolphin, or stop emulation,
   before mounting the image on the Mac again. Otherwise the two sides can
   overwrite each other's changes.

The program then behaves as it does on hardware. It creates
`settings.ini`, `sources.txt`, `installed.txt` and `cache/` next to
`boot.dol`, and installs apps to `sd:/apps/<folder>/`.

### Testing the card-dependent parts without Dolphin

- `make test` runs the catalog, JSON, SHA-256 and file code on the Mac with
  the normal C compiler.
- `make preview` renders every screen to `tools/preview/out/*.png` with a
  made-up card and catalog (see README.md; needs Python 3 with Pillow).
- Use a real GameCube with an SD2SP2 for everything else. The test plan is
  in HANDOFF.md.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Black screen, nothing at all | Try another graphics backend (Vulkan instead of Metal). Turn on **View > Show Log** and **View > Show Log Configuration**, and enable the OSReport log types. |
| Cube but no text | Font loading failed. Check that `Contents/Resources/Sys/GC/font_western.bin` exists inside `Dolphin.app`, or add an IPL dump (see "The console font"). |
| "No SD card found" | Expected in current Dolphin; see section 4. |
| "Network start failed" or DHCP timeout (with an SD build) | SP1 is not set to **Broadband Adapter (HLE)**, or emulation was started before you changed it. Stop emulation and start it again. |
| Host names don't resolve | Set the HLE adapter's DNS server to `8.8.8.8` (SP1 **…** button). |
| Catalog on the Mac not reachable | Use the Mac's LAN IP, not `localhost`. Allow incoming connections in the macOS firewall. |
