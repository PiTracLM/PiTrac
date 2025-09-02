initialize_global_flags


echo "=== Spin Detection Test ==="
echo "Testing spin algorithms (no camera required)"
echo ""

ensure_golf_config
setup_pitrac_environment

echo "Processing spin detection..."

pitrac_args=()
build_pitrac_logging_args pitrac_args

"$PITRAC_BINARY" --system_mode=test_spin "${pitrac_args[@]}" "$@"
echo "Spin test complete."