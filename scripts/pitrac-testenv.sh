#!/bin/bash
# Manage an isolated Raspberry Pi OS Trixie environment for testing PiTrac installs.
# Uses systemd-nspawn with a real Pi OS rootfs — does not touch the host system.
#
# Usage:
#   pitrac-testenv.sh create    - Download Pi OS image and create environment
#   pitrac-testenv.sh enter     - Shell into the environment
#   pitrac-testenv.sh snapshot  - Save current state as a restore point
#   pitrac-testenv.sh reset     - Restore to last snapshot
#   pitrac-testenv.sh destroy   - Delete everything
#   pitrac-testenv.sh status    - Show environment info

set -euo pipefail

MACHINE_DIR="/var/lib/machines"
ENV_NAME="pitrac-test"
ENV_PATH="$MACHINE_DIR/$ENV_NAME"
SNAP_PATH="$MACHINE_DIR/${ENV_NAME}-snapshot"
IMG_CACHE="/var/cache/pitrac-testenv"

PIOS_URL="https://downloads.raspberrypi.com/raspios_lite_arm64/images/raspios_lite_arm64-2025-12-04/2025-12-04-raspios-trixie-arm64-lite.img.xz"
PIOS_FILENAME="2025-12-04-raspios-trixie-arm64-lite.img.xz"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

die() { echo -e "${RED}Error:${NC} $*" >&2; exit 1; }
info() { echo -e "${GREEN}>>>${NC} $*"; }
warn() { echo -e "${YELLOW}>>>${NC} $*"; }

need_root() {
    [[ $EUID -eq 0 ]] || die "Run with sudo"
}

cmd_create() {
    need_root
    [[ -d "$ENV_PATH" ]] && die "Environment already exists. Run 'destroy' first or 'enter' to use it."

    # Check dependencies
    for cmd in wget xz losetup rsync; do
        command -v "$cmd" &>/dev/null || die "Missing required command: $cmd (apt install $cmd)"
    done

    mkdir -p "$IMG_CACHE"
    local img_xz="$IMG_CACHE/$PIOS_FILENAME"
    local img="${img_xz%.xz}"

    # Download if not cached
    if [[ ! -f "$img_xz" && ! -f "$img" ]]; then
        info "Downloading Raspberry Pi OS Trixie Lite arm64 (~500MB)..."
        wget -O "$img_xz" "$PIOS_URL"
    else
        info "Using cached image"
    fi

    # Decompress if needed
    if [[ -f "$img_xz" ]]; then
        info "Decompressing image..."
        xz -dk "$img_xz"
    fi

    # Mount rootfs partition from the image
    info "Extracting rootfs from Pi OS image..."
    local loop_dev
    loop_dev=$(losetup -fP --show "$img")

    local rootfs_part="${loop_dev}p2"
    local boot_part="${loop_dev}p1"

    [[ -b "$rootfs_part" ]] || die "Could not find rootfs partition at $rootfs_part"

    local mnt="/mnt/pitrac-img-$$"
    mkdir -p "$mnt"
    mount "$rootfs_part" "$mnt"

    # Copy rootfs
    info "Copying rootfs to $ENV_PATH (this takes a minute)..."
    mkdir -p "$ENV_PATH"
    rsync -a "$mnt/" "$ENV_PATH/"

    # Copy boot/firmware if present
    if [[ -b "$boot_part" ]]; then
        mkdir -p "$ENV_PATH/boot/firmware"
        mount "$boot_part" "$mnt/boot/firmware" 2>/dev/null || mount "$boot_part" "$ENV_PATH/boot/firmware"
        if mountpoint -q "$mnt/boot/firmware"; then
            rsync -a "$mnt/boot/firmware/" "$ENV_PATH/boot/firmware/"
            umount "$mnt/boot/firmware"
        elif mountpoint -q "$ENV_PATH/boot/firmware"; then
            umount "$ENV_PATH/boot/firmware"
        fi
    fi

    # Clean up mounts and loop device
    umount "$mnt"
    losetup -d "$loop_dev"
    rmdir "$mnt"

    # Clean up decompressed image (keep the .xz for future use)
    rm -f "$img"

    # Fix ld.so.preload if it exists (legacy Pi OS issue)
    if [[ -f "$ENV_PATH/etc/ld.so.preload" ]]; then
        info "Disabling ld.so.preload for container compatibility"
        sed -i 's/^/#/' "$ENV_PATH/etc/ld.so.preload"
    fi

    # Set hostname
    echo "pitrac-test" > "$ENV_PATH/etc/hostname"

    # Allow root login without password for convenience
    chroot "$ENV_PATH" passwd -d root 2>/dev/null || true

    # Copy host DNS config so apt works
    cp /etc/resolv.conf "$ENV_PATH/etc/resolv.conf" 2>/dev/null || true

    # Add PiTrac APT repo
    info "Adding PiTrac APT repository..."
    mkdir -p "$ENV_PATH/usr/share/keyrings"
    curl -fsSL https://pitraclm.github.io/packages/pitrac-repo.asc \
        | gpg --dearmor -o "$ENV_PATH/usr/share/keyrings/pitrac-archive-keyring.gpg" 2>/dev/null || warn "Could not fetch GPG key"

    echo "deb [arch=arm64 signed-by=/usr/share/keyrings/pitrac-archive-keyring.gpg] https://pitraclm.github.io/packages trixie main" \
        > "$ENV_PATH/etc/apt/sources.list.d/pitrac.list"

    info "Raspberry Pi OS Trixie environment created at $ENV_PATH"
    info "Run 'sudo $0 snapshot' to save this clean state"
    info "Run 'sudo $0 enter' to get a shell"
}

