#!/usr/bin/env bash
# boot-config.sh - Robust Raspberry Pi boot configuration management

set -euo pipefail

# Source logging functions if available
if [[ -f "$(dirname "${BASH_SOURCE[0]}")/pitrac-common-functions.sh" ]]; then
    source "$(dirname "${BASH_SOURCE[0]}")/pitrac-common-functions.sh"
else
    # Fallback logging functions
    log_info() { echo "[INFO] $*"; }
    log_warn() { echo "[WARN] $*"; }
    log_error() { echo "[ERROR] $*" >&2; }
    log_success() { echo "[✓] $*"; }
fi

readonly CONFIG_MAX_LINE_LENGTH=98  # Raspberry Pi firmware hard limit
readonly CONFIG_BACKUP_DIR="/boot/firmware/.pitrac_backups"

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

backup_config_txt() {
    local config_path="${1:-$(get_config_txt_path)}"
    local backup_dir="${2:-$CONFIG_BACKUP_DIR}"
    local timestamp=$(date +%Y%m%d_%H%M%S)
    local backup_path="${backup_dir}/config.txt.${timestamp}"

    mkdir -p "$backup_dir"

    if [[ ! -f "$config_path" ]]; then
        log_error "Config file not found: $config_path"
        return 1
    fi

    cp "$config_path" "$backup_path"
    log_info "Backup created: $backup_path"
    echo "$backup_path"
}

restore_config_txt() {
    local backup_path="$1"
    local config_path="${2:-$(get_config_txt_path)}"

    if [[ ! -f "$backup_path" ]]; then
        log_error "Backup file not found: $backup_path"
        return 1
    fi

    cp "$backup_path" "$config_path"
    log_success "Restored config from: $backup_path"
}

