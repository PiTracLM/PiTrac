#!/usr/bin/env bash
# migrate-to-json.sh - Migrate YAML configuration to JSON format

set -euo pipefail

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../lib/logging.sh" || true

# Configuration paths
YAML_CONFIG="${HOME}/.pitrac/config/pitrac.yaml"
USER_YAML="/etc/pitrac/pitrac.yaml"
SYSTEM_JSON="/etc/pitrac/golf_sim_config.json"
USER_SETTINGS="${HOME}/.pitrac/config/user_settings.json"
BACKUP_DIR="${HOME}/.pitrac/backups"

# Initialize logging
initialize_global_flags() {
    export PITRAC_VERBOSE="${PITRAC_VERBOSE:-0}"
    export PITRAC_DEBUG="${PITRAC_DEBUG:-0}"
}

initialize_global_flags

# Check if YAML config exists
yaml_source=""
if [[ -f "$YAML_CONFIG" ]]; then
    yaml_source="$YAML_CONFIG"
    log_info "Found user YAML config: $YAML_CONFIG"
elif [[ -f "$USER_YAML" ]]; then
    yaml_source="$USER_YAML"
    log_info "Found system YAML config: $USER_YAML"
else
    log_info "No YAML configuration found. Creating empty user_settings.json"
    mkdir -p "$(dirname "$USER_SETTINGS")"
    echo "{}" > "$USER_SETTINGS"
    log_success "Created empty user_settings.json"
    exit 0
fi

# Create backup
mkdir -p "$BACKUP_DIR"
backup_file="${BACKUP_DIR}/pitrac.yaml.$(date +%Y%m%d_%H%M%S).backup"
cp "$yaml_source" "$backup_file"
log_info "Backed up YAML config to: $backup_file"

# Python script to migrate YAML to JSON
python3 << 'PYTHON_SCRIPT'
import json
import yaml
import sys
import os

def load_yaml_config(path):
    """Load YAML configuration file"""
    try:
        with open(path, 'r') as f:
            return yaml.safe_load(f) or {}
    except Exception as e:
        print(f"Error loading YAML: {e}", file=sys.stderr)
        return {}

def load_json_config(path):
    """Load JSON configuration file"""
    try:
        with open(path, 'r') as f:
            return json.load(f)
    except Exception as e:
        print(f"Error loading JSON: {e}", file=sys.stderr)
        return {}

def flatten_dict(d, parent_key='', sep='.'):
    """Flatten nested dictionary with dot notation keys"""
    items = []
    for k, v in d.items():
        new_key = f"{parent_key}{sep}{k}" if parent_key else k
        if isinstance(v, dict):
            items.extend(flatten_dict(v, new_key, sep=sep).items())
        else:
            items.append((new_key, v))
    return dict(items)

def unflatten_dict(d, sep='.'):
    """Unflatten dot notation keys back to nested dict"""
    result = {}
    for key, value in d.items():
        parts = key.split(sep)
        current = result
        for part in parts[:-1]:
            if part not in current:
                current[part] = {}
            current = current[part]
        current[parts[-1]] = value
    return result

