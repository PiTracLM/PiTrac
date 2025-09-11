initialize_global_flags


echo "=== Spin Detection Test ==="
echo "Testing spin algorithms (no camera required)"
echo ""

ensure_golf_config

# Source the JSON config library if not already loaded
if ! declare -f get_config_value >/dev/null 2>&1; then
  source "$PITRAC_LIB_DIR/config_json.sh" || true
fi

# Export camera configuration from JSON config
export_config_env

setup_pitrac_environment

echo "Processing spin detection..."

pitrac_args=()
build_pitrac_logging_args pitrac_args

"$PITRAC_BINARY" --system_mode=test_spin "${pitrac_args[@]}" "$@"
echo "Spin test complete."