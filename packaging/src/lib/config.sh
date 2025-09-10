#!/usr/bin/env bash
# lib/config.sh - Configuration management functions

readonly DEFAULT_CONFIG="/etc/pitrac/pitrac.yaml"
readonly USER_CONFIG="${HOME}/.pitrac/config/pitrac.yaml"
readonly GOLF_CONFIG="golf_sim_config.json"
readonly GOLF_CONFIG_TEMPLATE="/etc/pitrac/golf_sim_config.json"

declare -A config

load_configuration() {
  local config_file="${1:-$DEFAULT_CONFIG}"
  
  if [[ -f "$USER_CONFIG" ]]; then
    config_file="$USER_CONFIG"
  fi
  
  if [[ ! -f "$config_file" ]]; then
    error "Configuration file not found: $config_file"
    return 1
  fi
  
  log_debug "Loading configuration from: $config_file"
  
  if command -v yq >/dev/null 2>&1; then
    parse_yaml_with_yq "$config_file"
  elif command -v python3 >/dev/null 2>&1; then
    parse_yaml_with_python "$config_file"
  else
    parse_yaml_basic "$config_file"
  fi
}

parse_yaml_with_yq() {
  local config_file="$1"
  
  config[system_mode]=$(yq -r '.system.mode // "single"' "$config_file" 2>/dev/null || echo "single")
  config[system_camera_role]=$(yq -r '.system.camera_role // "camera1"' "$config_file" 2>/dev/null || echo "camera1")
  config[system_golfer_orientation]=$(yq -r '.system.golfer_orientation // "right_handed"' "$config_file" 2>/dev/null || echo "right_handed")
  config[hardware_gpio_chip]=$(yq -r '.hardware.gpio_chip // "auto"' "$config_file" 2>/dev/null || echo "auto")
  
  config[network_broker_address]=$(yq -r '.network.broker_address // ""' "$config_file" 2>/dev/null || echo "")
  config[network_broker_port]=$(yq -r '.network.broker_port // "61616"' "$config_file" 2>/dev/null || echo "61616")
  config[network_web_port]=$(yq -r '.network.web_port // "8080"' "$config_file" 2>/dev/null || echo "8080")
  
  config[storage_image_logging_dir]=$(yq -r '.storage.image_logging_dir // "~/LM_Shares/Images/"' "$config_file" 2>/dev/null || echo "~/LM_Shares/Images/")
  config[storage_web_share_dir]=$(yq -r '.storage.web_share_dir // "~/LM_Shares/WebShare/"' "$config_file" 2>/dev/null || echo "~/LM_Shares/WebShare/")
  config[storage_calibration_dir]=$(yq -r '.storage.calibration_dir // "~/.pitrac/calibration/"' "$config_file" 2>/dev/null || echo "~/.pitrac/calibration/")
  
  config[camera_slot1_type]=$(yq -r '.cameras.slot1.type // "4"' "$config_file" 2>/dev/null || echo "4")
  config[camera_slot1_lens]=$(yq -r '.cameras.slot1.lens // "1"' "$config_file" 2>/dev/null || echo "1")
  config[camera_slot2_type]=$(yq -r '.cameras.slot2.type // "4"' "$config_file" 2>/dev/null || echo "4")
  config[camera_slot2_lens]=$(yq -r '.cameras.slot2.lens // "1"' "$config_file" 2>/dev/null || echo "1")
  
  config[simulators_e6_host]=$(yq -r '.simulators.e6_host // ""' "$config_file" 2>/dev/null || echo "")
  config[simulators_gspro_host]=$(yq -r '.simulators.gspro_host // ""' "$config_file" 2>/dev/null || echo "")
  config[simulators_trugolf_host]=$(yq -r '.simulators.trugolf_host // ""' "$config_file" 2>/dev/null || echo "")
  
  expand_config_paths
}

