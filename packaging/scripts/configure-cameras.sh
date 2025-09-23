#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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

# Backup config.txt before modifying
backup_config_txt() {
    local config_path="$1"
    local backup_path="${config_path}.pitrac.backup.$(date +%Y%m%d_%H%M%S)"

    log_info "Backing up ${config_path} to ${backup_path}"
    cp "$config_path" "$backup_path"
}

update_config_txt_param() {
    local config_path="$1"
    local param_name="$2"
    local param_value="$3"

    if [[ -z "$param_value" ]]; then
        local pattern="^${param_name}$"
        local new_line="${param_name}"
    else
        local pattern="^${param_name}="
        local new_line="${param_name}=${param_value}"
    fi

    if grep -q "$pattern" "$config_path"; then
        log_info "  Updating existing: ${new_line}"
        sed -i "s|${pattern}.*|${new_line}|" "$config_path"
    else
        log_info "  Adding new: ${new_line}"
        echo "${new_line}" >> "$config_path"
    fi
}

remove_config_txt_param() {
    local config_path="$1"
    local param_pattern="$2"

    if grep -q "^${param_pattern}" "$config_path"; then
        log_info "  Removing: ${param_pattern}"
        sed -i "/^${param_pattern}/d" "$config_path"
    fi
}

insert_in_config_section() {
    local config_path="$1"
    local content="$2"
    local section="${3:-global}"

    local temp_file=$(mktemp)

    if [[ "$section" == "global" ]]; then
        local inserted=false
        local in_section=false

        while IFS= read -r line; do
            if [[ "$line" =~ ^\[.*\]$ ]]; then
                if [[ "$inserted" == "false" ]]; then
                    echo "$content" >> "$temp_file"
                    inserted=true
                fi
                in_section=true
            fi
            echo "$line" >> "$temp_file"
        done < "$config_path"

        if [[ "$inserted" == "false" ]]; then
            echo "$content" >> "$temp_file"
        fi
    else
        local in_target_section=false
        local content_inserted=false

        while IFS= read -r line; do
            echo "$line" >> "$temp_file"

            if [[ "$line" == "[$section]" ]]; then
                in_target_section=true
            elif [[ "$line" =~ ^\[.*\]$ ]]; then
                if [[ "$in_target_section" == "true" ]] && [[ "$content_inserted" == "false" ]]; then
                    echo "$content" >> "$temp_file"
                    content_inserted=true
                fi
                in_target_section=false
            fi
        done < "$config_path"

        if [[ "$in_target_section" == "true" ]] && [[ "$content_inserted" == "false" ]]; then
            echo "$content" >> "$temp_file"
        fi
    fi

    mv "$temp_file" "$config_path"
}

