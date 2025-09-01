#!/usr/bin/env bash

# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags

# config/migrate.sh - Migrate from old JSON configuration to new YAML system

dry_run="${args[--dry-run]:-}"
json_file="golf_sim_config.json"
yaml_file="${HOME}/.pitrac/config/pitrac.yaml"
mappings_file="/etc/pitrac/config/parameter-mappings.yaml"

# Check if JSON file exists
if [[ ! -f "$json_file" ]]; then
    error "No golf_sim_config.json found in current directory"
    exit 1
fi

log_info "Analyzing existing configuration..."

# Create temporary Python script for migration
python_script='
import json
import yaml
import sys

# Load JSON configuration
with open("golf_sim_config.json", "r") as f:
    json_config = json.load(f)

# Load mappings if available
mappings = {}
try:
    with open("/etc/pitrac/config/parameter-mappings.yaml", "r") as f:
        mappings_data = yaml.safe_load(f)
        if mappings_data and "mappings" in mappings_data:
            mappings = mappings_data["mappings"]
except:
    pass

# Reverse mapping: JSON path to YAML key
reverse_map = {}
for yaml_key, mapping in mappings.items():
    if isinstance(mapping, dict) and "json_path" in mapping:
        reverse_map[mapping["json_path"]] = yaml_key

# Default values to compare against
defaults = {
    "gs_config.modes.kStartInPuttingMode": "0",
    "gs_config.ball_identification.kDetectionMethod": "legacy",
    "gs_config.ball_identification.kUseCLAHEProcessing": "1",
    "gs_config.ball_identification.kCLAHEClipLimit": "8",
    "gs_config.cameras.kCamera1Gain": "1.0",
    "gs_config.cameras.kCamera2Gain": "4.0",
    "gs_config.logging.kLogIntermediateExposureImagesToFile": "1",
    "gs_config.logging.kLogIntermediateSpinImagesToFile": "1",
    "gs_config.logging.kLogWebserverImagesToFile": "1",
    "gs_config.logging.kLogDiagnosticImagesToUniqueFiles": "1"
}

# Find non-default values
changes = {}
def traverse(obj, path=""):
    if isinstance(obj, dict):
        for key, value in obj.items():
            new_path = f"{path}.{key}" if path else key
            traverse(value, new_path)
    else:
        full_path = f"gs_config.{path}"
        if full_path in defaults and str(obj) != defaults[full_path]:
            if full_path in reverse_map:
                yaml_key = reverse_map[full_path]
                changes[yaml_key] = str(obj)
            else:
                # No mapping found, store as custom
                changes[f"_custom.{path}"] = str(obj)

traverse(json_config)

# Output migrated configuration
if changes:
    print("# Migrated settings from golf_sim_config.json")
    print("# Review and adjust as needed")
    print("")
    
    # Group by section
    sections = {}
    for key, value in sorted(changes.items()):
        section = key.split(".")[0]
        if section not in sections:
            sections[section] = {}
        
        # Build nested structure
        parts = key.split(".")
        current = sections[section]
        for part in parts[1:-1]:
            if part not in current:
                current[part] = {}
            current = current[part]
        current[parts[-1]] = value
    
    # Output YAML
    for section, content in sections.items():
        if section == "_custom":
            print("# Custom settings (no direct mapping)")
        print(f"{section}:")
        yaml.dump(content, sys.stdout, default_flow_style=False, indent=2)
        print()
else:
    print("# No non-default values found")
'

# Run migration analysis
if [[ "$dry_run" == "1" ]]; then
    info "Dry run mode - showing what would be migrated:"
    echo ""
    python3 -c "$python_script"
    echo ""
    info "To apply these changes, run without --dry-run"
else
    # Create backup of existing YAML if it exists
    if [[ -f "$yaml_file" ]]; then
        backup="${yaml_file}.backup.$(date +%Y%m%d_%H%M%S)"
        cp "$yaml_file" "$backup"
        info "Backed up existing config to: $backup"
    fi
    
    # Create user config directory if needed
    user_config_dir="$(dirname "$yaml_file")"
    if [[ ! -d "$user_config_dir" ]]; then
        mkdir -p "$user_config_dir"
    fi
    
    # Generate base configuration
    cat > "$yaml_file" << 'EOF'
# PiTrac Configuration
# Migrated from golf_sim_config.json
# Generated: $(date)

version: 2.0
profile: basic

EOF
    
    # Append migrated settings
    python3 -c "$python_script" >> "$yaml_file"
    
    success "Configuration migrated to: $yaml_file"
    info "Review the migrated settings with: pitrac config edit"
    info "Validate the configuration with: pitrac config validate"
fi
