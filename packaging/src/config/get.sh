
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


key="${args[key]}"
load_configuration

case "$key" in
  system.mode) echo "${config[system_mode]}" ;;
  system.camera_role) echo "${config[system_camera_role]}" ;;
  hardware.gpio_chip) echo "${config[hardware_gpio_chip]}" ;;
  network.broker_address) echo "${config[network_broker_address]}" ;;
  network.broker_port) echo "${config[network_broker_port]}" ;;
  storage.image_logging_dir) echo "${config[storage_image_logging_dir]}" ;;
  storage.web_share_dir) echo "${config[storage_web_share_dir]}" ;;
  cameras.slot1.type) echo "${config[camera_slot1_type]}" ;;
  cameras.slot2.type) echo "${config[camera_slot2_type]}" ;;
  simulators.e6_host) echo "${config[simulators_e6_host]}" ;;
  simulators.gspro_host) echo "${config[simulators_gspro_host]}" ;;
  *) error "Unknown configuration key: $key"; exit 1 ;;
esac
