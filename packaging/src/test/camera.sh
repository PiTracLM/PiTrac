initialize_global_flags


slot="${args[slot]}"

echo "=== Camera $slot Test ==="
echo "Testing camera $slot standalone"
echo ""

ensure_golf_config
setup_pitrac_environment

if [[ "$slot" == "1" ]]; then
  mode="camera1_test_standalone"
elif [[ "$slot" == "2" ]]; then
  mode="camera2_test_standalone"
else
  error "Invalid camera slot: $slot"
  exit 1
fi

pitrac_args=()
build_pitrac_logging_args pitrac_args

"$PITRAC_BINARY" --system_mode="$mode" "${pitrac_args[@]}" "$@"