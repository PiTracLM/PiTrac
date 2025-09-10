#!/usr/bin/env bash

# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags

# config/source.sh - Show where a configuration value comes from

key="${args[key]}"
user_config="${HOME}/.pitrac/config/pitrac.yaml"
system_config="/etc/pitrac/pitrac.yaml" 
default_config="/etc/pitrac/pitrac.yaml"
mappings_file="/etc/pitrac/config/parameter-mappings.yaml"
json_config="/etc/pitrac/golf_sim_config.json"

if [[ -z "$key" ]]; then
    error "Configuration key required"
    echo "Usage: pitrac config source <key>"
    echo "Example: pitrac config source cameras.camera1_gain"
    exit 1
fi

log_info "Searching for configuration key: $key"
echo ""

# Check in priority order and track where found
found_in=""
value=""
source_file=""

# Function to check for key in YAML file
check_yaml_file() {
    local file="$1"
    local desc="$2"
    
    if [[ ! -f "$file" ]]; then
        return 1
    fi
    
    if command -v python3 >/dev/null 2>&1; then
        local result
        result=$(python3 -c "
import yaml
import sys

try:
    with open('$file', 'r') as f:
        data = yaml.safe_load(f) or {}
    
    # Handle dot notation
    keys = '$key'.split('.')
    value = data
    for k in keys:
        if isinstance(value, dict) and k in value:
            value = value[k]
        else:
            sys.exit(1)
    
    print(value)
except:
    sys.exit(1)
" 2>/dev/null)
        
        if [[ $? -eq 0 ]]; then
            echo "✓ Found in $desc"
            echo "  File: $file"
            echo "  Value: $result"
            echo ""
            found_in="$desc"
            value="$result"
            source_file="$file"
            return 0
        fi
    else
        # Fallback: basic grep
        if grep -q "^[[:space:]]*${key##*.}:" "$file" 2>/dev/null; then
            local val
            val=$(grep "^[[:space:]]*${key##*.}:" "$file" | head -1 | cut -d: -f2- | xargs)
            echo "✓ Found in $desc"
            echo "  File: $file"
            echo "  Value: $val"
            echo ""
            found_in="$desc"
            value="$val"
            source_file="$file"
            return 0
        fi
    fi
    
    return 1
}

# Check environment variables first
env_var_name=""
if [[ -f "$mappings_file" ]] && command -v python3 >/dev/null 2>&1; then
    env_var_name=$(python3 -c "
import yaml
try:
    with open('$mappings_file', 'r') as f:
        mappings = yaml.safe_load(f) or {}
    if 'mappings' in mappings and '$key' in mappings['mappings']:
        mapping = mappings['mappings']['$key']
        if 'env_var' in mapping:
            print(mapping['env_var'])
except:
    pass
" 2>/dev/null)
fi

if [[ -n "$env_var_name" ]] && [[ -n "${!env_var_name}" ]]; then
    echo "✓ Found in environment variable"
    echo "  Variable: $env_var_name"
    echo "  Value: ${!env_var_name}"
    echo "  Priority: HIGHEST (overrides all configs)"
    echo ""
    found_in="environment"
    value="${!env_var_name}"
fi

# Check command line arguments (if pitrac is running)
if pgrep -f "pitrac_lm.*--.*$key" >/dev/null 2>&1; then
    echo "✓ Found in command line arguments"
    echo "  Process: $(pgrep -af "pitrac_lm" | grep -o "--[^ ]*$key[^ ]*" | head -1)"
    echo "  Priority: HIGH (overrides config files)"
    echo ""
    found_in="command_line"
fi

# Check user config
if [[ -z "$found_in" ]]; then
    check_yaml_file "$user_config" "user configuration"
fi

# Check system config
if [[ -z "$found_in" ]]; then
    check_yaml_file "$system_config" "system configuration"
fi

# Check default configs
if [[ -z "$found_in" ]]; then
    check_yaml_file "$default_config" "basic defaults"
fi


# Check parameter mappings for JSON mapping
if [[ -f "$mappings_file" ]] && command -v python3 >/dev/null 2>&1; then
    json_path=$(python3 -c "
import yaml
try:
    with open('$mappings_file', 'r') as f:
        mappings = yaml.safe_load(f) or {}
    if 'mappings' in mappings and '$key' in mappings['mappings']:
        mapping = mappings['mappings']['$key']
        if 'json_path' in mapping:
            print(mapping['json_path'])
except:
    pass
" 2>/dev/null)
    
    if [[ -n "$json_path" ]]; then
        echo "JSON Mapping:"
        echo "  YAML key: $key"
        echo "  Maps to: $json_path in golf_sim_config.json"
        
        # Check if exists in JSON
        if [[ -f "$json_config" ]] && command -v jq >/dev/null 2>&1; then
            json_val=$(jq -r ".${json_path//./\.}" "$json_config" 2>/dev/null)
            if [[ "$json_val" != "null" ]] && [[ -n "$json_val" ]]; then
                echo "  JSON value: $json_val"
            fi
        fi
        echo ""
    fi
fi

# Show validation rules if they exist
if [[ -f "$mappings_file" ]] && command -v python3 >/dev/null 2>&1; then
    python3 -c "
import yaml
try:
    with open('$mappings_file', 'r') as f:
        mappings = yaml.safe_load(f) or {}
    if 'mappings' in mappings and '$key' in mappings['mappings']:
        mapping = mappings['mappings']['$key']
        if 'validation' in mapping:
            print('Validation Rules:')
            val = mapping['validation']
            if 'min' in val:
                print(f'  Minimum: {val[\"min\"]}')
            if 'max' in val:
                print(f'  Maximum: {val[\"max\"]}')
            if 'enum' in val:
                print(f'  Allowed values: {val[\"enum\"]}')
            if 'pattern' in val:
                print(f'  Pattern: {val[\"pattern\"]}')
            print()
except:
    pass
" 2>/dev/null
fi

# Summary
if [[ -n "$found_in" ]]; then
    echo "=== Summary ==="
    echo "Key: $key"
    echo "Current value: $value"
    echo "Source: $found_in"
    if [[ -n "$source_file" ]]; then
        echo "File: $source_file"
    fi
    
    # Show override hierarchy
    echo ""
    echo "Override hierarchy (highest to lowest priority):"
    echo "  1. Environment variables"
    echo "  2. Command line arguments"
    echo "  3. User config (~/.pitrac/config/pitrac.yaml)"
    echo "  4. System config (/etc/pitrac/pitrac.yaml)"
    echo "  5. Default config"
    echo "  6. golf_sim_config.json"
else
    warn "Configuration key '$key' not found in any configuration source"
    echo ""
    echo "Available configuration keys can be found in:"
    echo "  - /etc/pitrac/pitrac.yaml"
    echo "  - /etc/pitrac/config/parameter-mappings.yaml"
    echo ""
    echo "Use 'pitrac config show' to see current configuration"
    exit 1
fi
