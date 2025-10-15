#!/usr/bin/env bash
set -euo pipefail

# Build xneur locally using relative paths and bundle matching shared libs.
# This script DOES NOT install packages; it only checks for them.
#
# Usage:
#   scripts/build-local.sh        # minimal features
#   scripts/build-local.sh --full # optional features (sound/notify/spell)
#
# Output:
#   out/xneur, out/libxneur.so.20, out/libxnconfig.so.20

FULL=0
if [[ ${1:-} == "--full" ]]; then FULL=1; fi

# 0) Preflight: tools and packages
need_tool() { command -v "$1" >/dev/null 2>&1 || { echo "Missing tool: $1"; MISSING=1; }; }
need_pkg()  { dpkg -s "$1" >/dev/null 2>&1 || { echo "Missing package: $1"; MISSING=1; }; }

MISSING=0
need_tool cmake
need_tool make
need_tool pkg-config

# Minimal features
for p in build-essential gettext libx11-dev libxext-dev libxi-dev libxtst-dev libpcre3-dev zlib1g-dev; do
  need_pkg "$p"
done

# Optional features
if [[ $FULL -eq 1 ]]; then
  for p in libgstreamer1.0-dev libnotify-dev libenchant-dev; do need_pkg "$p"; done
fi

if [[ $MISSING -eq 1 ]]; then
  echo "\nSome tools/packages are missing. Please install the above and re-run." >&2
  exit 2
fi

# 1) Configure (relative paths). Always use a fresh local build dir to avoid cache mismatch.
SRC_DIR="xneur"
BUILD_DIR="build-local"
OUT_DIR="out"
rm -rf "$BUILD_DIR"

if [[ $FULL -eq 1 ]]; then
  cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo
else
  cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DSOUNDS=OFF -DSPELL=OFF -DNOTIFICATIONS=OFF -DPLUGINS=OFF
fi

# 2) Build
cmake --build "$BUILD_DIR" -j"$(nproc)"

# 3) Bundle matched libs and set RPATH if patchelf is available; otherwise create a wrapper.
mkdir -p "$OUT_DIR"
install -m 0755 "$BUILD_DIR/src/xneur" "$OUT_DIR/xneur"
install -m 0644 "$BUILD_DIR/lib/lib/libxneur.so.20" "$OUT_DIR/libxneur.so.20"
install -m 0644 "$BUILD_DIR/lib/config/libxnconfig.so.20" "$OUT_DIR/libxnconfig.so.20"

if command -v patchelf >/dev/null 2>&1; then
  patchelf --set-rpath '$ORIGIN' "$OUT_DIR/xneur"
else
  cat > "$OUT_DIR/run.sh" <<'SH'
#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$DIR/xneur" "$@"
SH
  chmod +x "$OUT_DIR/run.sh"
  echo "Note: patchelf not found. Use ./out/run.sh to run with bundled libs." >&2
fi

md5sum "$OUT_DIR/xneur" | awk '{print "MD5:",$1}'
echo "Built files in ./out"
echo "Run: ./out/xneur  (or ./out/run.sh if patchelf is missing)"