configure_boot_config() {
    local camera_json="$1"
    local config_path

    config_path=$(get_config_txt_path) || return 1

    local num_cameras=$(echo "$camera_json" | python3 -c "import sys, json; data=json.load(sys.stdin); print(len(data.get('cameras', [])))")
    local has_innomaker=false
    local slot1_type=""
    local slot2_type=""

    if [[ "$num_cameras" -ge 1 ]]; then
        slot1_type=$(echo "$camera_json" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('configuration', {}).get('slot1', {}).get('type', ''))" 2>/dev/null || echo "")
    fi

    if [[ "$num_cameras" -ge 2 ]]; then
        slot2_type=$(echo "$camera_json" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('configuration', {}).get('slot2', {}).get('type', ''))" 2>/dev/null || echo "")
    fi

    # Check if any camera is InnoMaker (type 5)
    if [[ "$slot1_type" == "5" ]] || [[ "$slot2_type" == "5" ]]; then
        has_innomaker=true
    fi

    log_info "Detected camera configuration:"
    log_info "  Number of cameras: $num_cameras"
    log_info "  Slot 1 type: ${slot1_type:-none}"
    log_info "  Slot 2 type: ${slot2_type:-none}"
    log_info "  Has InnoMaker: $has_innomaker"

    # Skip if no cameras detected
    if [[ "$num_cameras" -eq 0 ]]; then
        log_warn "No cameras detected, skipping config.txt configuration"
        return 0
    fi

    backup_config_txt "$config_path"

    log_info "Configuring ${config_path}..."

    log_info "Removing existing PiTrac configuration (if present)..."
    sed -i '/# PiTrac Camera Configuration/,/# End PiTrac Camera Configuration/d' "$config_path" 2>/dev/null || true

    sed -i '/# Added by PiTrac installer/d' "$config_path" 2>/dev/null || true

    # Build config block dynamically, checking for existing values
    local config_block="# PiTrac Camera Configuration - Added by pitrac installer
# DO NOT MODIFY - Managed automatically by PiTrac"

    # Only add parameters that don't already exist
    if ! grep -q "^camera_auto_detect=" "$config_path"; then
        config_block="$config_block

# Disable automatic camera detection for manual control
camera_auto_detect=0"
    else
        log_info "  camera_auto_detect already exists, skipping"
    fi

    config_block="$config_block

# Core system parameters for PiTrac operation"

    if ! grep -q "^dtparam=spi=on" "$config_path"; then
        config_block="$config_block
dtparam=spi=on"
    else
        log_info "  dtparam=spi=on already exists, skipping"
    fi

    if ! grep -q "^force_turbo=" "$config_path"; then
        config_block="$config_block
force_turbo=1"
    else
        log_info "  force_turbo already exists, skipping"
    fi

    if ! grep -q "^arm_boost=" "$config_path"; then
        config_block="$config_block
arm_boost=1"
    else
        log_info "  arm_boost already exists, skipping"
    fi

    if [[ "$num_cameras" -eq 2 ]]; then
        config_block="$config_block

# Dual camera configuration (single-pi system)
# Camera 0: internal trigger, Camera 1: external trigger
[all]
dtoverlay=imx296,cam0
dtoverlay=imx296,sync-sink"
    elif [[ "$num_cameras" -eq 1 ]]; then
        config_block="$config_block

# Single camera configuration
[all]
dtoverlay=imx296,cam0"
    fi

    if [[ "$has_innomaker" == "true" ]]; then
        config_block="$config_block

# InnoMaker IMX296 camera support
dtparam=i2c_vc=on
dtoverlay=vc_mipi_imx296"
    fi

    config_block="$config_block

# End PiTrac Camera Configuration"
    local temp_file=$(mktemp)
    local inserted=false
    local line_count=0

    while IFS= read -r line; do
        line_count=$((line_count + 1))

        if [[ "$inserted" == "false" ]]; then
            if [[ "$line" =~ ^\[.*\]$ ]]; then
                echo "" >> "$temp_file"
                echo "$config_block" >> "$temp_file"
                echo "" >> "$temp_file"
                inserted=true
            elif [[ "$line_count" -gt 10 ]] && [[ ! "$line" =~ ^# ]] && [[ -n "$line" ]]; then
                echo "" >> "$temp_file"
                echo "$config_block" >> "$temp_file"
                echo "" >> "$temp_file"
                inserted=true
            fi
        fi

        echo "$line" >> "$temp_file"
    done < "$config_path"

    if [[ "$inserted" == "false" ]]; then
        echo "" >> "$temp_file"
        echo "$config_block" >> "$temp_file"
    fi

    mv "$temp_file" "$config_path"

    log_success "config.txt configuration complete"

    log_warn "IMPORTANT: System must be rebooted for camera configuration changes to take effect"
}

# Configure user_settings.json based on detected cameras
configure_user_settings() {
    local camera_json="$1"
    local user_settings_path="${2:-${HOME}/.pitrac/config/user_settings.json}"

    mkdir -p "$(dirname "$user_settings_path")"

    # Parse camera configuration
    local slot1_type=$(echo "$camera_json" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('configuration', {}).get('slot1', {}).get('type', ''))" 2>/dev/null || echo "")
    local slot1_lens=$(echo "$camera_json" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('configuration', {}).get('slot1', {}).get('lens', '1'))" 2>/dev/null || echo "1")
    local slot2_type=$(echo "$camera_json" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('configuration', {}).get('slot2', {}).get('type', ''))" 2>/dev/null || echo "")
    local slot2_lens=$(echo "$camera_json" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('configuration', {}).get('slot2', {}).get('lens', '1'))" 2>/dev/null || echo "1")

    log_info "Configuring user settings at ${user_settings_path}..."

    # Create or update user_settings.json using Python for proper JSON handling
    python3 <<EOF
import json
import os
from pathlib import Path

settings_path = "${user_settings_path}"
slot1_type = "${slot1_type}"
slot1_lens = "${slot1_lens}"
slot2_type = "${slot2_type}"
slot2_lens = "${slot2_lens}"

# Load existing settings if present
if os.path.exists(settings_path):
    try:
        with open(settings_path, 'r') as f:
            settings = json.load(f)
    except (json.JSONDecodeError, IOError):
        settings = {}
else:
    settings = {}

# Update camera settings if cameras were detected
if slot1_type:
    settings["cameras.slot1.type"] = slot1_type
    settings["cameras.slot1.lens"] = slot1_lens
    print(f"  Setting camera 1: type={slot1_type}, lens={slot1_lens}")

if slot2_type:
    settings["cameras.slot2.type"] = slot2_type
    settings["cameras.slot2.lens"] = slot2_lens
    print(f"  Setting camera 2: type={slot2_type}, lens={slot2_lens}")

# Write back the settings
Path(settings_path).parent.mkdir(parents=True, exist_ok=True)
with open(settings_path, 'w') as f:
    json.dump(settings, f, indent=2)

print(f"  Wrote settings to {settings_path}")
EOF

    if [[ $EUID -eq 0 ]] && [[ -n "${SUDO_USER:-}" ]]; then
        chown -R "${SUDO_USER}:${SUDO_USER}" "$(dirname "$user_settings_path")"
        log_info "  Set ownership to ${SUDO_USER}"
    fi

    log_success "User settings configuration complete"
}

main() {
    log_info "PiTrac Camera Configuration"
    log_info "============================"

    if ! command -v python3 &> /dev/null; then
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

    if camera_json=$(sudo python3 /usr/lib/pitrac/web-server/camera_detector.py --json 2>/dev/null); then
        if echo "$camera_json" | python3 -c "import sys, json; data=json.load(sys.stdin); sys.exit(0 if data.get('success', False) else 1)" 2>/dev/null; then
            local num_cameras=$(echo "$camera_json" | python3 -c "import sys, json; data=json.load(sys.stdin); print(len(data.get('cameras', [])))")

            if [[ "$num_cameras" -eq 0 ]]; then
                log_warn "No cameras detected - skipping camera configuration"
                log_info "Camera configuration can be done manually later if needed"
                exit 0
            fi

            log_success "Successfully detected ${num_cameras} camera(s)"

            echo "$camera_json" | python3 -c "
import sys, json
data = json.load(sys.stdin)
for cam in data.get('cameras', []):
    print(f\"  Camera {cam['index']}: {cam['description']} on {cam['port']} (Type {cam['pitrac_type']})\")
"

            configure_boot_config "$camera_json"

            if [[ -n "${SUDO_USER:-}" ]]; then
                user_home=$(eval echo ~${SUDO_USER})
            else
                user_home="${HOME}"
            fi
            configure_user_settings "$camera_json" "${user_home}/.pitrac/config/user_settings.json"

            log_success "Camera configuration completed successfully"
            log_warn "Please reboot the system for camera configuration to take effect"

        else
            log_error "Camera detection failed"
            log_info "You can manually configure cameras later if needed"
            exit 0
        fi
    else
        log_warn "Could not run camera detection - skipping camera configuration"
        log_info "This may be normal on non-Pi systems or if cameras are not connected"
        exit 0
    fi
}

main "$@"