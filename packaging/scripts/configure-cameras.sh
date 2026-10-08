#!/usr/bin/env bash

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
log_success() { echo -e "${GREEN}[✓]${NC} $*"; }

get_config_txt_path() {
    if [[ -f "/boot/firmware/config.txt" ]]; then
        echo "/boot/firmware/config.txt"
    elif [[ -f "/boot/config.txt" ]]; then
        echo "/boot/config.txt"
    else
        log_error "Could not find config.txt in /boot or /boot/firmware"
        return 1
    fi
}

BLOCK_START="# PiTrac Camera Configuration"
BLOCK_END="# End PiTrac Camera Configuration"

backup_config_txt() {
    local config_path="$1"
    local backup_dir
    backup_dir="$(dirname "$config_path")/.pitrac_backups"

    mkdir -p "$backup_dir"
    cp "$config_path" "$backup_dir/config.txt.$(date +%Y%m%d_%H%M%S)"
    log_info "Backed up ${config_path} to ${backup_dir}"

    printf "%s\n" "$backup_dir"/config.txt.* | sort -r | tail -n +6 | xargs -r rm -f
}

configure_boot_config() {
    local num_cameras="$1"
    local config_path

    config_path=$(get_config_txt_path) || return 1

    if grep -q "^${BLOCK_START}" "$config_path" && ! grep -q "^${BLOCK_END}" "$config_path"; then
        log_error "${config_path} has the PiTrac block start marker but no end marker"
        log_error "Fix or remove the PiTrac block by hand and rerun. ${config_path} was not changed."
        return 1
    fi

    # A run that misses a camera must not strip overlays a previous run wrote:
    # with camera_auto_detect=0 nothing else would bring them back.
    local previous_cameras
    previous_cameras=$(sed -n "/^${BLOCK_START}/,/^${BLOCK_END}/p" "$config_path" | grep -c '^dtoverlay=imx296') || true
    if [[ "$num_cameras" -lt "$previous_cameras" ]]; then
        log_warn "Detected ${num_cameras} camera(s) but config.txt has overlays for ${previous_cameras}, keeping ${previous_cameras}"
        num_cameras=$previous_cameras
    fi

    log_info "Configuring ${config_path} for ${num_cameras} camera(s)..."

    # The OS ships camera_auto_detect=1, which double-loads the camera overlays
    # alongside our explicit ones, so existing lines are forced to 0.
    local stripped
    stripped=$(mktemp)
    # Drop the old block plus the blank line written after it, so a rerun
    # reproduces the file byte for byte.
    awk -v start="^${BLOCK_START}" -v end="^${BLOCK_END}" '
        $0 ~ start { in_block = 1 }
        in_block { if ($0 ~ end) { in_block = 0; drop_blank = 1 } next }
        drop_blank && $0 == "" { drop_blank = 0; next }
        { drop_blank = 0; print }
    ' "$config_path" |
        sed -e '/# Added by PiTrac installer/d' \
            -e 's/^camera_auto_detect=.*/camera_auto_detect=0/' >"$stripped"

    local config_block="${BLOCK_START} - Added by pitrac installer
# DO NOT MODIFY - Managed automatically by PiTrac"

    if ! grep -q "^camera_auto_detect=" "$stripped"; then
        config_block="$config_block

# Disable automatic camera detection for manual control
camera_auto_detect=0"
    fi

    config_block="$config_block

# Core system parameters for PiTrac operation"

    if ! grep -q "^dtparam=spi=on" "$stripped"; then
        config_block="$config_block
dtparam=spi=on"
    fi

    # dtoverlay=spi1-2cs is needed for V3 connector board calibration (SPI1 DAC/ADC)
    if ! grep -q "^dtoverlay=spi1-2cs" "$stripped"; then
        config_block="$config_block
dtoverlay=spi1-2cs"
    fi

    if ! grep -q "^force_turbo=" "$stripped"; then
        config_block="$config_block
force_turbo=1"
    fi

    if ! grep -q "^arm_boost=" "$stripped"; then
        config_block="$config_block
arm_boost=1"
    fi

    # always-on keeps the 1.8V rail powered so external triggering works and the
    # trigger mode can be switched at runtime without a reboot. InnoMaker IMX296
    # boards use the same stock imx296 overlay.
    if [[ "$num_cameras" -eq 2 ]]; then
        config_block="$config_block

[all]
dtoverlay=imx296,always-on,cam0
dtoverlay=imx296,always-on,cam1"
    elif [[ "$num_cameras" -eq 1 ]]; then
        config_block="$config_block

[all]
dtoverlay=imx296,always-on,cam0"
    fi

    config_block="$config_block

${BLOCK_END}"

    local merged
    merged=$(mktemp)
    local inserted=false
    local line_count=0

    while IFS= read -r line; do
        line_count=$((line_count + 1))

        if [[ "$inserted" == "false" ]]; then
            if [[ "$line" =~ ^\[.*\]$ ]] ||
                { [[ "$line_count" -gt 10 ]] && [[ ! "$line" =~ ^# ]] && [[ -n "$line" ]]; }; then
                printf '%s\n\n' "$config_block" >>"$merged"
                inserted=true
            fi
        fi

        echo "$line" >>"$merged"
    done <"$stripped"

    if [[ "$inserted" == "false" ]]; then
        printf '%s\n\n' "$config_block" >>"$merged"
    fi

    rm -f "$stripped"

    if cmp -s "$merged" "$config_path"; then
        rm -f "$merged"
        log_success "${config_path} already up to date"
        return 0
    fi

    backup_config_txt "$config_path"
    cp "$merged" "$config_path"
    rm -f "$merged"
    touch /run/reboot-required

    log_success "config.txt configuration complete"
    log_warn "config.txt changed: reboot for the camera configuration to take effect"
}

# raspi-config loads i2c-dev when you enable I2C, but this installer bypasses
# raspi-config, so make sure /dev/i2c-* nodes appear on boot.
ensure_i2c_dev() {
    if grep -qs "^i2c[-_]dev" /etc/modules; then
        log_info "i2c-dev already in /etc/modules (likely via raspi-config)"
    elif [[ -d /etc/modules-load.d ]] && grep -rqs "^i2c[-_]dev" /etc/modules-load.d/; then
        log_info "i2c-dev already in /etc/modules-load.d/"
    elif [[ -d /etc/modules-load.d ]]; then
        log_info "Adding i2c-dev to /etc/modules-load.d/pitrac.conf"
        echo "i2c-dev" >>/etc/modules-load.d/pitrac.conf
    fi
}

main() {
    log_info "PiTrac Camera Configuration"
    log_info "============================"

    if ! command -v python3 &>/dev/null; then
        log_warn "Python3 not found - skipping camera configuration"
        log_info "Camera configuration requires Python3 to be installed"
        exit 0
    fi

    if [[ ! -f "/usr/lib/pitrac/web-server/camera_detector.py" ]]; then
        log_error "Camera detector not found at /usr/lib/pitrac/web-server/camera_detector.py"
        log_error "Please ensure PiTrac web server is installed first"
        exit 1
    fi

    log_info "Detecting connected cameras..."
    local camera_json
    local num_cameras=0

    # camera_detector.py exits non-zero when no cameras found, so ignore exit code
    camera_json=$(sudo python3 /usr/lib/pitrac/web-server/camera_detector.py --json 2>/dev/null) || true

    if echo "$camera_json" | python3 -c "import sys, json; json.load(sys.stdin)" 2>/dev/null; then
        num_cameras=$(echo "$camera_json" | python3 -c "import sys, json; data=json.load(sys.stdin); print(len(data.get('cameras', [])))")

        if [[ "$num_cameras" -gt 0 ]]; then
            log_success "Detected ${num_cameras} camera(s)"
            echo "$camera_json" | python3 -c "
import sys, json
data = json.load(sys.stdin)
for cam in data.get('cameras', []):
    print(f\"  Camera {cam['index']}: {cam['description']} on {cam['port']} (Type {cam['pitrac_type']})\")
"
        else
            log_warn "No cameras detected"
        fi
    else
        log_warn "Camera detection returned no usable output"
    fi

    configure_boot_config "$num_cameras"
    ensure_i2c_dev

    log_success "Configuration completed successfully"
}

main "$@"