cmd_enter() {
    need_root
    [[ -d "$ENV_PATH" ]] || die "No environment found. Run 'create' first."

    info "Entering test environment (type 'exit' to leave)..."
    info "Host services are NOT affected."

    # Ensure DNS works inside the container
    cp /etc/resolv.conf "$ENV_PATH/etc/resolv.conf" 2>/dev/null || true

    local bind_args=()
    for dev in /dev/video0 /dev/video1 /dev/media0 /dev/media1 /dev/media2 /dev/media3 /dev/gpiochip0 /dev/gpiochip4; do
        [[ -e "$dev" ]] && bind_args+=(--bind="$dev")
    done
    for ro in /proc/device-tree /sys/firmware/devicetree; do
        [[ -e "$ro" ]] && bind_args+=(--bind-ro="$ro")
    done

    systemd-nspawn \
        --directory="$ENV_PATH" \
        "${bind_args[@]}" \
        --capability=all \
        --machine="$ENV_NAME" \
        /bin/bash
}

cmd_snapshot() {
    need_root
    [[ -d "$ENV_PATH" ]] || die "No environment to snapshot."

    info "Saving snapshot..."
    rm -rf "$SNAP_PATH"
    cp -a "$ENV_PATH" "$SNAP_PATH"
    local size=$(du -sh "$SNAP_PATH" | cut -f1)
    info "Snapshot saved ($size)"
}

cmd_reset() {
    need_root
    [[ -d "$SNAP_PATH" ]] || die "No snapshot found. Run 'snapshot' first."

    info "Resetting to snapshot..."
    rm -rf "$ENV_PATH"
    cp -a "$SNAP_PATH" "$ENV_PATH"
    info "Reset complete"
}

cmd_destroy() {
    need_root
    warn "This deletes the test environment and snapshot."
    read -p "Continue? [y/N] " confirm
    [[ "$confirm" =~ ^[Yy]$ ]] || exit 0

    rm -rf "$ENV_PATH" "$SNAP_PATH"
    info "Destroyed"
}

cmd_status() {
    echo "Test environment: $ENV_PATH"
    if [[ -d "$ENV_PATH" ]]; then
        local size=$(du -sh "$ENV_PATH" | cut -f1)
        echo "  Status: exists ($size)"
    else
        echo "  Status: not created"
    fi

    echo "Snapshot: $SNAP_PATH"
    if [[ -d "$SNAP_PATH" ]]; then
        local size=$(du -sh "$SNAP_PATH" | cut -f1)
        echo "  Status: exists ($size)"
    else
        echo "  Status: none"
    fi

    echo "Image cache: $IMG_CACHE"
    if [[ -d "$IMG_CACHE" ]]; then
        local size=$(du -sh "$IMG_CACHE" | cut -f1)
        echo "  Status: exists ($size)"
    else
        echo "  Status: empty"
    fi

    echo ""
    echo "Workflow:"
    echo "  sudo $0 create     # download Pi OS + one-time setup"
    echo "  sudo $0 snapshot   # save clean state"
    echo "  sudo $0 enter      # get a shell, install stuff, test"
    echo "  sudo $0 reset      # wipe and restore to snapshot"
}

case "${1:-}" in
    create)   cmd_create ;;
    enter)    cmd_enter ;;
    snapshot) cmd_snapshot ;;
    reset)    cmd_reset ;;
    destroy)  cmd_destroy ;;
    status)   cmd_status ;;
    *)
        echo "Usage: sudo $0 {create|enter|snapshot|reset|destroy|status}"
        exit 1
        ;;
esac
