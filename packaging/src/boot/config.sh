
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


model="${args[model]:-auto}"

if [[ "$model" == "auto" ]]; then
  model=$(detect_pi_model)
fi

config_file=$(get_boot_config_path "$model")

echo "=== Boot Configuration for $model ==="
echo ""
echo "Config file: $config_file"
echo ""
echo "Required settings:"
echo "  camera_auto_detect=1"
echo "  dtparam=i2c_arm=on"
echo "  dtparam=spi=on"

if needs_gpu_memory_setting "$model"; then
  echo "  gpu_mem=$(get_recommended_gpu_memory)"
fi

echo ""
echo "Edit with: sudo nano $config_file"
echo "Then reboot: sudo reboot"
