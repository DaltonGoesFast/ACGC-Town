#!/usr/bin/env bash
# Cloud Agent / Linux bootstrap for the Animal Crossing PC port.
# Requires a 32-bit toolchain (JSystem casts pointers to u32).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

install_linux_deps() {
  if ! need_cmd apt-get; then
    echo "apt-get not found; install gcc-multilib, cmake, ninja, and libsdl2-dev:i386 yourself"
    return 0
  fi

  if ! dpkg --print-foreign-architectures 2>/dev/null | grep -qx i386; then
    sudo dpkg --add-architecture i386
  fi

  sudo apt-get update -y
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    gcc-multilib \
    g++-multilib \
    cmake \
    ninja-build \
    pkg-config \
    git \
    ca-certificates \
    libsdl2-dev:i386 \
    libgl1-mesa-dev:i386 \
    libx11-dev:i386 \
    libxext-dev:i386 \
    libxrandr-dev:i386 \
    libxcursor-dev:i386 \
    libxi-dev:i386 \
    libxss-dev:i386 \
    libasound2-dev:i386 \
    libpulse-dev:i386 \
    mesa-utils \
    xvfb
}

# Idempotent dependency install for Linux Cloud Agents
if [[ "$(uname -s)" == "Linux" ]]; then
  if ! need_cmd i686-linux-gnu-gcc && ! (need_cmd gcc && gcc -m32 -print-file-name=libgcc.a >/dev/null 2>&1); then
    install_linux_deps
  elif ! pkg-config --exists sdl2 2>/dev/null && ! pkg-config --exists sdl2 --print-errors 2>/dev/null; then
    # Prefer refreshing deps when SDL2 i386 pkg-config is missing
    if ! ls /usr/lib/i386-linux-gnu/libSDL2.so* >/dev/null 2>&1 \
      && ! ls /usr/lib/i686-linux-gnu/libSDL2.so* >/dev/null 2>&1; then
      install_linux_deps
    fi
  fi
fi

BUILD_DIR="$ROOT/pc/build32"
mkdir -p "$BUILD_DIR"

CMAKE_ARGS=(-S "$ROOT/pc" -B "$BUILD_DIR" -G Ninja)
if [[ "$(uname -s)" == "Linux" ]]; then
  if need_cmd i686-linux-gnu-gcc && need_cmd i686-linux-gnu-g++; then
    CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$ROOT/pc/cmake/Toolchain-linux32.cmake")
  else
    CMAKE_ARGS+=(-DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32)
  fi
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "$BUILD_DIR/bin/rom" "$BUILD_DIR/bin/save" "$BUILD_DIR/bin/texture_pack"

# Optional: Cloud Agent / env secret pointing at a disc image
if [[ -n "${ACGC_ROM_PATH:-}" && -f "${ACGC_ROM_PATH}" ]]; then
  cp -f "${ACGC_ROM_PATH}" "$BUILD_DIR/bin/rom/"
elif [[ -n "${ACGC_ROM:-}" && -f "${ACGC_ROM}" ]]; then
  cp -f "${ACGC_ROM}" "$BUILD_DIR/bin/rom/"
fi

# Smoke check that does not require a disc image
"$BUILD_DIR/bin/AnimalCrossing" --help >/dev/null

echo "[install] PC port ready at $BUILD_DIR/bin/AnimalCrossing"