def map_yaml_to_json_keys(yaml_config):
    """Map YAML keys to golf_sim_config.json structure"""
    mapped = {}
    
    # Direct mappings from YAML to JSON structure
    mappings = {
        # System settings
        'system.putting_mode': 'gs_config.modes.kStartInPuttingMode',
        
        # Camera settings
        'cameras.camera1_gain': 'gs_config.cameras.kCamera1Gain',
        'cameras.camera2_gain': 'gs_config.cameras.kCamera2Gain',
        
        # Simulator settings
        'simulators.e6_host': 'gs_config.golf_simulator_interfaces.E6.kE6ConnectAddress',
        'simulators.e6_port': 'gs_config.golf_simulator_interfaces.E6.kE6ConnectPort',
        'simulators.gspro_host': 'gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress',
        'simulators.gspro_port': 'gs_config.golf_simulator_interfaces.GSPro.kGSProConnectPort',
        
        # Ball detection
        'ball_detection.use_clahe': 'gs_config.ball_identification.kUseCLAHEProcessing',
        'ball_detection.clahe_clip_limit': 'gs_config.ball_identification.kCLAHEClipLimit',
        'ball_detection.ball_radius_pixels': 'gs_config.ball_position.kExpectedBallRadiusPixelsAt40cm',
        
        # Storage settings
        'storage.log_exposure_images': 'gs_config.logging.kLogIntermediateExposureImagesToFile',
        'storage.log_spin_images': 'gs_config.logging.kLogIntermediateSpinImagesToFile',
        'storage.log_webserver_images': 'gs_config.logging.kLogWebserverImagesToFile',
        'storage.unique_diagnostic_files': 'gs_config.logging.kLogDiagnosticImagesToUniqueFiles',
        'storage.image_dir': 'gs_config.logging.kLinuxBaseImageLoggingDir',
        'storage.web_share_dir': 'gs_config.user_interface.kWebServerTomcatShareDirectory',
        
        # Network settings
        'network.broker_address': 'gs_config.ipc_interface.kWebActiveMQHostAddress',
        'network.web_refresh_seconds': 'gs_config.user_interface.kRefreshTimeSeconds',
        
        # Spin settings
        'spin.skip_calculation': 'gs_config.golf_simulator_interfaces.kSkipSpinCalculation',
        'spin.write_csv_files': 'gs_config.spin_analysis.kWriteSpinAnalysisCsvFiles',
        
        # Advanced Hough settings
        'ball_detection_advanced.strobed_balls.canny_lower': 'gs_config.ball_identification.kStrobedBallsCannyLower',
        'ball_detection_advanced.strobed_balls.canny_upper': 'gs_config.ball_identification.kStrobedBallsCannyUpper',
        'ball_detection_advanced.strobed_balls.min_circles': 'gs_config.ball_identification.kStrobedBallsMinHoughReturnCircles',
        'ball_detection_advanced.strobed_balls.max_circles': 'gs_config.ball_identification.kStrobedBallsMaxHoughReturnCircles',
    }
    
    # Flatten YAML config
    flat_yaml = flatten_dict(yaml_config)
    
    # Apply mappings
    for yaml_key, json_key in mappings.items():
        if yaml_key in flat_yaml:
            value = flat_yaml[yaml_key]
            # Convert boolean values to "0"/"1" strings as expected by C++
            if isinstance(value, bool):
                value = "1" if value else "0"
            elif value is not None:
                value = str(value)
            
            # Set the value in mapped dict using nested structure
            parts = json_key.split('.')
            current = mapped
            for part in parts[:-1]:
                if part not in current:
                    current[part] = {}
                current = current[part]
            current[parts[-1]] = value
    
    return mapped

def extract_differences(user_config, defaults):
    """Extract only values that differ from defaults"""
    differences = {}
    
    def compare_nested(user, default, path=''):
        for key, user_value in user.items():
            current_path = f"{path}.{key}" if path else key
            
            if key not in default:
                # New key not in defaults
                if path not in differences:
                    differences[path] = {}
                differences[path][key] = user_value
            elif isinstance(user_value, dict) and isinstance(default.get(key), dict):
                # Recursive comparison for nested dicts
                nested_diff = {}
                compare_nested(user_value, default[key], current_path)
            elif user_value != default.get(key):
                # Value differs from default
                parts = current_path.split('.')
                current = differences
                for part in parts[:-1]:
                    if part not in current:
                        current[part] = {}
                    current = current[part]
                current[parts[-1]] = user_value
    
    compare_nested(user_config, defaults)
    return differences

# Main migration
yaml_source = os.environ.get('yaml_source', '')
system_json = os.environ.get('SYSTEM_JSON', '/etc/pitrac/golf_sim_config.json')
user_settings = os.environ.get('USER_SETTINGS', os.path.expanduser('~/.pitrac/config/user_settings.json'))

if not yaml_source:
    print("Error: No YAML source file specified", file=sys.stderr)
    sys.exit(1)

# Load configurations
yaml_config = load_yaml_config(yaml_source)
system_defaults = load_json_config(system_json)

# Map YAML to JSON structure
mapped_config = map_yaml_to_json_keys(yaml_config)

# Extract only differences from defaults
user_overrides = extract_differences(mapped_config, system_defaults)

# Ensure directory exists
os.makedirs(os.path.dirname(user_settings), exist_ok=True)

# Write user settings
with open(user_settings, 'w') as f:
    json.dump(user_overrides, f, indent=2, sort_keys=True)

# Report results
num_overrides = sum(1 for _ in str(user_overrides).split('"') if ':' in _) // 2
print(f"Successfully migrated {num_overrides} user settings to JSON format")

PYTHON_SCRIPT

# Export variables for Python script
export yaml_source="$yaml_source"
export SYSTEM_JSON="$SYSTEM_JSON"
export USER_SETTINGS="$USER_SETTINGS"

# Run migration
if python3 -c "$python_script" 2>/dev/null; then
    log_success "Migration complete! User settings saved to: $USER_SETTINGS"
    log_info "Original YAML backed up to: $backup_file"
    
    # Show what was migrated
    if [[ -f "$USER_SETTINGS" ]]; then
        num_settings=$(grep -c '":' "$USER_SETTINGS" 2>/dev/null || echo "0")
        log_info "Migrated $num_settings custom settings"
    fi
else
    log_error "Migration failed. Please check the error messages above."
    exit 1
fi