#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PICO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK_PATH="${PICO_SDK_PATH:-$HOME/pico-sdk}"

if [[ ! -f "$SDK_PATH/pico_sdk_init.cmake" ]]; then
    echo "pico-sdk not found at $SDK_PATH (set PICO_SDK_PATH)" >&2
    exit 1
fi

cd "$PICO_DIR"
mkdir -p firmware

for board in pico pico_w pico2 pico2_w; do
    build_dir="build-${board}"
    echo "==> ${board}"
    PICO_SDK_PATH="$SDK_PATH" cmake -B "$build_dir" -DPICO_BOARD="$board" \
        > "$build_dir.configure.log" 2>&1 || { tail -20 "$build_dir.configure.log"; exit 1; }
    PICO_SDK_PATH="$SDK_PATH" cmake --build "$build_dir" -j 4 \
        > "$build_dir.build.log" 2>&1 || { tail -30 "$build_dir.build.log"; exit 1; }
    cp "$build_dir/pitrac_pico.uf2" "firmware/pitrac_${board}.uf2"
    rm -f "$build_dir.configure.log" "$build_dir.build.log"
    size=$(stat -f%z "firmware/pitrac_${board}.uf2" 2>/dev/null || stat -c%s "firmware/pitrac_${board}.uf2")
    echo "    firmware/pitrac_${board}.uf2 (${size} bytes)"
done

echo "done"
