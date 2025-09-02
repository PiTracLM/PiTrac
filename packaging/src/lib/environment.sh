#!/usr/bin/env bash
# lib/environment.sh - Environment setup functions

readonly PITRAC_BINARY="/usr/lib/pitrac/pitrac_lm"
readonly PITRAC_LIB_DIR="/usr/lib/pitrac"
readonly PITRAC_SHARE_DIR="/usr/share/pitrac"
readonly PITRAC_CONFIG_DIR="/etc/pitrac"

setup_pitrac_environment() {
  export LD_LIBRARY_PATH="${PITRAC_LIB_DIR}:${LD_LIBRARY_PATH:-}"
  export PITRAC_ROOT="${PITRAC_LIB_DIR}"
  
  if [[ -z "${HOME:-}" ]]; then
    if [[ -n "${USER:-}" ]]; then
      export HOME=$(getent passwd "$USER" | cut -d: -f6)
    elif [[ -n "${SUDO_USER:-}" ]]; then
      export HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
    else
      local current_user=$(whoami)
      export HOME=$(getent passwd "$current_user" | cut -d: -f6)
      if [[ -z "${HOME:-}" ]]; then
        export HOME="/home/$current_user"
      fi
    fi
  fi
  
  setup_camera_env
  
  set_libcamera_config
  
  ensure_directories
  
  return 0
}

setup_camera_env() {
  local config_file="${1:-${config[config_file]:-$PITRAC_CONFIG}}"
  
  local slot1_type="4"
  local slot2_type="4"
  local slot1_lens=""
  local slot2_lens=""
  
  if [[ -f "$config_file" ]] && declare -p config >/dev/null 2>&1; then
    slot1_type="${config[camera_slot1_type]:-4}"
    slot2_type="${config[camera_slot2_type]:-4}"
    slot1_lens="${config[camera_slot1_lens]:-}"
    slot2_lens="${config[camera_slot2_lens]:-}"
  elif [[ -f "$config_file" ]]; then
    slot1_type=$(grep -A3 "^  slot1:" "$config_file" 2>/dev/null | \
                 grep "type:" | awk '{print $2}' | cut -d'#' -f1 | tr -d ' ' || echo "4")
    slot2_type=$(grep -A3 "^  slot2:" "$config_file" 2>/dev/null | \
                 grep "type:" | awk '{print $2}' | cut -d'#' -f1 | tr -d ' ' || echo "4")
    slot1_lens=$(grep -A3 "^  slot1:" "$config_file" 2>/dev/null | \
                 grep "lens:" | awk '{print $2}' | cut -d'#' -f1 | tr -d ' ' || true)
    slot2_lens=$(grep -A3 "^  slot2:" "$config_file" 2>/dev/null | \
                 grep "lens:" | awk '{print $2}' | cut -d'#' -f1 | tr -d ' ' || true)
  fi
  
  export PITRAC_SLOT1_CAMERA_TYPE="${slot1_type}"
  export PITRAC_SLOT2_CAMERA_TYPE="${slot2_type}"
  
  [[ -n "$slot1_lens" ]] && export PITRAC_SLOT1_LENS_TYPE="$slot1_lens"
  [[ -n "$slot2_lens" ]] && export PITRAC_SLOT2_LENS_TYPE="$slot2_lens"
  
  log_debug "Camera environment: Slot1=$slot1_type/$slot1_lens, Slot2=$slot2_type/$slot2_lens"
}

set_libcamera_config() {
  local model=$(detect_pi_model)
  local config_file=$(get_libcamera_config_path "$model")
  
  if [[ -f "$config_file" ]]; then
    export LIBCAMERA_RPI_CONFIG_FILE="$config_file"
    log_debug "Set LIBCAMERA_RPI_CONFIG_FILE to $config_file"
  else
    log_warn "libcamera config file not found: $config_file"
  fi
}

ensure_directories() {
  local dirs=(
    "${HOME}/.pitrac/config"
    "${HOME}/.pitrac/cache"
    "${HOME}/.pitrac/state"
    "${HOME}/.pitrac/calibration"
    "${HOME}/.pitrac/logs"
    "${HOME}/.pitrac/run"
    "${HOME}/LM_Shares/Images"
    "${HOME}/LM_Shares/WebShare"
  )
  
  for dir in "${dirs[@]}"; do
    if [[ ! -d "$dir" ]]; then
      mkdir -p "$dir" 2>/dev/null || true
    fi
  done
}

get_user_config_dir() {
  echo "${HOME}/.pitrac/config"
}

get_user_state_dir() {
  echo "${HOME}/.pitrac/state"
}

get_user_cache_dir() {
  echo "${HOME}/.pitrac/cache"
}

get_user_log_dir() {
  echo "${HOME}/.pitrac/logs"
}

is_systemd_service() {
  if [[ -n "${INVOCATION_ID:-}" ]] || [[ -n "${JOURNAL_STREAM:-}" ]]; then
    return 0
  else
    return 1
  fi
}

setup_systemd_environment() {
  export PITRAC_SERVICE_MODE="1"
  
  if [[ -z "${HOME:-}" ]]; then
    local current_user=$(whoami)
    export HOME=$(getent passwd "$current_user" | cut -d: -f6)
    if [[ -z "${HOME:-}" ]]; then
      export HOME="/home/$current_user"
    fi
  fi
  
  export PATH="/usr/local/bin:/usr/bin:/bin"
  
  if [[ ! -d "${HOME}" ]]; then
    log_error "Home directory does not exist: ${HOME}"
    return 1
  fi
  
  cd "${HOME}" || return 1
  
  return 0
}

check_environment() {
  local errors=0
  
  if [[ ! -f "$PITRAC_BINARY" ]]; then
    log_error "PiTrac binary not found: $PITRAC_BINARY"
    ((errors++))
  fi
  
  if [[ ! -d "$PITRAC_LIB_DIR" ]]; then
    log_error "PiTrac library directory not found: $PITRAC_LIB_DIR"
    ((errors++))
  fi
  
  if ! is_raspberry_pi; then
    log_warn "Not running on Raspberry Pi - some features may not work"
  fi
  
  if ! check_camera_availability; then
    log_warn "No cameras detected - camera features will not work"
  fi
  
  if ! is_gpio_available; then
    log_warn "GPIO not available - strobe features will not work"
  fi
  
  if [[ $errors -gt 0 ]]; then
    return 1
  else
    return 0
  fi
}

export_pitrac_environment() {
  local env_file="${HOME}/.pitrac/state/environment"
  
  cat > "$env_file" << EOF
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
export PITRAC_ROOT="${PITRAC_ROOT:-}"
export LIBCAMERA_RPI_CONFIG_FILE="${LIBCAMERA_RPI_CONFIG_FILE:-}"
export PITRAC_SLOT1_CAMERA_TYPE="${PITRAC_SLOT1_CAMERA_TYPE:-}"
export PITRAC_SLOT2_CAMERA_TYPE="${PITRAC_SLOT2_CAMERA_TYPE:-}"
export PITRAC_SLOT1_LENS_TYPE="${PITRAC_SLOT1_LENS_TYPE:-}"
export PITRAC_SLOT2_LENS_TYPE="${PITRAC_SLOT2_LENS_TYPE:-}"
EOF
  
  chmod 644 "$env_file"
}
