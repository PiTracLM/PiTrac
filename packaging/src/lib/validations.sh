#!/usr/bin/env bash
# lib/validations.sh - Input validation functions for Bashly
validate_integer() {
  local value="$1"
  local arg="${2:-value}"
  
  if [[ ! "$value" =~ ^-?[0-9]+$ ]]; then
    echo "$arg must be an integer"
  fi
}

validate_file_exists() {
  local value="$1"
  local arg="${2:-file}"
  
  if [[ ! -f "$value" ]]; then
    echo "$arg does not exist: $value"
  fi
}

validate_dir_exists() {
  local value="$1"
  local arg="${2:-directory}"
  
  if [[ ! -d "$value" ]]; then
    echo "$arg does not exist: $value"
  fi
}

validate_system_mode() {
  local value="$1"
  
  case "$value" in
    camera1|camera2)
      ;;
    camera1_test_standalone)
      ;;
    camera2_test_standalone)
      ;;
    *)
      echo "Invalid system mode: $value"
      ;;
  esac
}

validate_camera_slot() {
  local value="$1"
  
  if [[ "$value" != "1" && "$value" != "2" ]]; then
    echo "Camera slot must be 1 or 2"
  fi
}

validate_not_already_running() {
  if is_pitrac_running; then
    echo "PiTrac is already running (PID: $(get_pitrac_pid))"
  fi
}

validate_positive_integer() {
  local value="$1"
  local arg="${2:-value}"
  
  if [[ ! "$value" =~ ^[0-9]+$ ]] || [[ "$value" -eq 0 ]]; then
    echo "$arg must be a positive integer"
  fi
}

validate_port() {
  local value="$1"
  local arg="${2:-port}"
  
  if [[ ! "$value" =~ ^[0-9]+$ ]] || [[ "$value" -lt 1 ]] || [[ "$value" -gt 65535 ]]; then
    echo "$arg must be a valid port number (1-65535)"
  fi
}

validate_host() {
  local value="$1"
  local arg="${2:-host}"
  
  if [[ -z "$value" ]]; then
    echo "$arg cannot be empty"
  elif [[ ! "$value" =~ ^[a-zA-Z0-9.-]+$ ]]; then
    echo "$arg contains invalid characters"
  fi
}


validate_path() {
  local value="$1"
  local arg="${2:-path}"
  
  if [[ "$value" =~ [\<\>\|] ]]; then
    echo "$arg contains invalid characters"
  fi
}

validate_writable_dir() {
  local value="$1"
  local arg="${2:-directory}"
  
  if [[ ! -d "$value" ]]; then
    echo "$arg does not exist: $value"
  elif [[ ! -w "$value" ]]; then
    echo "$arg is not writable: $value"
  fi
}

validate_executable() {
  local value="$1"
  local arg="${2:-executable}"
  
  if ! command -v "$value" >/dev/null 2>&1; then
    echo "$arg not found in PATH: $value"
  fi
}

validate_yaml_file() {
  local value="$1"
  local arg="${2:-file}"
  
  if [[ ! -f "$value" ]]; then
    echo "$arg does not exist: $value"
  elif [[ ! "$value" =~ \.(yaml|yml)$ ]]; then
    echo "$arg must be a YAML file (.yaml or .yml)"
  fi
  
  if command -v yq >/dev/null 2>&1; then
    if ! yq eval '.' "$value" >/dev/null 2>&1; then
      echo "$arg contains invalid YAML syntax"
    fi
  fi
}

validate_json_file() {
  local value="$1"
  local arg="${2:-file}"
  
  if [[ ! -f "$value" ]]; then
    echo "$arg does not exist: $value"
  elif [[ ! "$value" =~ \.json$ ]]; then
    echo "$arg must be a JSON file (.json)"
  fi
  
  if command -v jq >/dev/null 2>&1; then
    if ! jq '.' "$value" >/dev/null 2>&1; then
      echo "$arg contains invalid JSON syntax"
    fi
  fi
}

validate_boolean() {
  local value="$1"
  local arg="${2:-value}"
  
  case "${value,,}" in
    true|false|yes|no|1|0)
      ;;
    *)
      echo "$arg must be a boolean value (true/false, yes/no, 1/0)"
      ;;
  esac
}

validate_enum() {
  local value="$1"
  local allowed="$2"
  local arg="${3:-value}"
  
  if [[ ! " $allowed " =~ " $value " ]]; then
    echo "$arg must be one of: $allowed"
  fi
}

validate_not_empty() {
  local value="$1"
  local arg="${2:-value}"
  
  if [[ -z "$value" ]]; then
    echo "$arg cannot be empty"
  fi
}

validate_email() {
  local value="$1"
  local arg="${2:-email}"
  
  if [[ ! "$value" =~ ^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$ ]]; then
    echo "$arg must be a valid email address"
  fi
}

validate_url() {
  local value="$1"
  local arg="${2:-URL}"
  
  if [[ ! "$value" =~ ^https?://[a-zA-Z0-9.-]+(:[0-9]+)?(/.*)?$ ]]; then
    echo "$arg must be a valid URL"
  fi
}
