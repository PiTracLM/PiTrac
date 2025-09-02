initialize_global_flags


host="${args[--host]:-}"

echo "=== GSPro Server Test ==="
echo ""

setup_pitrac_environment

pitrac_args=()
build_pitrac_logging_args pitrac_args

if [[ -n "$host" ]]; then
  echo "Testing connection to: $host"
  "$PITRAC_BINARY" --system_mode=test_gspro_server --gspro_host_address="$host" "${pitrac_args[@]}"
else
  echo "Testing with configured GSPro host"
  "$PITRAC_BINARY" --system_mode=test_gspro_server "${pitrac_args[@]}"
fi