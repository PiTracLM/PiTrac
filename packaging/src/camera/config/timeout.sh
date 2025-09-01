
# Source libraries
source "${BASH_SOURCE%/*}/../../lib/logging.sh"
source "${BASH_SOURCE%/*}/../../lib/global_flags.sh"

# Initialize global flags and logging
initialize_global_flags

timeout_ms="${args[milliseconds]}"

config_file=$(get_libcamera_config_path)

echo "Setting camera timeout to ${timeout_ms}ms"
echo ""
echo "Add to $config_file:"
echo "  camera_timeout_value_ms: $timeout_ms"
echo ""
echo "Manual edit required:"
echo "  sudo nano $config_file"
