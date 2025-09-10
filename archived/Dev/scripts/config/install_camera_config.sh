#!/usr/bin/env bash
set -euo pipefail

# Camera Configuration Script for PiTrac
# Handles libcamera timeouts, IPA files, and camera-specific setup

# Configuration paths
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/../common.sh"

# Load defaults from config file
load_defaults "camera-config" "$@"

ASSETS_DIR="${ASSETS_DIR:-${SCRIPT_DIR}/assets}"
CAMERA_TIMEOUT_VALUE_MS="${CAMERA_TIMEOUT_VALUE_MS:-1000000}"
INSTALL_IPA_FILES="${INSTALL_IPA_FILES:-1}"
TUNING_FILE="${TUNING_FILE:-}"
ENABLE_DEBUG="${ENABLE_DEBUG:-0}"
FORCE="${FORCE:-0}"

# Get libcamera pipeline path based on Pi model
get_libcamera_pipeline_path() {
  local pi_model="$1"
  case "$pi_model" in
    "5")
      echo "/usr/share/libcamera/pipeline/rpi/pisp"
      ;;
    "4")
      echo "/usr/share/libcamera/pipeline/rpi/vc4"
      ;;
    *)
      # Default to vc4 for unknown models
      echo "/usr/share/libcamera/pipeline/rpi/vc4"
      ;;
  esac
}

# Get IPA path based on Pi model
get_libcamera_ipa_path() {
  local pi_model="$1"
  case "$pi_model" in
    "5")
      echo "/usr/share/libcamera/ipa/rpi/pisp"
      ;;
    "4")
      echo "/usr/share/libcamera/ipa/rpi/vc4"
      ;;
    *)
      # Default to vc4 for unknown models
      echo "/usr/share/libcamera/ipa/rpi/vc4"
      ;;
  esac
}

# Configure libcamera timeouts
configure_libcamera_timeouts() {
  local pi_model="$1"
  local pipeline_path
  pipeline_path="$(get_libcamera_pipeline_path "$pi_model")"
  
  echo "Configuring libcamera timeouts for Pi $pi_model..."
  
  # Check if pipeline directory exists
  if [ ! -d "$pipeline_path" ]; then
    log_info "libcamera pipeline path $pipeline_path does not exist"
    log_info "Not on Raspberry Pi hardware - skipping camera hardware configuration"
    return 0
  fi
  
  local config_file="$pipeline_path/rpi_apps.yaml"
  
  # Check if config file exists, if not try to find example
  if [ ! -f "$config_file" ]; then
    if [ -f "$pipeline_path/example.yaml" ]; then
      echo "Creating rpi_apps.yaml from example.yaml"
      $SUDO cp "$pipeline_path/example.yaml" "$config_file"
    else
      echo "Creating new rpi_apps.yaml configuration file"
      $SUDO tee "$config_file" >/dev/null << 'EOF'
# libcamera configuration for PiTrac
# Extended timeout for external trigger applications

%YAML 1.1
---
pipeline:
    camera_timeout_value_ms: 1000000,
EOF
    fi
  fi
  
  # Backup original if not already done
  if [ ! -f "${config_file}.ORIGINAL" ]; then
    $SUDO cp "$config_file" "${config_file}.ORIGINAL"
  fi
  
  # Check if timeout is already configured
  if grep -q "camera_timeout_value_ms.*1000000" "$config_file"; then
    echo "libcamera timeout already configured"
  else
    echo "Adding extended timeout to libcamera configuration"
    
    # Add timeout configuration to pipeline section
    if grep -q "^pipeline:" "$config_file"; then
      # Pipeline section exists, add timeout under it
      $SUDO sed -i '/^pipeline:/a\    camera_timeout_value_ms: 1000000,' "$config_file"
    else
      # No pipeline section, add it
      echo "pipeline:" | $SUDO tee -a "$config_file" >/dev/null
      echo "    camera_timeout_value_ms: 1000000," | $SUDO tee -a "$config_file" >/dev/null
    fi
  fi
  
  echo "libcamera timeout configuration completed"
}

