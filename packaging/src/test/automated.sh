initialize_global_flags


suite="${args[--suite]:-basic}"

echo "=== Automated Test Suite ==="
echo "Running: $suite suite"
echo ""

ensure_golf_config
setup_pitrac_environment

"$PITRAC_BINARY" --system_mode=automated_testing --logging_level=info "$@"
echo "Test suite complete."
