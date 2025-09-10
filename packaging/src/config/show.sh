if ! declare -f initialize_global_flags >/dev/null 2>&1; then
  echo "Error: Required functions not found. Regenerate the CLI." >&2
  exit 1
fi

if ! declare -p args >/dev/null 2>&1; then
  declare -gA args
fi

show_config_value() {
  local label="$1"
  local key="$2"
  local default_value="$3"
  local actual_value="${config[$key]:-$default_value}"
  local source=""
  
  # Determine source of the value
  # Convert key like "system_golfer_orientation" to search for "golfer_orientation:" under "system:"
  local yaml_key=""
  case "$key" in
    system_*)
      yaml_key="${key#system_}"
      if [[ -f "$USER_CONFIG" ]] && grep -A10 "^system:" "$USER_CONFIG" 2>/dev/null | grep -q "^\s*${yaml_key}:" 2>/dev/null; then
        source="[user]"
      elif [[ -n "${config[$key]}" ]]; then
        source="[system]"
      else
        source="[default]"
      fi
      ;;
    storage_*)
      yaml_key="${key#storage_}"
      yaml_key="${yaml_key//_/_}"
      if [[ -f "$USER_CONFIG" ]] && grep -A10 "^storage:" "$USER_CONFIG" 2>/dev/null | grep -q "^\s*${yaml_key}:" 2>/dev/null; then
        source="[user]"
      elif [[ -n "${config[$key]}" ]]; then
        source="[system]"
      else
        source="[default]"
      fi
      ;;
    *)
      if [[ -n "${config[$key]}" ]]; then
        if [[ -f "$USER_CONFIG" ]] && grep -q "${key#*_}" "$USER_CONFIG" 2>/dev/null; then
          source="[user]"
        else
          source="[system]"
        fi
      else
        source="[default]"
      fi
      ;;
  esac
  
  echo "  $label: $actual_value $source"
}

initialize_global_flags


show_yaml="${args[--yaml]:-}"

if [[ "$show_yaml" == "1" ]]; then
  # Show raw configuration file
  config_file="${USER_CONFIG:-$DEFAULT_CONFIG}"
  
  if [[ -f "$config_file" ]]; then
    echo "=== Configuration File: $config_file ==="
    echo ""
    cat "$config_file"
  else
    log_error "Configuration file not found: $config_file"
    exit 1
  fi
