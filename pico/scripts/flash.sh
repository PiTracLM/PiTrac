#!/usr/bin/env bash
#
# flash.sh — flash the latest build to a Pi Pico.
#
# Two modes, auto-detected:
#
#   1. picotool present (preferred): uses `picotool load -fx` so you don't
#      have to put the Pico into BOOTSEL mode manually. -f forces; -x exits
#      bootloader after flash so the new firmware starts immediately.
#
#   2. picotool missing: looks for the RPI-RP2 mass-storage mount and copies
#      the .uf2 there. You'll need to hold BOOTSEL while plugging the Pico
#      in for this to work.
#
# Run from the project root:
#   ./scripts/flash.sh
#   ./scripts/flash.sh path/to/custom.uf2

set -euo pipefail

# Resolve project root (one level up from scripts/)
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Default to the production pico_w UF2 staged by build-all.sh. Pass a path to
# flash a different board variant (e.g. firmware/pitrac_pico.uf2).
UF2="${1:-${PROJECT_ROOT}/firmware/pitrac_pico_w.uf2}"

if [[ ! -f "$UF2" ]]; then
  echo "error: $UF2 not found. Build first:"
  echo "    cd $PROJECT_ROOT && bash scripts/build-all.sh"
  exit 1
fi

echo "flashing: $UF2"

if command -v picotool &>/dev/null; then
  # picotool can reset a connected Pico into bootloader mode itself, so we
  # don't need the user to hold BOOTSEL on a reflash. -f forces overwrite,
  # -x leaves bootloader after flash so the firmware boots immediately.
  echo "using picotool"
  picotool load -fx "$UF2"
  exit 0
fi

# Fallback: look for the RPI-RP2 mass-storage mount and copy. Works on
# Linux and macOS without any additional tooling.
echo "picotool not found — falling back to drag-drop mode"
echo "  hold BOOTSEL while plugging the Pico in, then re-run this script"

MOUNT=""
if [[ "$(uname -s)" == "Darwin" ]]; then
  MOUNT="/Volumes/RPI-RP2"
elif [[ "$(uname -s)" == "Linux" ]]; then
  # Common udisks2 mount paths
  for candidate in "/media/$USER/RPI-RP2" "/run/media/$USER/RPI-RP2" /mnt/RPI-RP2; do
    if [[ -d "$candidate" ]]; then
      MOUNT="$candidate"
      break
    fi
  done
fi

if [[ -z "$MOUNT" || ! -d "$MOUNT" ]]; then
  echo "error: RPI-RP2 not mounted. Hold BOOTSEL while plugging in."
  echo "       (or install picotool: https://github.com/raspberrypi/picotool)"
  exit 1
fi

cp -v "$UF2" "$MOUNT/"
sync
echo "done — Pico should reboot into the new firmware shortly"
