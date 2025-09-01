
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


slot="${args[slot]}"

echo "=== Automatic Calibration - Camera $slot ==="
echo ""

setup_pitrac_environment

if [[ "$slot" == "1" ]]; then
  mode="camera1AutoCalibrate"
elif [[ "$slot" == "2" ]]; then
  mode="camera2AutoCalibrate"
else
  error "Invalid camera slot: $slot"
  exit 1
fi

"$PITRAC_BINARY" --system_mode="$mode" --logging_level=info "$@"
