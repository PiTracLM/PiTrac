#!/usr/bin/env bash

# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags

# config/preset.sh - Apply configuration presets

preset_name="${args[preset_name]}"
user_config="${HOME}/.pitrac/config/pitrac.yaml"
mappings_file="/etc/pitrac/config/parameter-mappings.yaml"

# Create user config directory if needed
if [[ ! -d "$(dirname "$user_config")" ]]; then
    mkdir -p "$(dirname "$user_config")"
fi

# Create basic config if it doesn't exist
if [[ ! -f "$user_config" ]]; then
    cat > "$user_config" << 'EOF'
# PiTrac User Configuration
version: 2.0
profile: basic

EOF
fi

# Apply preset based on mappings file
if [[ -f "$mappings_file" ]] && command -v python3 >/dev/null 2>&1; then
    python3 << EOF
import yaml
import sys

try:
    # Load mappings with presets
    with open("$mappings_file", "r") as f:
        mappings_data = yaml.safe_load(f)
    
    # Load user config
    with open("$user_config", "r") as f:
        user_config = yaml.safe_load(f) or {}
    
    # Find preset
    preset_name = "$preset_name"
    if "presets" not in mappings_data or preset_name not in mappings_data["presets"]:
        print(f"ERROR: Preset '{preset_name}' not found", file=sys.stderr)
        sys.exit(1)
    
    preset = mappings_data["presets"][preset_name]
    print(f"Applying preset: {preset_name}")
    if "description" in preset:
        print(f"Description: {preset['description']}")
    
    # Apply preset settings
    if "settings" in preset:
        for key, value in preset["settings"].items():
            # Parse dot notation
            parts = key.split(".")
            current = user_config
            for part in parts[:-1]:
                if part not in current:
                    current[part] = {}
                current = current[part]
            current[parts[-1]] = value
            print(f"  Set {key} = {value}")
    
    # Save updated config
    with open("$user_config", "w") as f:
        yaml.dump(user_config, f, default_flow_style=False, sort_keys=False)
    
    print(f"SUCCESS: Preset '{preset_name}' applied to {user_config}")
    
except Exception as e:
    print(f"ERROR: Failed to apply preset: {e}", file=sys.stderr)
    sys.exit(1)
EOF
else
    # Fallback without Python
    case "$preset_name" in
        indoor)
            info "Applying indoor preset (basic settings only)"
            # Just add a comment to the config
            echo "# Preset: indoor" >> "$user_config"
            success "Indoor preset applied (limited without python3-yaml)"
            ;;
        outdoor)
            info "Applying outdoor preset (basic settings only)"
            echo "# Preset: outdoor" >> "$user_config"
            success "Outdoor preset applied (limited without python3-yaml)"
            ;;
        putting)
            info "Applying putting preset"
            echo "system:" >> "$user_config"
            echo "  putting_mode: true" >> "$user_config"
            success "Putting preset applied"
            ;;
        driver)
            info "Applying driver preset (basic settings only)"
            echo "# Preset: driver" >> "$user_config"
            success "Driver preset applied (limited without python3-yaml)"
            ;;
        debug)
            info "Applying debug preset"
            echo "storage:" >> "$user_config"
            echo "  log_exposure_images: true" >> "$user_config"
            echo "  log_spin_images: true" >> "$user_config"
            echo "  log_webserver_images: true" >> "$user_config"
            success "Debug preset applied"
            ;;
        *)
            error "Unknown preset: $preset_name"
            exit 1
            ;;
    esac
fi

log_info "Run 'pitrac config validate' to check configuration"
