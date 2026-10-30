#!/bin/sh
# Runs a .dol in Dolphin (the Flathub build) with no window and keeps a few
# frames as PNGs. Used by CI; works on any Linux with flatpak.
#   tools/dolphin/run.sh boot.dol out-dir [seconds]
# Dolphin has no GameCube SD adapter, so the program sees no card. The
# Broadband Adapter is emulated (built-in, no TAP device needed).
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
timeout "$secs" flatpak run --filesystem="$(dirname "$dol")" --filesystem="$user" \
    --command=dolphin-emu-nogui $app -p headless -u "$user" -e "$dol" > "$out/dolphin.log" 2>&1
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
