#!/usr/bin/env bash
# lib/process.sh - Process management functions

PITRAC_PID_DIR="/var/run/pitrac"
PITRAC_PID_FILE="${PITRAC_PID_DIR}/pitrac.pid"
PITRAC_LOCK_FILE="${PITRAC_PID_DIR}/pitrac.lock"
PITRAC_LOG_FILE="/var/log/pitrac/pitrac.log"

ensure_pid_directory() {
  if [[ ! -d "$PITRAC_PID_DIR" ]]; then
    if [[ $EUID -eq 0 ]]; then
      mkdir -p "$PITRAC_PID_DIR"
      chown "${SUDO_USER:-$USER}:${SUDO_USER:-$USER}" "$PITRAC_PID_DIR"
    else
      PITRAC_PID_DIR="${HOME}/.pitrac/run"
      PITRAC_PID_FILE="${PITRAC_PID_DIR}/pitrac.pid"
      PITRAC_LOCK_FILE="${PITRAC_PID_DIR}/pitrac.lock"
      mkdir -p "$PITRAC_PID_DIR"
    fi
  fi
}

is_pitrac_running() {
  local pid
  
  if [[ -f "$PITRAC_PID_FILE" ]]; then
    pid=$(cat "$PITRAC_PID_FILE" 2>/dev/null)
    if [[ -n "$pid" ]] && is_process_running "$pid" "pitrac_lm"; then
      return 0
    else
      rm -f "$PITRAC_PID_FILE"
    fi
  fi
  
  pid=$(pgrep -x "pitrac_lm" 2>/dev/null | head -1)
  if [[ -n "$pid" ]]; then
    warn "Found pitrac_lm running without PID file (PID: $pid)"
    return 0
  fi
  
  return 1
}

is_process_running() {
  local pid="$1"
  local expected_name="${2:-}"
  
  if [[ -z "$pid" ]]; then
    return 1
  fi
  
  if ! kill -0 "$pid" 2>/dev/null; then
    return 1
  fi
  
  if [[ -n "$expected_name" ]]; then
    local actual_name
    actual_name=$(ps -p "$pid" -o comm= 2>/dev/null)
    if [[ "$actual_name" != "$expected_name" ]]; then
      return 1
    fi
  fi
  
  return 0
}

get_pitrac_pid() {
  if [[ -f "$PITRAC_PID_FILE" ]]; then
    cat "$PITRAC_PID_FILE" 2>/dev/null
  else
    pgrep -x "pitrac_lm" 2>/dev/null | head -1
  fi
}

save_pid() {
  local pid="$1"
  ensure_pid_directory
  
  echo "$pid" > "${PITRAC_PID_FILE}.tmp"
  mv -f "${PITRAC_PID_FILE}.tmp" "$PITRAC_PID_FILE"
}

remove_pid_file() {
  rm -f "$PITRAC_PID_FILE"
}

start_background_process() {
  local binary="$1"
  shift
  local -a args=("$@")
  
  ensure_pid_directory
  ensure_log_directory
  
  (
    exec 1>>"$PITRAC_LOG_FILE"
    exec 2>&1
    
    setsid "$binary" "${args[@]}" &
    echo $!
  ) &
  
  local pid=$!
  wait $pid 2>/dev/null
  local background_pid=$(cat)
  echo "$background_pid"
}

exec_with_cleanup() {
  local binary="$1"
  shift
  local -a args=("$@")
  
  trap 'cleanup_on_exit' EXIT
  trap 'handle_interrupt' INT TERM
  
  exec "$binary" "${args[@]}"
}

cleanup_on_exit() {
  remove_pid_file
  log_info "PiTrac stopped"
}

handle_interrupt() {
  log_info "Received interrupt signal, shutting down..."
  cleanup_on_exit
  exit 130
}

stop_pitrac() {
  local force="${1:-false}"
  local timeout="${2:-5}"
  local pid
  
  pid=$(get_pitrac_pid)
  
  if [[ -z "$pid" ]]; then
    log_info "PiTrac is not running"
    return 0
  fi
  
  if ! is_process_running "$pid" "pitrac_lm"; then
    log_info "PiTrac process not found (stale PID file)"
    remove_pid_file
    return 0
  fi
  
  log_info "Stopping PiTrac (PID: $pid)..."
  
  if [[ "$force" == "true" ]]; then
    kill -9 "$pid" 2>/dev/null
  else
    kill -TERM "$pid" 2>/dev/null
    
    local count=0
    while [[ $count -lt $timeout ]] && is_process_running "$pid"; do
      sleep 1
      ((count++))
    done
    
    if is_process_running "$pid"; then
      log_warn "Process didn't stop gracefully, forcing..."
      kill -9 "$pid" 2>/dev/null
    fi
  fi
  
  if ! is_process_running "$pid"; then
    remove_pid_file
    success "PiTrac stopped"
    return 0
  else
    error "Failed to stop PiTrac"
    return 1
  fi
}

ensure_log_directory() {
  local log_dir
  log_dir=$(dirname "$PITRAC_LOG_FILE")
  
  if [[ ! -d "$log_dir" ]]; then
    if [[ $EUID -eq 0 ]]; then
      mkdir -p "$log_dir"
      chown "${SUDO_USER:-$USER}:${SUDO_USER:-$USER}" "$log_dir"
    else
      PITRAC_LOG_FILE="${HOME}/.pitrac/logs/pitrac.log"
      log_dir=$(dirname "$PITRAC_LOG_FILE")
      mkdir -p "$log_dir"
    fi
  fi
  
  if [[ -f "$PITRAC_LOG_FILE" ]]; then
    local size
    size=$(stat -f%z "$PITRAC_LOG_FILE" 2>/dev/null || stat -c%s "$PITRAC_LOG_FILE" 2>/dev/null)
    if [[ $size -gt 10485760 ]]; then
      mv "$PITRAC_LOG_FILE" "${PITRAC_LOG_FILE}.old"
      touch "$PITRAC_LOG_FILE"
    fi
  fi
}

acquire_lock() {
  ensure_pid_directory
  
  exec 200>"$PITRAC_LOCK_FILE"
  
  if ! flock -n 200; then
    error "Another instance is starting or stopping PiTrac"
    return 1
  fi
  
  return 0
}

release_lock() {
  exec 200>&-
}

not_already_running() {
  if is_pitrac_running; then
    error "PiTrac is already running (PID: $(get_pitrac_pid))"
    error "Use 'pitrac stop' to stop it first"
    exit 1
  fi
}
