#!/usr/bin/env bash
# lib/config_json.sh - JSON-based configuration management

readonly SYSTEM_CONFIG="/etc/pitrac/golf_sim_config.json"
readonly USER_SETTINGS="${HOME}/.pitrac/config/user_settings.json"

# Ensure jq is available
check_jq() {
    if ! command -v jq >/dev/null 2>&1; then
        error "jq is required for configuration management. Please install it:"
        echo "  sudo apt-get install jq    # Debian/Ubuntu"
        echo "  brew install jq            # macOS"
        return 1
    fi
    return 0
}

# Initialize user settings file if it doesn't exist
init_user_settings() {
    if [[ ! -f "$USER_SETTINGS" ]]; then
        mkdir -p "$(dirname "$USER_SETTINGS")"
        echo "{}" > "$USER_SETTINGS"
    fi
}

# Get a configuration value (checks user settings first, then system defaults)
get_config_value() {
    local key="$1"
    local default="${2:-}"
    
    check_jq || return 1
    
    # Convert dot notation to jq path (.gs_config.cameras.kCamera1Gain)
    local jq_path=".${key}"
    
    # Check user settings first
    if [[ -f "$USER_SETTINGS" ]]; then
        local user_value
        user_value=$(jq -r "$jq_path // empty" "$USER_SETTINGS" 2>/dev/null)
        if [[ -n "$user_value" && "$user_value" != "null" ]]; then
            echo "$user_value"
            return 0
        fi
    fi
    
    # Fall back to system defaults
    if [[ -f "$SYSTEM_CONFIG" ]]; then
        local system_value
        system_value=$(jq -r "$jq_path // empty" "$SYSTEM_CONFIG" 2>/dev/null)
        if [[ -n "$system_value" && "$system_value" != "null" ]]; then
            echo "$system_value"
            return 0
        fi
    fi
    
    # Return default if provided
    echo "$default"
}

# Set a configuration value in user settings
set_config_value() {
    local key="$1"
    local value="$2"
    
    check_jq || return 1
    init_user_settings
    
    # Convert dot notation to jq path
    local jq_path=".${key}"
    
    # Check if value is different from system default
    local default_value
    if [[ -f "$SYSTEM_CONFIG" ]]; then
        default_value=$(jq -r "$jq_path // empty" "$SYSTEM_CONFIG" 2>/dev/null)
    fi
    
    if [[ "$value" == "$default_value" ]]; then
        # Remove from user settings if it's the same as default
        delete_config_value "$key"
        log_info "Reset $key to default value"
    else
        # Update user settings with new value
        local temp_file
        temp_file=$(mktemp)
        
        # Build the nested structure from dot notation
        local jq_expr="$jq_path = "
        
        # Detect value type and format appropriately
        if [[ "$value" =~ ^[0-9]+$ ]]; then
            # Integer
            jq_expr="${jq_expr}${value}"
        elif [[ "$value" =~ ^[0-9]+\.[0-9]+$ ]]; then
            # Float
            jq_expr="${jq_expr}${value}"
        elif [[ "$value" == "true" || "$value" == "false" ]]; then
            # Boolean
            jq_expr="${jq_expr}${value}"
        else
            # String
            jq_expr="${jq_expr}\"${value}\""
        fi
        
        jq "$jq_expr" "$USER_SETTINGS" > "$temp_file" 2>/dev/null
        
        if [[ $? -eq 0 ]]; then
            mv "$temp_file" "$USER_SETTINGS"
            log_success "Set $key = $value"
        else
            rm -f "$temp_file"
            error "Failed to set configuration value"
            return 1
        fi
    fi
}

# Delete a configuration value from user settings
delete_config_value() {
    local key="$1"
    
    check_jq || return 1
    
    if [[ ! -f "$USER_SETTINGS" ]]; then
        return 0
    fi
    
    # Convert dot notation to jq delete path
    local jq_path=".${key}"
    
    local temp_file
    temp_file=$(mktemp)
    
    jq "del($jq_path)" "$USER_SETTINGS" > "$temp_file" 2>/dev/null
    
    if [[ $? -eq 0 ]]; then
        mv "$temp_file" "$USER_SETTINGS"
    else
        rm -f "$temp_file"
    fi
}

