#!/usr/bin/env bash
set -euo pipefail

# Build xneur locally and bundle matching shared libs to avoid ABI issues.
# Usage:
#   scripts/build-local.sh [--full]
#
# Options:
#   --full  Build with optional features (sound, notifications, spell)
#
# Output:
#   out/xneur, out/libxneur.so.20, out/libxnconfig.so.20

FULL=0
if [[ ${1:-} == "--full" ]]; then
  FULL=1
fi

sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config gettext \
  libx11-dev libxext-dev libxi-dev libxtst-dev libpcre3-dev zlib1g-dev

if [[ $FULL -eq 1 ]]; then
  sudo apt-get install -y libgstreamer1.0-dev libnotify-dev libenchant-dev
  cmake -S xneur -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
else
  cmake -S xneur -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DSOUNDS=OFF -DSPELL=OFF -DNOTIFICATIONS=OFF -DPLUGINS=OFF
fi

cmake --build build -j"$(nproc)"

mkdir -p out
install -m 0755 build/src/xneur out/xneur
install -m 0644 build/lib/lib/libxneur.so.20 out/libxneur.so.20
install -m 0644 build/lib/config/libxnconfig.so.20 out/libxnconfig.so.20

if ! command -v patchelf >/dev/null 2>&1; then
  sudo apt-get install -y patchelf
fi
patchelf --set-rpath '$ORIGIN' out/xneur

md5sum out/xneur | awk '{print "MD5:",$1}'
echo "Built files in ./out"
echo "Run: ./out/xneur"