validate_line_length() {
    local line="$1"
    local max_length="${2:-$CONFIG_MAX_LINE_LENGTH}"

    if [[ ${#line} -gt $max_length ]]; then
        log_error "Line exceeds maximum length of $max_length characters:"
        log_error "  Length: ${#line}"
        log_error "  Line: ${line:0:100}..."
        return 1
    fi

    return 0
}

validate_config_file() {
    local config_path="${1:-$(get_config_txt_path)}"
    local errors=0

    log_info "Validating $config_path..."

    if [[ ! -f "$config_path" ]]; then
        log_error "Config file not found: $config_path"
        return 1
    fi

    local line_num=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [[ ${#line} -gt $CONFIG_MAX_LINE_LENGTH ]]; then
            log_error "Line $line_num exceeds $CONFIG_MAX_LINE_LENGTH chars (${#line} chars)"
            errors=$((errors + 1))
        fi
    done < "$config_path"

    local duplicates
    duplicates=$(grep -vE '^#|^$' "$config_path" | \
                 grep -E '^[a-zA-Z_][a-zA-Z0-9_]*=' | \
                 cut -d= -f1 | \
                 sort | \
                 uniq -d)

    if [[ -n "$duplicates" ]]; then
        log_warn "Duplicate parameters found:"
        while read -r param; do
            log_warn "  - $param (appears $(grep -cE "^${param}=" "$config_path") times)"
            errors=$((errors + 1))
        done <<< "$duplicates"
    fi

    if [[ $errors -eq 0 ]]; then
        log_success "Validation passed: no errors found"
        return 0
    else
        log_error "Validation failed: $errors error(s) found"
        return 1
    fi
}

param_exists() {
    local config_path="$1"
    local param_name="$2"

    if grep -qE "^${param_name}(=|$)" "$config_path" || \
       grep -qE "^#\s*${param_name}(=|$)" "$config_path"; then
        return 0
    fi

    return 1
}

get_param_value() {
    local config_path="$1"
    local param_name="$2"

    local value
    value=$(grep -E "^${param_name}=" "$config_path" | head -n1 | cut -d= -f2-)

    echo "$value"
}

is_param_commented() {
    local config_path="$1"
    local param_name="$2"

    if grep -qE "^#\s*${param_name}(=|$)" "$config_path" && \
       ! grep -qE "^${param_name}(=|$)" "$config_path"; then
        return 0
    fi

    return 1
}

remove_param_all() {
    local config_path="$1"
    local param_name="$2"

    sed -i "/^${param_name}=/d" "$config_path"
    sed -i "/^${param_name}$/d" "$config_path"
    sed -i "/^#.*${param_name}=/d" "$config_path"
    sed -i "/^#.*${param_name}$/d" "$config_path"
}

safe_config_set() {
    local config_path="$1"
    local param_name="$2"
    local param_value="${3:-}"  # Optional - empty for boolean params
    local section="${4:-}"       # Optional - filter section like "all", "pi4", "pi5"

    local new_line
    if [[ -z "$param_value" ]]; then
        new_line="$param_name"
    else
        new_line="${param_name}=${param_value}"
    fi

    if ! validate_line_length "$new_line"; then
        log_error "Cannot set parameter: line too long"
        log_error "Parameter: $param_name"
        log_error "Value: $param_value"
        log_error "Suggestion: Use shorter parameter values or dtoverlay shorthand"
        return 1
    fi

    remove_param_all "$config_path" "$param_name"

    if [[ -n "$section" ]]; then
        ensure_section "$config_path" "$section"
        insert_in_section "$config_path" "$new_line" "$section"
    else
        insert_before_sections "$config_path" "$new_line"
    fi

    log_success "Set: $new_line"
}

ensure_section() {
    local config_path="$1"
    local section="$2"  # e.g., "all", "pi4", "pi5"

    local section_marker="[$section]"

    if grep -qF "$section_marker" "$config_path"; then
        return 0
    fi

    echo "" >> "$config_path"
    echo "$section_marker" >> "$config_path"
    log_info "Created section: $section_marker"
}

insert_before_sections() {
    local config_path="$1"
    local new_line="$2"

    local temp_file=$(mktemp)
    local inserted=false

    while IFS= read -r line; do
        if [[ "$inserted" == "false" && "$line" =~ ^\[.*\]$ ]]; then
            echo "$new_line" >> "$temp_file"
            inserted=true
        fi
        echo "$line" >> "$temp_file"
    done < "$config_path"

    if [[ "$inserted" == "false" ]]; then
        echo "$new_line" >> "$temp_file"
    fi

    mv "$temp_file" "$config_path"
}

insert_in_section() {
    local config_path="$1"
    local new_line="$2"
    local section="$3"

    local temp_file=$(mktemp)
    local in_target_section=false
    local inserted=false

    while IFS= read -r line; do
        echo "$line" >> "$temp_file"

        if [[ "$line" == "[$section]" ]]; then
            in_target_section=true
        elif [[ "$line" =~ ^\[.*\]$ ]]; then
            if [[ "$in_target_section" == "true" && "$inserted" == "false" ]]; then
                local temp_file2=$(mktemp)
                head -n -1 "$temp_file" > "$temp_file2"
                echo "$new_line" >> "$temp_file2"
                echo "$line" >> "$temp_file2"
                mv "$temp_file2" "$temp_file"
                inserted=true
            fi
            in_target_section=false
        fi
    done < "$config_path"

    if [[ "$in_target_section" == "true" && "$inserted" == "false" ]]; then
        echo "$new_line" >> "$temp_file"
    fi

    mv "$temp_file" "$config_path"
}

set_dtparam() {
    local config_path="$1"
    local param="$2"      # e.g., "spi", "i2c_arm"
    local value="$3"      # e.g., "on", "off"
    local section="${4:-}"

    safe_config_set "$config_path" "dtparam=${param}" "$value" "$section"
}

add_dtoverlay() {
    local config_path="$1"
    local overlay="$2"    # e.g., "imx296,cam0" or "vc_mipi_imx296"
    local section="${3:-}"

    local full_line="dtoverlay=${overlay}"

    if ! validate_line_length "$full_line"; then
        log_error "dtoverlay line too long (${#full_line} > $CONFIG_MAX_LINE_LENGTH chars)"
        log_error "Overlay: $overlay"
        return 1
    fi

    if [[ -n "$section" ]]; then
        ensure_section "$config_path" "$section"
        insert_in_section "$config_path" "$full_line" "$section"
    else
        insert_before_sections "$config_path" "$full_line"
    fi

    log_success "Added: $full_line"
}

remove_dtoverlay() {
    local config_path="$1"
    local overlay="$2"

    sed -i "/^dtoverlay=${overlay}$/d" "$config_path"
    log_info "Removed dtoverlay: $overlay"
}

cleanup_duplicates() {
    local config_path="${1:-$(get_config_txt_path)}"

    log_info "Cleaning up duplicate parameters..."

    local temp_file=$(mktemp)
    local -A seen_params
    local -a lines

    mapfile -t lines < <(tac "$config_path")

    for line in "${lines[@]}"; do
        if [[ -z "$line" || "$line" =~ ^#.* ]]; then
            echo "$line" >> "$temp_file"
            continue
        fi

        if [[ "$line" =~ ^([a-zA-Z_][a-zA-Z0-9_]*)= ]]; then
            local param="${BASH_REMATCH[1]}"

            if [[ -z "${seen_params[$param]:-}" ]]; then
                seen_params[$param]=1
                echo "$line" >> "$temp_file"
            else
                log_info "  Removed duplicate: $line"
            fi
        elif [[ "$line" =~ ^([a-zA-Z_][a-zA-Z0-9_]*)$ ]]; then
            local param="${BASH_REMATCH[1]}"

            if [[ -z "${seen_params[$param]:-}" ]]; then
                seen_params[$param]=1
                echo "$line" >> "$temp_file"
            else
                log_info "  Removed duplicate: $line"
            fi
        else
            echo "$line" >> "$temp_file"
        fi
    done

    tac "$temp_file" > "$config_path"
    rm "$temp_file"

    log_success "Duplicate cleanup complete"
}

remove_pitrac_block() {
    local config_path="${1:-$(get_config_txt_path)}"

    log_info "Removing PiTrac configuration block..."

    sed -i '/# PiTrac Camera Configuration/,/# End PiTrac Camera Configuration/d' "$config_path" 2>/dev/null || true
    sed -i '/# Added by PiTrac installer/d' "$config_path" 2>/dev/null || true

    log_success "PiTrac block removed"
}

configure_pitrac_boot() {
    local config_path="${1:-$(get_config_txt_path)}"
    local pi_model="${2:-unknown}"
    local num_cameras="${3:-0}"
    local has_innomaker="${4:-false}"

    log_info "Configuring PiTrac boot settings..."
    log_info "  Pi Model: $pi_model"
    log_info "  Cameras: $num_cameras"
    log_info "  InnoMaker: $has_innomaker"

    local backup
    backup=$(backup_config_txt "$config_path")

    remove_pitrac_block "$config_path"

    safe_config_set "$config_path" "camera_auto_detect" "1"
    safe_config_set "$config_path" "force_turbo" "1"

    case "$pi_model" in
        pi5)
            safe_config_set "$config_path" "arm_boost" "1"
            ;;
        pi4)
            safe_config_set "$config_path" "gpu_mem" "256"
            ;;
    esac

    set_dtparam "$config_path" "spi" "on" "all"

    # Camera-specific overlays in [all] section
    if [[ "$num_cameras" -ge 1 ]]; then
        if [[ "$num_cameras" -eq 2 ]]; then
            add_dtoverlay "$config_path" "imx296,cam0" "all"
            add_dtoverlay "$config_path" "imx296,sync-sink" "all"
        else
            add_dtoverlay "$config_path" "imx296,cam0" "all"
        fi
    fi

    # InnoMaker support
    if [[ "$has_innomaker" == "true" ]]; then
        set_dtparam "$config_path" "i2c_vc" "on" "all"
        add_dtoverlay "$config_path" "vc_mipi_imx296" "all"
    fi

    if ! validate_config_file "$config_path"; then
        log_error "Configuration validation failed! Restoring backup..."
        restore_config_txt "$backup" "$config_path"
        return 1
    fi

    log_success "PiTrac boot configuration complete"
    log_warn "Reboot required for changes to take effect"
}
