initialize_global_flags


suite="${args[--suite]:-basic}"

echo "=== Automated Test Suite ==="
echo "Running: $suite suite"
echo ""

ensure_golf_config

# Source the JSON config library if not already loaded
if ! declare -f get_config_value >/dev/null 2>&1; then
  source "$PITRAC_LIB_DIR/config_json.sh" || true
fi

# Export camera configuration from JSON config
export_config_env

setup_pitrac_environment

pitrac_args=()
build_pitrac_logging_args pitrac_args

"$PITRAC_BINARY" --system_mode=automated_testing "${pitrac_args[@]}" "$@"
echo "Test suite complete."