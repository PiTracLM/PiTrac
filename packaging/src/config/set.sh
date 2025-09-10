
#!/usr/bin/env bash
# config set - Set configuration value using JSON

# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags

# Source JSON config library
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../lib/config_json.sh"

key="${args[key]}"
value="${args[value]}"

# Validate the value if possible
if ! validate_config_value "$key" "$value"; then
    exit 1
fi

# Set the configuration value
if set_config_value "$key" "$value"; then
    log_success "Configuration updated"
    log_info "Restart PiTrac for changes to take effect: pitrac stop && pitrac run"
else
    error "Failed to update configuration"
    exit 1
fi
