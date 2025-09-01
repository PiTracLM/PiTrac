#!/usr/bin/env bash

# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags

# config/diff.sh - Show configuration differences from defaults

user_config="${HOME}/.pitrac/config/pitrac.yaml"
system_config="/etc/pitrac/pitrac.yaml"
default_config="/etc/pitrac/config/settings-basic.yaml"
json_output="${args[--json]:-}"

# Determine which configs to compare
if [[ -f "$user_config" ]]; then
    current_config="$user_config"
    info "Comparing user configuration against defaults"
elif [[ -f "$system_config" ]]; then
    current_config="$system_config"
    info "Comparing system configuration against defaults"
else
    error "No configuration found"
    info "Create one with: pitrac config edit"
    exit 1
fi

# Check if default template exists
if [[ ! -f "$default_config" ]] && [[ -f "/usr/share/pitrac/config/settings-basic.yaml" ]]; then
    default_config="/usr/share/pitrac/config/settings-basic.yaml"
fi

if [[ ! -f "$default_config" ]]; then
    # Fallback: Show non-comment lines from current config
    warn "Default configuration template not found"
    info "Showing all non-default settings:"
    echo ""
    grep -v '^#' "$current_config" | grep -v '^$' | grep -v '^version:' | grep -v '^profile:'
    exit 0
fi

# Use Python if available for accurate YAML parsing
if command -v python3 >/dev/null 2>&1; then
    python3 << EOF
import yaml
import json
import sys

def flatten_dict(d, parent_key='', sep='.'):
    """Flatten nested dictionary with dot notation."""
    items = []
    for k, v in d.items():
        new_key = f"{parent_key}{sep}{k}" if parent_key else k
        if isinstance(v, dict):
            items.extend(flatten_dict(v, new_key, sep=sep).items())
        else:
            items.append((new_key, v))
    return dict(items)

def load_yaml_safe(filepath):
    """Load YAML file safely, returning empty dict on error."""
    try:
        with open(filepath, 'r') as f:
            return yaml.safe_load(f) or {}
    except:
        return {}

# Load configurations
current = load_yaml_safe("$current_config")
defaults = load_yaml_safe("$default_config")

# Remove metadata fields
for key in ['version', 'profile', '_preset']:
    current.pop(key, None)
    defaults.pop(key, None)

# Flatten for comparison
current_flat = flatten_dict(current)
defaults_flat = flatten_dict(defaults)

# Find differences
added = {}
modified = {}
removed = {}

for key, value in current_flat.items():
    if key not in defaults_flat:
        added[key] = value
    elif defaults_flat[key] != value:
        modified[key] = {'from': defaults_flat[key], 'to': value}

for key, value in defaults_flat.items():
    if key not in current_flat:
        removed[key] = value

if "$json_output" == "1":
    # JSON output
    result = {
        'added': added,
        'modified': modified,
        'removed': removed
    }
    print(json.dumps(result, indent=2))
else:
    # Human-readable output
    if not added and not modified and not removed:
        print("✓ Configuration matches defaults")
        sys.exit(0)
    
    if modified:
        print("=== Modified Settings ===")
        for key, values in modified.items():
            print(f"  {key}:")
            print(f"    Default: {values['from']}")
            print(f"    Current: {values['to']}")
        print()
    
    if added:
        print("=== Added Settings ===")
        for key, value in added.items():
            print(f"  {key}: {value}")
        print()
    
    if removed:
        print("=== Settings Using Defaults ===")
        print("  (These are in the default config but not in your config)")
        for key, value in removed.items():
            print(f"  {key}: {value}")
        print()
    
    print(f"Summary: {len(modified)} modified, {len(added)} added, {len(removed)} using defaults")

EOF

else
    # Fallback without Python - basic diff
    info "Using basic diff (install python3-yaml for better output)"
    echo ""
    
    if command -v diff >/dev/null 2>&1; then
        # Show unified diff
        diff -u --label="Default" --label="Current" "$default_config" "$current_config" | tail -n +3
    else
        # Ultra-basic: just show the current config
        echo "=== Current Configuration ==="
        grep -v '^#' "$current_config" | grep -v '^$'
    fi
fi
