#!/usr/bin/env bash
# Per-boot runtime prep for Cloud Agents. Does not launch the GUI game
# (agents start it on demand). Ensures runtime directories exist and, when
# present, copies a disc image into the expected rom/ folder.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/pc/build32/bin"
mkdir -p "$BIN/rom" "$BIN/save" "$BIN/texture_pack"

if [[ -n "${ACGC_ROM_PATH:-}" && -f "${ACGC_ROM_PATH}" ]]; then
  cp -f "${ACGC_ROM_PATH}" "$BIN/rom/"
elif [[ -n "${ACGC_ROM:-}" && -f "${ACGC_ROM}" ]]; then
  cp -f "${ACGC_ROM}" "$BIN/rom/"
else
  shopt -s nullglob
  root_roms=("$ROOT"/*.{ciso,iso,gcm,CISO,ISO,GCM})
  if (( ${#root_roms[@]} > 0 )); then
    existing=("$BIN/rom"/*.{ciso,iso,gcm,CISO,ISO,GCM})
    if (( ${#existing[@]} == 0 )); then
      cp -f "${root_roms[0]}" "$BIN/rom/"
    fi
  fi
  shopt -u nullglob
fi

if [[ -x "$BIN/AnimalCrossing" || -x "$BIN/AnimalCrossing.exe" ]]; then
  echo "[start] binary present under $BIN"
else
  echo "[start] binary missing; run install first" >&2
fi
