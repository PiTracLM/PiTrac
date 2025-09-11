initialize_global_flags

system_mode="${args[--system-mode]:-camera1}"
foreground="${args[--foreground]:-}"
msg_broker_address="${args[--msg-broker-address]:-}"
base_image_logging_dir="${args[--base-image-logging-dir]:-}"
web_server_share_dir="${args[--web-server-share-dir]:-}"
e6_host_address="${args[--e6-host-address]:-}"
gspro_host_address="${args[--gspro-host-address]:-}"

if ! validate_system_mode "$system_mode"; then
  log_error "Invalid system mode: $system_mode"
  exit 1
fi

if is_pitrac_running; then
  log_error "PiTrac is already running (PID: $(cat "$PITRAC_PID_FILE"))"
  exit 1
fi

ensure_golf_config

# Source the JSON config library if not already loaded
if ! declare -f get_config_value >/dev/null 2>&1; then
  source "$PITRAC_LIB_DIR/config_json.sh" || true
fi

# Export camera configuration from JSON config
export_config_env

setup_pitrac_environment

cmd_args=("--system_mode=$system_mode")

build_pitrac_logging_args cmd_args

[[ -n "$msg_broker_address" ]] && cmd_args+=("--msg_broker_address=$msg_broker_address")
[[ -n "$base_image_logging_dir" ]] && cmd_args+=("--base_image_logging_dir=$base_image_logging_dir")
[[ -n "$web_server_share_dir" ]] && cmd_args+=("--web_server_share_dir=$web_server_share_dir")
[[ -n "$e6_host_address" ]] && cmd_args+=("--e6_host_address=$e6_host_address")
[[ -n "$gspro_host_address" ]] && cmd_args+=("--gspro_host_address=$gspro_host_address")

if [[ "$foreground" == "1" ]]; then
  log_info "Starting PiTrac launch monitor (foreground)..."
  log_info "Press Ctrl+C to stop"
  exec "$PITRAC_BINARY" "${cmd_args[@]}"
else
  log_info "Starting PiTrac launch monitor (background)..."
  log_info "Use 'pitrac status' to check status, 'pitrac logs' to view output"
  
  ensure_pid_directory
  ensure_log_directory
  
  nohup "$PITRAC_BINARY" "${cmd_args[@]}" > "$PITRAC_LOG_FILE" 2>&1 &
  local pid=$!
  echo $pid > "$PITRAC_PID_FILE"
  
  sleep 2
  if kill -0 $pid 2>/dev/null; then
    log_info "PiTrac started successfully (PID: $pid)"
  else
    log_error "Failed to start PiTrac. Check logs with 'pitrac logs'"
    rm -f "$PITRAC_PID_FILE"
    exit 1
  fi
fi
