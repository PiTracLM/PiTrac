
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


show_effective="${args[--effective]:-}"

if [[ "$show_effective" == "1" ]]; then
  # Show effective configuration (with defaults applied)
  load_configuration
  
  echo "=== Effective Configuration ==="
  echo ""
  echo "System:"
  echo "  Mode: ${config[system_mode]}"
  echo "  Camera Role: ${config[system_camera_role]}"
  echo ""
  echo "Hardware:"
  echo "  GPIO Chip: ${config[hardware_gpio_chip]}"
  echo ""
  echo "Network:"
  echo "  Broker Address: ${config[network_broker_address]}"
  echo "  Broker Port: ${config[network_broker_port]}"
  echo ""
  echo "Storage:"
  echo "  Image Logging Dir: ${config[storage_image_logging_dir]}"
  echo "  Web Share Dir: ${config[storage_web_share_dir]}"
  echo ""
  echo "Cameras:"
  echo "  Slot 1: ${config[camera_slot1_type]}"
  echo "  Slot 2: ${config[camera_slot2_type]}"
  echo ""
  echo "Simulators:"
  echo "  E6 Host: ${config[simulators_e6_host]}"
  echo "  GSPro Host: ${config[simulators_gspro_host]}"
else
  # Show raw configuration file
  config_file="${USER_CONFIG:-$DEFAULT_CONFIG}"
  
  if [[ -f "$config_file" ]]; then
    echo "=== Configuration File: $config_file ==="
    echo ""
    cat "$config_file"
  else
    error "Configuration file not found: $config_file"
    exit 1
  fi
fi
