initialize_global_flags


suite="${args[--suite]:-basic}"

echo "=== Automated Test Suite ==="
echo "Running: $suite suite"
echo ""

ensure_golf_config
setup_pitrac_environment

pitrac_args=()
build_pitrac_logging_args pitrac_args

"$PITRAC_BINARY" --system_mode=automated_testing "${pitrac_args[@]}" "$@"
echo "Test suite complete."