# List all configuration values (merged view)
list_config_values() {
    local filter="${1:-}"
    
    check_jq || return 1
    
    # Merge system and user configs
    local merged_config
    merged_config=$(mktemp)
    
    if [[ -f "$SYSTEM_CONFIG" ]]; then
        cp "$SYSTEM_CONFIG" "$merged_config"
    else
        echo "{}" > "$merged_config"
    fi
    
    if [[ -f "$USER_SETTINGS" ]]; then
        # Merge user settings over system defaults
        jq -s '.[0] * .[1]' "$merged_config" "$USER_SETTINGS" > "${merged_config}.tmp"
        mv "${merged_config}.tmp" "$merged_config"
    fi
    
    if [[ -n "$filter" ]]; then
        # Filter by prefix
        jq ".$filter // {}" "$merged_config" 2>/dev/null
    else
        jq '.' "$merged_config"
    fi
    
    rm -f "$merged_config"
}

# Show only user overrides
show_user_overrides() {
    check_jq || return 1
    
    if [[ -f "$USER_SETTINGS" ]]; then
        jq '.' "$USER_SETTINGS"
    else
        echo "{}"
    fi
}

# Reset all user settings
reset_all_config() {
    if [[ -f "$USER_SETTINGS" ]]; then
        # Backup before reset
        local backup="${USER_SETTINGS}.backup.$(date +%Y%m%d_%H%M%S)"
        cp "$USER_SETTINGS" "$backup"
        log_info "Backed up current settings to: $backup"
        
        echo "{}" > "$USER_SETTINGS"
        log_success "Reset all user settings to defaults"
    else
        log_info "No user settings to reset"
    fi
}

# Validate configuration value
validate_config_value() {
    local key="$1"
    local value="$2"
    
    # Basic validation based on key patterns
    case "$key" in
        *.Gain|*.gain)
            # Gain values typically 0.5-16.0
            if [[ ! "$value" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
                error "Gain must be a number"
                return 1
            fi
            local float_val
            float_val=$(echo "$value" | awk '{print ($1 >= 0.5 && $1 <= 16.0) ? "valid" : "invalid"}')
            if [[ "$float_val" != "valid" ]]; then
                error "Gain must be between 0.5 and 16.0"
                return 1
            fi
            ;;
        *.Port|*.port)
            # Port values 1-65535
            if [[ ! "$value" =~ ^[0-9]+$ ]] || [[ $value -lt 1 ]] || [[ $value -gt 65535 ]]; then
                error "Port must be between 1 and 65535"
                return 1
            fi
            ;;
        *.Address|*.address|*.host|*.Host)
            # IP address or hostname
            if [[ -n "$value" ]] && ! [[ "$value" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ || "$value" =~ ^[a-zA-Z0-9.-]+$ ]]; then
                error "Invalid address format"
                return 1
            fi
            ;;
    esac
    
    return 0
}

# Export configuration as environment variables (for C++ binary)
export_config_env() {
    # Camera types are special - they come from user settings or defaults
    local camera1_type
    local camera2_type
    
    camera1_type=$(get_config_value "cameras.slot1.type" "4")
    camera2_type=$(get_config_value "cameras.slot2.type" "4")
    
    export PITRAC_SLOT1_CAMERA_TYPE="$camera1_type"
    export PITRAC_SLOT2_CAMERA_TYPE="$camera2_type"
    
    # Export any lens types if configured
    local lens1
    local lens2
    lens1=$(get_config_value "cameras.slot1.lens" "")
    lens2=$(get_config_value "cameras.slot2.lens" "")
    
    [[ -n "$lens1" ]] && export PITRAC_SLOT1_LENS_TYPE="$lens1"
    [[ -n "$lens2" ]] && export PITRAC_SLOT2_LENS_TYPE="$lens2"
}