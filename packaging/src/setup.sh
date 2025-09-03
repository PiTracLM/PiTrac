initialize_global_flags


pi_model="${args[--pi-model]:-auto}"
skip_reboot="${args[--skip-reboot]:-}"

echo "=== PiTrac Initial Setup ==="
echo ""

if [[ "$pi_model" == "auto" ]]; then
  pi_model=$(detect_pi_model)
  echo "Detected Pi model: $pi_model"
else
  echo "Using specified Pi model: $pi_model"
fi

echo ""
echo "Creating directory structure..."
ensure_directories
log_info "Directories created"

user_config_dir="${HOME}/.pitrac/config"
user_config_file="${user_config_dir}/pitrac.yaml"

if [[ ! -f "$user_config_file" ]] && [[ -f "/etc/pitrac/pitrac.yaml" ]]; then
  echo "Creating user configuration..."
  cp "/etc/pitrac/pitrac.yaml" "$user_config_file"
  log_info "User configuration created"
fi

echo ""
echo "=== Boot Configuration ==="
echo "Edit $(get_boot_config_path) and add:"
echo "  camera_auto_detect=1"
echo "  dtparam=i2c_arm=on"
echo "  dtparam=spi=on"

if needs_gpu_memory_setting "$pi_model"; then
  echo "  gpu_mem=$(get_recommended_gpu_memory)"
fi

if is_single_pi ; then
  echo "  # Setup camera on slot 0 to be internally triggered, and on slot 1 to be externally triggered"
  echo "  dtoverlay=imx296,sync-sink=1 "
  echo "  dtoverlay=imx296,cam0"
else
  echo "<no other camera-related parameters need to be set in config.sys>"
fi

echo ""
echo "=== Current Status ==="

if is_gpio_available; then
  success "GPIO available (chip $(get_gpio_chip))"
else
  warn "GPIO not available"
fi

if check_camera_availability; then
  success "Camera(s) detected"
else
  warn "No cameras detected"
fi

if [[ "$skip_reboot" != "1" ]]; then
  echo ""
  echo "Setup complete. Reboot required."
  echo "Run: sudo reboot"
fi
