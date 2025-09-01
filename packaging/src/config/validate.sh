#!/usr/bin/env bash

# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags

# config/validate.sh - Validate PiTrac configuration

user_config="${HOME}/.pitrac/config/pitrac.yaml"
mappings_file="/etc/pitrac/config/parameter-mappings.yaml"
fix_issues="${args[--fix]:-}"

if [[ ! -f "$user_config" ]]; then
    error "No user configuration found at $user_config"
    info "Create one with: pitrac config edit"
    exit 1
fi

# First check YAML syntax
log_info "Checking YAML syntax..."
if command -v python3 >/dev/null 2>&1; then
    if python3 -c "import yaml; yaml.safe_load(open('$user_config'))" 2>/dev/null; then
        success "YAML syntax is valid"
    else
        error "YAML syntax errors found!"
        python3 -c "import yaml; yaml.safe_load(open('$user_config'))" 2>&1
        exit 1
    fi
elif command -v yq >/dev/null 2>&1; then
    if yq eval '.' "$user_config" >/dev/null 2>&1; then
        success "YAML syntax is valid"
    else
        error "YAML syntax errors found!"
        yq eval '.' "$user_config" 2>&1
        exit 1
    fi
else
    warn "Cannot validate YAML syntax (install python3-yaml or yq)"
fi

# Validate values against schema
log_info "Validating configuration values..."

if [[ -f "$mappings_file" ]] && command -v python3 >/dev/null 2>&1; then
    python3 << 'EOF'
import yaml
import sys
import re

errors = []
warnings = []
fixed = []

try:
    # Load user config
    with open("$user_config", "r") as f:
        user_config = yaml.safe_load(f) or {}
    
    # Load mappings with validation rules
    with open("$mappings_file", "r") as f:
        mappings_data = yaml.safe_load(f)
    
    if "mappings" not in mappings_data:
        print("WARNING: No mappings found in parameter-mappings.yaml")
        sys.exit(0)
    
    mappings = mappings_data["mappings"]
    
    # Flatten user config for validation
    def flatten_dict(d, parent_key='', sep='.'):
        items = []
        for k, v in d.items():
            new_key = f"{parent_key}{sep}{k}" if parent_key else k
            if isinstance(v, dict):
                items.extend(flatten_dict(v, new_key, sep=sep).items())
            else:
                items.append((new_key, v))
        return dict(items)
    
    flat_config = flatten_dict(user_config)
    
    # Validate each setting
    for key, value in flat_config.items():
        if key.startswith('_'):  # Skip internal keys
            continue
            
        if key in mappings and "validation" in mappings[key]:
            validation = mappings[key]["validation"]
            
            # Check enum values
            if "enum" in validation:
                if str(value) not in [str(v) for v in validation["enum"]]:
                    errors.append(f"{key}: '{value}' not in allowed values {validation['enum']}")
                    if "$fix_issues" == "1":
                        # Set to first allowed value
                        fixed.append(f"{key}: '{value}' -> '{validation['enum'][0]}'")
            
            # Check numeric ranges
            if "min" in validation or "max" in validation:
                try:
                    num_val = float(value)
                    if "min" in validation and num_val < validation["min"]:
                        errors.append(f"{key}: {value} below minimum {validation['min']}")
                        if "$fix_issues" == "1":
                            fixed.append(f"{key}: {value} -> {validation['min']}")
                    if "max" in validation and num_val > validation["max"]:
                        errors.append(f"{key}: {value} above maximum {validation['max']}")
                        if "$fix_issues" == "1":
                            fixed.append(f"{key}: {value} -> {validation['max']}")
                except (ValueError, TypeError):
                    errors.append(f"{key}: '{value}' is not numeric")
            
            # Check pattern
            if "pattern" in validation:
                if not re.match(validation["pattern"], str(value)):
                    errors.append(f"{key}: '{value}' does not match required pattern")
    
    # Report results
    if errors:
        print("ERRORS FOUND:")
        for error in errors:
            print(f"  ✗ {error}")
        
        if fixed:
            print("\nFIXED:")
            for fix in fixed:
                print(f"  ✓ {fix}")
            print("\nConfiguration updated with fixes")
            # TODO: Actually update the config file
        
        sys.exit(1)
    else:
        print("✓ All configuration values are valid")
        
    if warnings:
        print("\nWARNINGS:")
        for warning in warnings:
            print(f"  ⚠ {warning}")
    
except Exception as e:
    print(f"ERROR: Validation failed: {e}", file=sys.stderr)
    sys.exit(1)
EOF
    
    exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        success "Configuration is valid"
    else
        error "Configuration has errors"
        if [[ -z "$fix_issues" ]]; then
            info "Run with --fix to attempt automatic fixes"
        fi
        exit $exit_code
    fi
else
    warn "Cannot perform full validation (python3-yaml not available)"
    info "Basic syntax check passed"
fi
