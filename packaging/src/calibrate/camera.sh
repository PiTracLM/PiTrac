
slot="${args[slot]}"
interactive="${args[--interactive]:-}"

echo "=== Camera $slot Calibration ==="
echo ""

setup_pitrac_environment

if [[ "$slot" == "1" ]]; then
  mode="camera1Calibrate"
elif [[ "$slot" == "2" ]]; then
  mode="camera2Calibrate"
else
  error "Invalid camera slot: $slot"
  exit 1
fi

"$PITRAC_BINARY" --system_mode="$mode" --logging_level=info "$@"
