
#!/usr/bin/env bash
# config get - Get configuration value using JSON

# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags

# Source JSON config library
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../lib/config_json.sh"

key="${args[key]:-}"
show_all="${args[--all]:-}"
show_user="${args[--user-only]:-}"

if [[ -z "$key" && "$show_all" != "1" && "$show_user" != "1" ]]; then
    error "Specify a configuration key or use --all to show all settings"
    echo ""
    echo "Examples:"
    echo "  pitrac config get gs_config.cameras.kCamera1Gain"
    echo "  pitrac config get --all"
    echo "  pitrac config get --user-only"
    exit 1
fi

if [[ "$show_all" == "1" ]]; then
    log_info "Showing all configuration (merged system + user):"
    list_config_values
elif [[ "$show_user" == "1" ]]; then
    log_info "Showing user overrides only:"
    show_user_overrides
else
    # Get specific value
    value=$(get_config_value "$key")
    
    if [[ -n "$value" ]]; then
        echo "$value"
    else
        warn "Configuration key '$key' not found"
        exit 1
    fi
fi
