#!/usr/bin/env bash
# Windows (MSYS2 MINGW32) setup for the Animal Crossing PC port.
# Prerequisites: MSYS2 installed, then from this script or a MINGW32 shell:
#   pacman -S --noconfirm mingw-w64-i686-gcc mingw-w64-i686-cmake \
#     mingw-w64-i686-SDL2 mingw-w64-i686-ninja
set -euo pipefail

export PATH="/mingw32/bin:/usr/bin:${PATH:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ "$(uname -m)" == "x86_64" ]] && ! gcc -dumpmachine 2>/dev/null | grep -q i686; then
  echo "Error: use the MSYS2 MINGW32 environment (i686-w64-mingw32)." >&2
  echo "Open mingw32.exe or set MSYSTEM=MINGW32 with PATH=/mingw32/bin:/usr/bin" >&2
  exit 1
fi

echo "=== Toolchain ==="
which gcc cmake ninja
gcc -dumpmachine

echo "=== Configure ==="
cmake -S pc -B pc/build32 -G Ninja

echo "=== Build ==="
cmake --build pc/build32 -j"$(nproc)"

mkdir -p pc/build32/bin/rom pc/build32/bin/texture_pack pc/build32/bin/save

shopt -s nullglob
roms=(pc/build32/bin/rom/*.{ciso,iso,gcm,CISO,ISO,GCM})
if (( ${#roms[@]} == 0 )); then
  root_roms=(*.{ciso,iso,gcm,CISO,ISO,GCM})
  if (( ${#root_roms[@]} > 0 )); then
    cp "${root_roms[0]}" pc/build32/bin/rom/
  fi
fi
shopt -u nullglob

echo "=== Smoke: --help ==="
./pc/build32/bin/AnimalCrossing.exe --help

echo ""
echo "Run: pc/build32/bin/AnimalCrossing.exe --verbose"
echo "Disc image path: pc/build32/bin/rom/"
