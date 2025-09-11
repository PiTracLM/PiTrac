initialize_global_flags

# Source the JSON config library if not already loaded
if ! declare -f get_config_value >/dev/null 2>&1; then
  source "$PITRAC_LIB_DIR/config_json.sh" || true
fi

duration="${args[--duration]:-0}"

echo "=== Strobe Pulse Test ==="
echo "WARNING: Look at LED from at least 2 feet away!"
echo "You should see dark-reddish pulses"
echo ""

if [[ "$duration" == "0" ]]; then
  echo "Press Ctrl+C to stop"
else
  echo "Running for ${duration} seconds"
fi

# Ensure golf_sim_config.json is available
if ! ensure_golf_config; then
  exit 1
fi

setup_pitrac_environment

# Export camera configuration from JSON config
export_config_env

pitrac_args=()
build_pitrac_logging_args pitrac_args

"$PITRAC_BINARY" --pulse_test --system_mode=camera1 "${pitrac_args[@]}"