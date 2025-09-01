initialize_global_flags


host="${args[--host]:-}"

echo "=== GSPro Server Test ==="
echo ""

setup_pitrac_environment

if [[ -n "$host" ]]; then
  echo "Testing connection to: $host"
  "$PITRAC_BINARY" --system_mode=test_gspro_server --gspro_host_address="$host" --logging_level=info
else
  echo "Testing with configured GSPro host"
  "$PITRAC_BINARY" --system_mode=test_gspro_server --logging_level=info
fi
