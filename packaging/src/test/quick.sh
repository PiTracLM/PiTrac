initialize_global_flags


echo "=== Quick Image Processing Test ==="
echo "Testing image processing algorithms (no camera required)"
echo ""

if ! ensure_golf_config; then
  exit 1
fi

setup_pitrac_environment

echo "Processing test images..."
echo "================================"

pitrac_args=()
build_pitrac_logging_args pitrac_args

"$PITRAC_BINARY" --system_mode=test --send_test_results=1 --skip_wait_armed=1 "${pitrac_args[@]}" "$@"
echo "================================"
echo "Test complete. Check output for ball detection results."