parse_yaml_with_python() {
  local config_file="$1"
  
  local python_script='
import yaml
import sys

with open(sys.argv[1], "r") as f:
    data = yaml.safe_load(f)

def get_value(data, path, default=""):
    keys = path.split(".")
    value = data
    for key in keys:
        if isinstance(value, dict) and key in value:
            value = value[key]
        else:
            return default
    return value if value is not None else default

paths = {
    "system_mode": "system.mode",
    "system_camera_role": "system.camera_role",
    "system_golfer_orientation": "system.golfer_orientation",
    "hardware_gpio_chip": "hardware.gpio_chip",
    "network_broker_address": "network.broker_address",
    "network_broker_port": "network.broker_port",
    "network_web_port": "network.web_port",
    "storage_image_logging_dir": "storage.image_logging_dir",
    "storage_web_share_dir": "storage.web_share_dir",
    "storage_calibration_dir": "storage.calibration_dir",
    "camera_slot1_type": "cameras.slot1.type",
    "camera_slot1_lens": "cameras.slot1.lens",
    "camera_slot2_type": "cameras.slot2.type",
    "camera_slot2_lens": "cameras.slot2.lens",
    "simulators_e6_host": "simulators.e6_host",
    "simulators_gspro_host": "simulators.gspro_host",
    "simulators_trugolf_host": "simulators.trugolf_host"
}

defaults = {
    "system_mode": "single",
    "system_camera_role": "camera1",
    "system_golfer_orientation": "right_handed",
    "hardware_gpio_chip": "auto",
    "network_broker_address": "",
    "network_broker_port": "61616",
    "network_web_port": "8080",
    "storage_image_logging_dir": "~/LM_Shares/Images/",
    "storage_web_share_dir": "~/LM_Shares/WebShare/",
    "storage_calibration_dir": "~/.pitrac/calibration/",
    "camera_slot1_type": "4",
    "camera_slot1_lens": "1",
    "camera_slot2_type": "4",
    "camera_slot2_lens": "1"
}

for key, path in paths.items():
    value = get_value(data, path, defaults.get(key, ""))
    print(f"{key}={value}")
'
  
  while IFS='=' read -r key value; do
    config["$key"]="$value"
  done < <(python3 -c "$python_script" "$config_file")
  
  expand_config_paths
}

