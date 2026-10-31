#!/bin/sh
# Runs a .dol in Dolphin (the Flathub build, which has no dolphin-emu-nogui)
# on a virtual X display and keeps a few frames as PNGs. Used by CI; works on
# any Linux with flatpak and xvfb-run.
#   tools/dolphin/run.sh boot.dol out-dir [seconds]
# Dolphin has no GameCube SD adapter, so the program sees no card. The
# Broadband Adapter is emulated (built-in, no TAP device needed). Its DNS
# server defaults to 3.18.217.27, a game-revival server that answers every
# name with its own address and speaks only old SSL, so every HTTPS request
# fails with -0x6E80 (bad protocol version); use a public resolver instead.
set -eu
dol=$(realpath "$1"); out=$(realpath -m "$2"); secs=${3:-30}
app=org.DolphinEmu.dolphin-emu
user=$(realpath -m "${DOLPHIN_USER_DIR:-dolphin-user}")
rm -rf "$user"; mkdir -p "$user/Config" "$out"

# EXIDeviceType (Source/Core/Core/HW/EXI/EXI_Device.h): 12 = EthernetBuiltIn, 255 = None
cat > "$user/Config/Dolphin.ini" <<EOF
[Core]
GFXBackend = Vulkan
SlotA = 255
SlotB = 255
SerialPort1 = 12
BBA_BUILTIN_DNS = 1.1.1.1
[DSP]
Backend = No Audio Output
[Movie]
DumpFrames = True
DumpFramesSilent = True
[Analytics]
PermissionAsked = True
Enabled = False
EOF
cat > "$user/Config/GFX.ini" <<EOF
[Settings]
DumpFramesAsImages = True
EOF

set +e
timeout "$secs" xvfb-run -a -s "-screen 0 1280x1024x24" \
    flatpak run --filesystem="$(dirname "$dol")" --filesystem="$user" \
    $app -b -u "$user" -e "$dol" > "$out/dolphin.log" 2>&1
status=$?
set -e
cat "$out/dolphin.log"
echo "dolphin exited with $status (124 = still running at the time limit, as expected)"

frames=$(find "$user/Dump/Frames" -name '*.png' 2>/dev/null | sort -V)
n=$(printf '%s\n' "$frames" | grep -c . || true)
echo "$n frames dumped"
if [ "$n" -gt 0 ]; then
    # a few from across the run, and the last one
    for pct in 10 25 50 75; do
        f=$(printf '%s\n' "$frames" | sed -n "$(( n * pct / 100 + 1 ))p")
        cp "$f" "$out/frame-${pct}pct.png"
    done
    cp "$(printf '%s\n' "$frames" | tail -n 1)" "$out/last.png"
fi

if grep -qiE "invalid (read|write) from|crash|exception" "$out/dolphin.log"; then
    echo "::error::Dolphin reported a crash; see dolphin.log"
    exit 1
fi
[ "$n" -gt 0 ] || { echo "::error::Dolphin produced no frames"; exit 1; }
