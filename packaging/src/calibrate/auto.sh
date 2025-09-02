initialize_global_flags


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

pitrac_args=()
build_pitrac_logging_args pitrac_args

"$PITRAC_BINARY" --system_mode="$mode" "${pitrac_args[@]}" "$@"