parse_yaml_basic() {
  local config_file="$1"
  
  parse_yaml_value() {
    local section="$1"
    local key="$2"
    local default="${3:-}"
    local lines_after="${4:-1}"
    
    local value=""
    if [[ -f "$config_file" ]]; then
      value=$(awk -v section="$section" -v key="$key" -v lines="$lines_after" '
        $0 ~ "^" section ":" { in_section = 1; next }
        in_section && NF > 0 && $0 !~ "^[[:space:]]" { in_section = 0 }
        in_section && $0 ~ "^[[:space:]]+" key ":" {
          sub(/^[[:space:]]+/, "")
          sub(key "[[:space:]]*:[[:space:]]*", "")
          sub(/[[:space:]]*#.*$/, "")
          gsub(/^"|"$/, "")
          print
          exit
        }
      ' "$config_file")
    fi
    
    echo "${value:-$default}"
  }
  
  config[system_mode]=$(parse_yaml_value "system" "mode" "single")
  config[system_camera_role]=$(parse_yaml_value "system" "camera_role" "camera1")
  config[system_golfer_orientation]=$(parse_yaml_value "system" "golfer_orientation" "right_handed")
  config[hardware_gpio_chip]=$(parse_yaml_value "hardware" "gpio_chip" "auto")
  config[network_broker_address]=$(parse_yaml_value "network" "broker_address" "")
  config[network_broker_port]=$(parse_yaml_value "network" "broker_port" "61616")
  config[storage_image_logging_dir]=$(parse_yaml_value "storage" "image_logging_dir" "~/LM_Shares/Images/")
  config[storage_web_share_dir]=$(parse_yaml_value "storage" "web_share_dir" "~/LM_Shares/WebShare/")
  config[storage_calibration_dir]=$(parse_yaml_value "storage" "calibration_dir" "~/.pitrac/calibration/")
  config[network_web_port]=$(parse_yaml_value "network" "web_port" "8080")
  config[camera_slot1_type]=$(parse_yaml_value "cameras" "type" "4" 3)
  config[camera_slot1_lens]=$(parse_yaml_value "cameras" "lens" "1" 3)
  config[camera_slot2_type]=$(parse_yaml_value "cameras" "type" "4" 6)
  config[camera_slot2_lens]=$(parse_yaml_value "cameras" "lens" "1" 6)
  config[simulators_e6_host]=$(parse_yaml_value "simulators" "e6_host" "")
  config[simulators_gspro_host]=$(parse_yaml_value "simulators" "gspro_host" "")
  config[simulators_trugolf_host]=$(parse_yaml_value "simulators" "trugolf_host" "")
  
  expand_config_paths
}

expand_config_paths() {
  local key
  for key in storage_image_logging_dir storage_web_share_dir storage_calibration_dir; do
    if [[ -n "${config[$key]}" ]]; then
      config[$key]="${config[$key]//\~/$HOME}"
    fi
  done
}

get_config() {
  local key="$1"
  local default="${2:-}"
  
  echo "${config[$key]:-$default}"
}

set_config() {
  local key="$1"
  local value="$2"
  
  config[$key]="$value"
}

get_system_mode() {
  local mode="${config[system_mode]:-single}"
  local role="${config[system_camera_role]:-camera1}"
  
  if [[ "$mode" == "dual" && "$role" == "camera2" ]]; then
    echo "camera2"
  else
    echo "camera1"
  fi
}

build_pitrac_arguments() {
  local -n args_ref=$1
  
  args_ref=("--system_mode=$(get_system_mode)")
  
  [[ -n "${config[system_golfer_orientation]}" ]] && \
    args_ref+=("--golfer_orientation=${config[system_golfer_orientation]}")
  
  if [[ -n "${config[network_broker_address]}" ]]; then
    local broker="${config[network_broker_address]}"
    if [[ "$broker" != *:* ]]; then
      broker="tcp://${broker}:${config[network_broker_port]:-61616}"
    fi
    args_ref+=("--msg_broker_address=$broker")
  fi
  
  [[ -n "${config[storage_image_logging_dir]}" ]] && \
    args_ref+=("--base_image_logging_dir=${config[storage_image_logging_dir]}")
  [[ -n "${config[storage_web_share_dir]}" ]] && \
    args_ref+=("--web_server_share_dir=${config[storage_web_share_dir]}")
  
  [[ -n "${config[simulators_e6_host]}" ]] && \
    args_ref+=("--e6_host_address=${config[simulators_e6_host]}")
  [[ -n "${config[simulators_gspro_host]}" ]] && \
    args_ref+=("--gspro_host_address=${config[simulators_gspro_host]}")
  
  return 0
}

ensure_golf_config() {
  if [[ ! -f "$GOLF_CONFIG" ]]; then
    if [[ -f "$GOLF_CONFIG_TEMPLATE" ]]; then
      log_info "Copying golf simulator configuration to current directory..."
      cp "$GOLF_CONFIG_TEMPLATE" .
      
      if command -v jq >/dev/null 2>&1; then
        jq ".logging.base_directory = \"${config[storage_image_logging_dir]}\"" "$GOLF_CONFIG" > "${GOLF_CONFIG}.tmp"
        mv "${GOLF_CONFIG}.tmp" "$GOLF_CONFIG"
      else
        sed -i.bak "s|~/|${HOME}/|g" "$GOLF_CONFIG"
        rm -f "${GOLF_CONFIG}.bak"
      fi
    else
      error "Golf simulator configuration not found at $GOLF_CONFIG_TEMPLATE"
      return 1
    fi
  fi
  
  return 0
}

validate_config() {
  local valid=true
  
  if [[ ! "${config[camera_slot1_type]}" =~ ^[1-5]$ ]]; then
    error "Invalid camera type for slot1: ${config[camera_slot1_type]} (must be 1-5)"
    valid=false
  fi
  
  if [[ ! "${config[camera_slot2_type]}" =~ ^[1-5]$ ]]; then
    error "Invalid camera type for slot2: ${config[camera_slot2_type]} (must be 1-5)"
    valid=false
  fi
  
  for key in storage_image_logging_dir storage_web_share_dir; do
    local dir="${config[$key]}"
    if [[ -n "$dir" ]]; then
      local parent
      parent=$(dirname "$dir")
      if [[ ! -d "$parent" ]]; then
        warn "Parent directory does not exist: $parent"
      fi
    fi
  done
  
  if [[ -n "${config[network_broker_address]}" ]]; then
    if [[ ! "${config[network_broker_address]}" =~ ^(tcp://)?[a-zA-Z0-9.-]+(:[0-9]+)?$ ]]; then
      error "Invalid broker address format: ${config[network_broker_address]}"
      valid=false
    fi
  fi
  
  if [[ "$valid" == "true" ]]; then
    return 0
  else
    return 1
  fi
}

reset_config() {
  local backup="${1:-true}"
  local config_file="${USER_CONFIG:-$DEFAULT_CONFIG}"
  
  if [[ "$backup" == "true" ]] && [[ -f "$config_file" ]]; then
    local backup_file="${config_file}.backup.$(date +%Y%m%d_%H%M%S)"
    cp "$config_file" "$backup_file"
    log_info "Configuration backed up to: $backup_file"
  fi
  
  if [[ -f "/usr/share/pitrac/config.yaml.default" ]]; then
    cp "/usr/share/pitrac/config.yaml.default" "$config_file"
    success "Configuration reset to defaults"
  else
    error "Default configuration not found"
    return 1
  fi
}
