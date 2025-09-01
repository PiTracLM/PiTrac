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
"$PITRAC_BINARY" --system_mode=test --logging_level=info "$@"
echo "================================"
echo "Test complete. Check output for ball detection results."