else
  load_configuration
  
  # Build the actual command arguments that would be passed to pitrac_lm
  cmd_args=()
  build_pitrac_arguments cmd_args
  
  build_pitrac_logging_args cmd_args
  
  echo "=== PiTrac Runtime Configuration ==="
  echo ""
  echo "# This shows what will actually be used when PiTrac runs"
  echo "# Config file: ${USER_CONFIG:-$DEFAULT_CONFIG}"
  echo ""
  
  echo "## System:"
  mode_value="${config[system_mode]:-single}"
  binary_mode="$(get_system_mode)"
  
  if [[ -f "$USER_CONFIG" ]] && grep -A10 "^system:" "$USER_CONFIG" 2>/dev/null | grep -q "^\s*mode:" 2>/dev/null; then
    mode_source="[user]"
  elif [[ -n "${config[system_mode]}" ]]; then
    mode_source="[system]"
  else
    mode_source="[default]"
  fi
  
  echo "  Mode: ${mode_value} ${mode_source}"
  if [[ "$mode_value" == "dual" ]]; then
    echo "    (runs as: ${binary_mode})"
  fi
  
  show_config_value "Camera Role" "system_camera_role" "camera1"
  show_config_value "Golfer Orientation" "system_golfer_orientation" "right_handed"
  echo ""
  
  echo "## Hardware:"
  show_config_value "GPIO Chip" "hardware_gpio_chip" "auto"
  echo ""
  
  echo "## Network:"
  broker="${config[network_broker_address]:-}"
  port="${config[network_broker_port]:-61616}"
  if [[ -n "$broker" ]]; then
    if [[ "$broker" != *:* ]]; then
      broker="tcp://${broker}:${port}"
    fi
    echo "  Message Broker: ${broker} [configured]"
  else
    echo "  Message Broker: not configured [default]"
  fi
  show_config_value "Web Port" "network_web_port" "8080"
  echo ""
  
  echo "## Storage:"
  show_config_value "Image Logging" "storage_image_logging_dir" "~/LM_Shares/Images/"
  show_config_value "Web Share" "storage_web_share_dir" "~/LM_Shares/WebShare/"
  show_config_value "Calibration" "storage_calibration_dir" "~/.pitrac/calibration/"
  echo ""
  
  echo "## Cameras:"
  slot1_type="${config[camera_slot1_type]:-4}"
  slot1_lens="${config[camera_slot1_lens]:-1}"
  
  if [[ -f "$USER_CONFIG" ]] && grep -q "^\s*slot1:\s*$" "$USER_CONFIG" 2>/dev/null && \
     grep -A2 "^\s*slot1:\s*$" "$USER_CONFIG" | grep -q "^\s*type:" 2>/dev/null; then
    slot1_source_type="[user]"
  elif [[ -n "${config[camera_slot1_type]}" ]]; then
    slot1_source_type="[system]"
  else
    slot1_source_type="[default]"
  fi
  
  if [[ -f "$USER_CONFIG" ]] && grep -q "^\s*slot1:\s*$" "$USER_CONFIG" 2>/dev/null && \
     grep -A3 "^\s*slot1:\s*$" "$USER_CONFIG" | grep -q "^\s*lens:" 2>/dev/null; then
    slot1_source_lens="[user]"
  elif [[ -n "${config[camera_slot1_lens]}" ]]; then
    slot1_source_lens="[system]"
  else
    slot1_source_lens="[default]"
  fi
  
  echo "  Slot 1: Type=${slot1_type} ${slot1_source_type}, Lens=${slot1_lens} ${slot1_source_lens}"
  
  slot2_type="${config[camera_slot2_type]:-4}"
  slot2_lens="${config[camera_slot2_lens]:-1}"
  
  if [[ -f "$USER_CONFIG" ]] && grep -q "^\s*slot2:\s*$" "$USER_CONFIG" 2>/dev/null && \
     grep -A2 "^\s*slot2:\s*$" "$USER_CONFIG" | grep -q "^\s*type:" 2>/dev/null; then
    slot2_source_type="[user]"
  elif [[ -n "${config[camera_slot2_type]}" ]]; then
    slot2_source_type="[system]"
  else
    slot2_source_type="[default]"
  fi
  
  if [[ -f "$USER_CONFIG" ]] && grep -q "^\s*slot2:\s*$" "$USER_CONFIG" 2>/dev/null && \
     grep -A3 "^\s*slot2:\s*$" "$USER_CONFIG" | grep -q "^\s*lens:" 2>/dev/null; then
    slot2_source_lens="[user]"
  elif [[ -n "${config[camera_slot2_lens]}" ]]; then
    slot2_source_lens="[system]"
  else
    slot2_source_lens="[default]"
  fi
  
  echo "  Slot 2: Type=${slot2_type} ${slot2_source_type}, Lens=${slot2_lens} ${slot2_source_lens}"
  echo ""
  
  echo "## Simulators:"
  if [[ -n "${config[simulators_e6_host]}" ]]; then
    echo "  E6 Host: ${config[simulators_e6_host]} [configured]"
  else
    echo "  E6 Host: not configured [default]"
  fi
  if [[ -n "${config[simulators_gspro_host]}" ]]; then
    echo "  GSPro Host: ${config[simulators_gspro_host]} [configured]"
  else
    echo "  GSPro Host: not configured [default]"
  fi
  if [[ -n "${config[simulators_trugolf_host]}" ]]; then
    echo "  TruGolf Host: ${config[simulators_trugolf_host]} [configured]"
  else
    echo "  TruGolf Host: not configured [default]"
  fi
  echo ""
  
  echo "## Command-line arguments to pitrac_lm:"
  echo "# Note: system.mode gets translated to --system_mode=camera1/camera2"
  if [[ ${#cmd_args[@]} -gt 0 ]]; then
    for arg in "${cmd_args[@]}"; do
      echo "  $arg"
    done
  else
    echo "  (none)"
  fi
  echo ""
  
  echo "## Additional Files:"
  echo "  Golf Config: /etc/pitrac/golf_sim_config.json"
  echo "  Binary: /usr/lib/pitrac/pitrac_lm"
fi
