#!/bin/bash
# build_pc.sh - Build the Animal Crossing PC port
# Run from MSYS2 MINGW32 (32-bit i686) or UCRT64
#
# Prerequisites:
#   pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-SDL2
#
# Usage:
#   1. ./build_pc.sh
#   2. Place your disc image (.ciso/.iso/.gcm) in pc/build32/bin/rom/
#   3. pc/build32/bin/AnimalCrossing.exe

set -e

# MINGW32 is the 32-bit i686 shell (the save layout). UCRT64 is also accepted.
if [ "$MSYSTEM" != "MINGW32" ] && [ "$MSYSTEM" != "UCRT64" ]; then
    echo "Error: Must run from MSYS2 MINGW32 (not MINGW64 or MSYS) or UCRT64."
    echo "Open 'MSYS2 MINGW32' from your Start menu for the 32-bit build, then run this script again."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/pc/build32"
BIN_DIR="$BUILD_DIR/bin"

# --- CMake configure ---
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ ! -f Makefile ]; then
    echo "=== Configuring CMake ==="
    cmake .. -G "MinGW Makefiles"
fi

# --- Build ---
echo "=== Building PC port ==="
mingw32-make -j$(nproc)

# --- Create runtime directories ---
mkdir -p "$BIN_DIR/rom"
mkdir -p "$BIN_DIR/texture_pack"
mkdir -p "$BIN_DIR/save"

echo ""
echo "=== Build complete! ==="
echo ""
echo "Place your Animal Crossing disc image (.ciso/.iso/.gcm) in:"
echo "  pc/build32/bin/rom/"
echo ""
echo "Run: pc/build32/bin/AnimalCrossing.exe"
