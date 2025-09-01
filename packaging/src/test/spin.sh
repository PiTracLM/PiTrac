initialize_global_flags


echo "=== Spin Detection Test ==="
echo "Testing spin algorithms (no camera required)"
echo ""

ensure_golf_config
setup_pitrac_environment

echo "Processing spin detection..."
"$PITRAC_BINARY" --system_mode=test_spin --logging_level=info "$@"
echo "Spin test complete."
