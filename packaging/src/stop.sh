initialize_global_flags


force_stop="${args[--force]:-}"
timeout="${args[--timeout]:-5}"

if ! validate_positive_integer "$timeout"; then
  log_error "Invalid timeout: $timeout"
  exit 1
fi

if [[ "$force_stop" == "1" ]]; then
  log_info "Force stopping PiTrac..."
  force_stop_pitrac
else
  log_info "Stopping PiTrac gracefully (timeout: ${timeout}s)..."
  stop_pitrac_gracefully "$timeout"
fi