# Install IPA camera sensor files
install_ipa_files() {
  local pi_model="$1"
  local ipa_path
  ipa_path="$(get_libcamera_ipa_path "$pi_model")"
  
  log_info "Installing IPA sensor files for Pi $pi_model..."
  
  # Check if IPA directory exists
  if [ ! -d "$ipa_path" ]; then
    echo "Warning: IPA path $ipa_path does not exist"
    echo "libcamera may not be installed or Pi model detection may be incorrect"
    return 1
  fi
  
  # Install IMX296 NOIR sensor file if available
  local ipa_file=""
  case "$pi_model" in
    "5")
      ipa_file="$ASSETS_DIR/imx296_noir.json.PI_5_FOR_PISP_DIRECTORY"
      ;;
    "4")
      ipa_file="$ASSETS_DIR/imx296_noir.json.PI_4_FOR_VC4_DIRECTORY"
      ;;
  esac
  
  if [ -n "$ipa_file" ] && [ -f "$ipa_file" ]; then
    echo "Installing IMX296 NOIR sensor configuration..."
    $SUDO cp "$ipa_file" "$ipa_path/imx296_noir.json"
    echo "IMX296 NOIR sensor file installed"
  else
    echo "Note: IMX296 NOIR sensor file not found at $ipa_file"
    echo "This is only needed if using IMX296 NOIR cameras"
  fi
}

# Set up libcamera environment variable
setup_libcamera_environment() {
  local pi_model="$1"
  local pipeline_path
  pipeline_path="$(get_libcamera_pipeline_path "$pi_model")"
  
  local config_file="$pipeline_path/rpi_apps.yaml"
  
  echo "Setting up libcamera environment variables..."
  
  # Check if config file exists
  if [ ! -f "$config_file" ]; then
    log_info "libcamera config file $config_file does not exist"
    log_info "Not on Raspberry Pi hardware - skipping environment configuration"
    return 0
  fi
  
  # This will be added to the shell profile by the environment script
  # For now, just export it for the current session
  export LIBCAMERA_RPI_CONFIG_FILE="$config_file"
  
  echo "libcamera environment configured"
  echo "LIBCAMERA_RPI_CONFIG_FILE will be set to: $config_file"
}

# Verify camera detection
verify_camera_setup() {
  echo "Verifying camera setup..."
  
  # Check if libcamera tools are available
  if need_cmd libcamera-hello; then
    echo "Testing camera detection with libcamera-hello..."
    
    # Run camera detection (timeout after 10 seconds)
    if timeout 10s libcamera-hello --list-cameras >/dev/null 2>&1; then
      echo "Camera detection successful"
      
      # Show detected cameras
      echo "Detected cameras:"
      libcamera-hello --list-cameras 2>/dev/null || true
    else
      log_warn "Camera detection test timed out or failed"
      echo "This may be normal if no cameras are currently connected"
    fi
  else
    echo "Note: libcamera-hello not found. Camera verification skipped."
    echo "Install rpicam-apps package to enable camera testing"
  fi
}

# Check if camera is configured
is_camera_config_installed() {
  # In non-Pi environments, check for marker
  if [ ! -d "/usr/share/libcamera" ] && [ ! -d "/boot/firmware" ]; then
    [ -f "$HOME/.pitrac_camera_configured" ] && return 0
    return 1
  fi
  
  local pi_model
  pi_model="$(detect_pi_model)"
  local pipeline_path
  pipeline_path="$(get_libcamera_pipeline_path "$pi_model")"
  local config_file="$pipeline_path/rpi_apps.yaml"
  
  # Check if timeout configuration exists
  [ -f "$config_file" ] && grep -q "camera_timeout_value_ms.*1000000" "$config_file" && return 0
  return 1
}

# Main configuration
configure_camera() {
  local pi_model
  pi_model="$(detect_pi_model)"
  
  if [ "$pi_model" = "unknown" ]; then
    echo "Warning: Could not detect Pi model. Using Pi 4 defaults."
    pi_model="4"
  else
    echo "Detected Raspberry Pi $pi_model"
  fi
  
  echo "Configuring camera system for PiTrac..."
  
  # Check if we're in a non-Pi environment (Docker, etc)
  if [ ! -d "/usr/share/libcamera" ] && [ ! -d "/boot/firmware" ]; then
    log_info "Not running on Raspberry Pi hardware. Skipping camera hardware configurations."
    # Create marker for non-Pi environments
    touch "$HOME/.pitrac_camera_configured"
    echo "Camera configuration completed (non-Pi environment)."
    return 0
  fi
  
  # Configure libcamera timeouts for external triggers
  configure_libcamera_timeouts "$pi_model"
  
  # Install camera sensor IPA files if available
  install_ipa_files "$pi_model"
  
  # Set up environment variables
  setup_libcamera_environment "$pi_model"
  
  # Verify camera setup
  verify_camera_setup
  
  echo "Camera configuration completed!"
  echo ""
  echo "Note: The LIBCAMERA_RPI_CONFIG_FILE environment variable will be"
  echo "configured permanently when you run the PiTrac environment setup."
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  configure_camera
fi