initialize_global_flags


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

setup_pitrac_environment

pitrac_args=()
build_pitrac_logging_args pitrac_args

"$PITRAC_BINARY" --pulse_test --system_mode=camera1 "${pitrac_args[@]}"