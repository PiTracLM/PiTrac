#!/usr/bin/env bash
# lib/services.sh - Service management functions

is_service_running() {
  local service="$1"

  if command -v systemctl >/dev/null 2>&1; then
    if systemctl is-active --quiet "$service" 2>/dev/null; then
      return 0
    fi
  fi

  case "$service" in
    activemq)
      pgrep -f "activemq" >/dev/null 2>&1
      ;;
    pitrac)
      pgrep -x "pitrac_lm" >/dev/null 2>&1
      ;;
    *)
      return 1
      ;;
  esac
}

start_service() {
  local service="$1"
  local use_sudo="${2:-auto}"

  if [[ "$use_sudo" == "auto" ]]; then
    if [[ $EUID -eq 0 ]]; then
      use_sudo="no"
    elif systemctl show "$service" --property=User 2>/dev/null | grep -q "User=.*${USER}"; then
      use_sudo="no"
    else
      use_sudo="yes"
    fi
  fi

  if command -v systemctl >/dev/null 2>&1; then
    if [[ "$use_sudo" == "yes" ]]; then
      if command -v sudo >/dev/null 2>&1; then
        if ! sudo systemctl start "$service" 2>&1; then
          log_warn "Failed to start $service - it may already be running or misconfigured"
        fi
      else
        log_error "sudo required to start $service but not available"
        return 1
      fi
    else
      if ! systemctl start "$service" 2>&1; then
        log_warn "Failed to start $service - it may already be running or misconfigured"
      fi
    fi
  else
    log_error "systemctl not available, cannot start $service"
    return 1
  fi

  sleep 1

  if is_service_running "$service"; then
    return 0
  else
    return 1
  fi
}

stop_service() {
  local service="$1"
  local use_sudo="${2:-auto}"

  if [[ "$use_sudo" == "auto" ]]; then
    if [[ $EUID -eq 0 ]]; then
      use_sudo="no"
    elif systemctl show "$service" --property=User 2>/dev/null | grep -q "User=.*${USER}"; then
      use_sudo="no"
    else
      use_sudo="yes"
    fi
  fi

  if command -v systemctl >/dev/null 2>&1; then
    if [[ "$use_sudo" == "yes" ]]; then
      if command -v sudo >/dev/null 2>&1; then
        sudo systemctl stop "$service" 2>/dev/null
      else
        log_error "sudo required to stop $service but not available"
        return 1
      fi
    else
      systemctl stop "$service" 2>/dev/null
    fi
  else
    log_error "systemctl not available, cannot stop $service"
    return 1
  fi

  sleep 1

  if ! is_service_running "$service"; then
    return 0
  else
    return 1
  fi
}

restart_service() {
  local service="$1"
  local use_sudo="${2:-auto}"

  stop_service "$service" "$use_sudo"
  sleep 1
  start_service "$service" "$use_sudo"
}

enable_service() {
  local service="$1"
  local use_sudo="${2:-auto}"

  if [[ "$use_sudo" == "auto" ]]; then
    if [[ $EUID -eq 0 ]]; then
      use_sudo="no"
    else
      use_sudo="yes"
    fi
  fi

  if command -v systemctl >/dev/null 2>&1; then
    if [[ "$use_sudo" == "yes" ]]; then
      if command -v sudo >/dev/null 2>&1; then
        sudo systemctl enable "$service" 2>/dev/null
      else
        log_error "sudo required to enable $service but not available"
        return 1
      fi
    else
      systemctl enable "$service" 2>/dev/null
    fi
  else
    log_error "systemctl not available, cannot enable $service"
    return 1
  fi
}

disable_service() {
  local service="$1"
  local use_sudo="${2:-auto}"

  if [[ "$use_sudo" == "auto" ]]; then
    if [[ $EUID -eq 0 ]]; then
      use_sudo="no"
    else
      use_sudo="yes"
    fi
  fi

  if command -v systemctl >/dev/null 2>&1; then
    if [[ "$use_sudo" == "yes" ]]; then
      if command -v sudo >/dev/null 2>&1; then
        sudo systemctl disable "$service" 2>/dev/null
      else
        log_error "sudo required to disable $service but not available"
        return 1
      fi
    else
      systemctl disable "$service" 2>/dev/null
    fi
  else
    log_error "systemctl not available, cannot disable $service"
    return 1
  fi
}

get_service_status() {
  local service="$1"

  if command -v systemctl >/dev/null 2>&1; then
    systemctl status "$service" --no-pager 2>/dev/null || true
  else
    if is_service_running "$service"; then
      echo "$service is running"
    else
      echo "$service is not running"
    fi
  fi
}

check_activemq_broker() {
  local address="${1:-localhost}"
  local port="${2:-61616}"

  if command -v ss >/dev/null 2>&1; then
    ss -tln | grep -q ":${port}" 2>/dev/null
  elif command -v netstat >/dev/null 2>&1; then
    netstat -tln | grep -q ":${port}" 2>/dev/null
  else
    timeout 2 bash -c "echo >/dev/tcp/${address}/${port}" 2>/dev/null
  fi
}



get_service_logs() {
  local service="$1"
  local follow="${2:-false}"
  local lines="${3:-50}"

  if command -v journalctl >/dev/null 2>&1; then
    if [[ "$follow" == "true" ]]; then
      journalctl -u "$service" -f
    else
      journalctl -u "$service" -n "$lines" --no-pager
    fi
  else
    case "$service" in
      pitrac)
        local log_file="${PITRAC_LOG_FILE:-/var/log/pitrac/pitrac.log}"
        if [[ ! -f "$log_file" ]]; then
          log_file="${HOME}/.pitrac/logs/pitrac.log"
        fi
        if [[ ! -f "$log_file" ]]; then
          log_file="/tmp/pitrac.log"
        fi
        ;;
      activemq)
        local log_file="/var/log/activemq/activemq.log"
        ;;
      *)
        log_error "Unknown service: $service"
        return 1
        ;;
    esac

    if [[ -f "$log_file" ]]; then
      if [[ "$follow" == "true" ]]; then
        tail -f "$log_file"
      else
        tail -n "$lines" "$log_file"
      fi
    else
      log_error "Log file not found: $log_file"
      return 1
    fi
  fi
}

check_required_services() {
  local all_good=true

  log_info "Checking required services..."

  if is_service_running "activemq"; then
    log_debug "✓ ActiveMQ is running"
    if check_activemq_broker; then
      log_debug "  Broker accessible on port 61616"
    else
      log_warn "  Broker not accessible on port 61616"
      all_good=false
    fi
  else
    log_warn "✗ ActiveMQ is not running"
    all_good=false
  fi


  if [[ "$all_good" == "true" ]]; then
    return 0
  else
    return 1
  fi
}