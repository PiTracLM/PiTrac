#!/usr/bin/env bash
set -euo pipefail

# PiTrac Environment Configuration Script
# Sets up environment variables, directories, and shell configuration
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/../common.sh"

# Load defaults from config file
load_defaults "pitrac-environment" "$@"

# Configuration with loaded defaults
FORCE="${FORCE:-0}"

# Use detection from common.sh, prioritize actual directories over defaults
DETECTED_ROOT="$(detect_pitrac_root)"
if [ -d "$DETECTED_ROOT" ] && [ "$DETECTED_ROOT" != "/home/$(whoami)/Dev/PiTrac/Software/LMSourceCode" ]; then
  DEFAULT_PITRAC_ROOT="$DETECTED_ROOT"
else
  DEFAULT_PITRAC_ROOT="${PITRAC_ROOT:-$DETECTED_ROOT}"
fi

if [ -d "$DETECTED_ROOT" ] && [ "$DETECTED_ROOT" != "/home/$(whoami)/Dev/PiTrac/Software/LMSourceCode" ]; then
  PITRAC_BASE="$(dirname "$(dirname "$DETECTED_ROOT")")"
  # If PiTrac is at /work or similar root, put LM_Shares there too
  if [ "$PITRAC_BASE" = "/work" ] || [ "$PITRAC_BASE" = "/PiTrac" ]; then
    DEFAULT_IMAGE_DIR="${PITRAC_IMAGE_DIR:-$PITRAC_BASE/LM_Shares/Images}"
    DEFAULT_WEB_DIR="${PITRAC_WEB_DIR:-$PITRAC_BASE/LM_Shares/WebShare}"
  else
    # Otherwise put LM_Shares at same level as PiTrac
    PARENT_DIR="$(dirname "$PITRAC_BASE")"
    DEFAULT_IMAGE_DIR="${PITRAC_IMAGE_DIR:-$PARENT_DIR/LM_Shares/Images}"
    DEFAULT_WEB_DIR="${PITRAC_WEB_DIR:-$PARENT_DIR/LM_Shares/WebShare}"
  fi
else
  DETECTED_IMAGE_DIR="$(detect_lm_shares_dir "Images")"
  DEFAULT_IMAGE_DIR="${PITRAC_IMAGE_DIR:-$DETECTED_IMAGE_DIR}"
  
  DETECTED_WEB_DIR="$(detect_lm_shares_dir "WebShare")"
  DEFAULT_WEB_DIR="${PITRAC_WEB_DIR:-$DETECTED_WEB_DIR}"
fi
DEFAULT_MSG_BROKER_IP="${PITRAC_MSG_BROKER_IP:-localhost}"
DEFAULT_E6_HOST="${PITRAC_E6_HOST:-}"
DEFAULT_GSPRO_HOST="${PITRAC_GSPRO_HOST:-}"
DEFAULT_SLOT1_CAMERA="${PITRAC_SLOT1_CAMERA:-4}"
DEFAULT_SLOT2_CAMERA="${PITRAC_SLOT2_CAMERA:-4}"
UPDATE_SHELL_RC="${UPDATE_SHELL_RC:-1}"

# Get current shell config file
get_shell_config_file() {
  local shell_name
  shell_name="$(basename "$SHELL")"
  
  case "$shell_name" in
    "zsh")
      echo "$HOME/.zshrc"
      ;;
    "bash")
      echo "$HOME/.bashrc"
      ;;
    *)
      # Default to bashrc
      echo "$HOME/.bashrc"
      ;;
  esac
}

# Prompt for user input with default
prompt_with_default() {
  local prompt="$1"
  local default="$2"
  local result
  
  read -p "$prompt [$default]: " result
  echo "${result:-$default}"
}

# Prompt for network configuration
prompt_network_config() {
  echo ""
  echo "=== Network Configuration ==="
  echo "Enter network settings for your PiTrac installation."
  echo ""
  
  # Detect local network
  local local_ip
  local_ip=$(hostname -I | awk '{print $1}' 2>/dev/null || echo "127.0.0.1")
  
  echo "Detected local IP: $local_ip"
  
  # ActiveMQ Broker Address
  # Default to localhost for single-machine setups
  local default_broker="localhost"
  
  echo ""
  echo "For single-machine setup (most common): use 'localhost' or '127.0.0.1'"
  echo "For multi-machine setup: use the broker machine's IP address"
  
  PITRAC_MSG_BROKER_IP=$(prompt_with_default "ActiveMQ Broker IP address" "$default_broker")
  
  # Golf Simulator Addresses (optional)
  echo ""
  echo "Golf simulator integration (optional - leave blank to skip):"
  PITRAC_E6_HOST=$(prompt_with_default "E6/TruGolf host IP (optional)" "")
  PITRAC_GSPRO_HOST=$(prompt_with_default "GSPro host IP (optional)" "")
}

# Prompt for camera configuration
prompt_camera_config() {
  echo ""
  echo "=== Camera Configuration ==="
  echo "Select camera types for your setup:"
  echo "  4 = Official Pi Global Shutter cameras with 6mm lenses (recommended)"
  echo "  6 = Innomaker Global Shutter cameras with 3.6mm lenses"
  echo ""
  
  PITRAC_SLOT1_CAMERA=$(prompt_with_default "Camera Slot 1 type" "4")
  PITRAC_SLOT2_CAMERA=$(prompt_with_default "Camera Slot 2 type" "4")
}

# Prompt for directory configuration
prompt_directory_config() {
  echo ""
  echo "=== Directory Configuration ==="
  echo "Configure PiTrac directories:"
  echo ""
  
  PITRAC_ROOT=$(prompt_with_default "PiTrac source code directory" "$DEFAULT_PITRAC_ROOT")
  PITRAC_IMAGE_DIR=$(prompt_with_default "Image storage directory" "$DEFAULT_IMAGE_DIR")
  PITRAC_WEB_DIR=$(prompt_with_default "Web server directory" "$DEFAULT_WEB_DIR")
}

# Create necessary directories
create_directories() {
  echo "Creating PiTrac directories..."
  
  # Create base directories
  mkdir -p "$(dirname "$PITRAC_IMAGE_DIR")"
  mkdir -p "$PITRAC_IMAGE_DIR"
  mkdir -p "$PITRAC_WEB_DIR"
  
  # Create LM_Shares structure if using default paths
  if [[ "$PITRAC_IMAGE_DIR" == *"/LM_Shares/"* ]]; then
    local shares_base
    shares_base="$(dirname "$PITRAC_IMAGE_DIR")"
    mkdir -p "$shares_base/Images"
    mkdir -p "$shares_base/WebShare"
    echo "Created LM_Shares directory structure at $shares_base"
  fi
  
  echo "Directories created successfully"
}

# Generate environment configuration
generate_environment_config() {
  local pi_model
  pi_model="$(detect_pi_model)"
  
  # Get libcamera config path
  local libcamera_config=""
  case "$pi_model" in
    "5")
      libcamera_config="/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml"
      ;;
    "4")
      libcamera_config="/usr/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml"
      ;;
    *)
      libcamera_config=""
      ;;
  esac
  
  # Generate environment variables
  cat << EOF
# PiTrac Environment Configuration
# PiTrac environment variables - configured $(date)

# Core PiTrac paths
export PITRAC_ROOT="$PITRAC_ROOT"
export PITRAC_BASE_IMAGE_LOGGING_DIR="$PITRAC_IMAGE_DIR/"
export PITRAC_WEBSERVER_SHARE_DIR="$PITRAC_WEB_DIR/"

# Network configuration
export PITRAC_MSG_BROKER_FULL_ADDRESS="tcp://$PITRAC_MSG_BROKER_IP:61616"
EOF

  # Add golf sim hosts if specified
  if [ -n "$PITRAC_E6_HOST" ]; then
    echo "export PITRAC_E6_HOST_ADDRESS=\"$PITRAC_E6_HOST\""
  fi
  
  if [ -n "$PITRAC_GSPRO_HOST" ]; then
    echo "export PITRAC_GSPRO_HOST_ADDRESS=\"$PITRAC_GSPRO_HOST\""
  fi
  
  cat << EOF

# Camera configuration
export PITRAC_SLOT1_CAMERA_TYPE=$PITRAC_SLOT1_CAMERA
export PITRAC_SLOT2_CAMERA_TYPE=$PITRAC_SLOT2_CAMERA

# libcamera configuration (only on Raspberry Pi)
$([ -n "$libcamera_config" ] && echo "export LIBCAMERA_RPI_CONFIG_FILE=\"$libcamera_config\"")

# Additional PiTrac environment settings
export PITRAC_ENV_CONFIGURED=1
EOF
}

# Update shell configuration
update_shell_config() {
  local config_file
  config_file="$(get_shell_config_file)"
  
  echo "Updating shell configuration: $config_file"
  
  # Backup original if not already done
  if [ ! -f "${config_file}.ORIGINAL" ] && [ -f "$config_file" ]; then
    cp "$config_file" "${config_file}.ORIGINAL"
  fi
  
  # Remove any existing PiTrac configuration
  if [ -f "$config_file" ]; then
    # Remove lines between PiTrac markers
    sed -i '/# PiTrac Environment Configuration/,/# End PiTrac Environment/d' "$config_file"
  else
    # Create config file if it doesn't exist
    touch "$config_file"
  fi
  
  # Add new PiTrac configuration
  echo "" >> "$config_file"
  generate_environment_config >> "$config_file"
  echo "# End PiTrac Environment" >> "$config_file"
  
  echo "Shell configuration updated"
}

# Create PiTrac configuration summary
create_config_summary() {
  local summary_file="$HOME/.pitrac_config_summary"
  
  cat > "$summary_file" << EOF
PiTrac Configuration Summary
Generated: $(date)

Directories:
  Source Code: $PITRAC_ROOT
  Images: $PITRAC_IMAGE_DIR
  Web Content: $PITRAC_WEB_DIR

Network:
  ActiveMQ Broker: $PITRAC_MSG_BROKER_IP:61616
  E6/TruGolf Host: ${PITRAC_E6_HOST:-"Not configured"}
  GSPro Host: ${PITRAC_GSPRO_HOST:-"Not configured"}

Cameras:
  Slot 1: Type $PITRAC_SLOT1_CAMERA
  Slot 2: Type $PITRAC_SLOT2_CAMERA

Shell Configuration: $(get_shell_config_file)
EOF

  echo "Configuration summary saved to: $summary_file"
}

# Interactive configuration setup
run_interactive_setup() {
  echo "=== PiTrac Environment Setup ==="
  echo "This will configure environment variables and directories for PiTrac."
  echo ""
  
  # Check if already configured
  if [ "${PITRAC_ENV_CONFIGURED:-}" = "1" ] && [ "$FORCE" != "1" ]; then
    echo "PiTrac environment appears to already be configured."
    echo "Current PITRAC_ROOT: ${PITRAC_ROOT:-Not set}"
    echo ""
    echo "Set FORCE=1 to reconfigure, or run with --force flag"
    return 0
  fi
  
  # Gather configuration
  prompt_network_config
  prompt_camera_config  
  prompt_directory_config
  
  echo ""
  echo "=== Configuration Summary ==="
  echo "PiTrac Root: $PITRAC_ROOT"
  echo "Images: $PITRAC_IMAGE_DIR"
  echo "Web Content: $PITRAC_WEB_DIR"
  echo "ActiveMQ Broker: $PITRAC_MSG_BROKER_IP:61616"
  echo "Camera Slot 1: Type $PITRAC_SLOT1_CAMERA"
  echo "Camera Slot 2: Type $PITRAC_SLOT2_CAMERA"
  echo ""
  
  read -p "Apply this configuration? [y/N]: " confirm
  if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
    echo "Configuration cancelled."
    return 1
  fi
  
  # Apply configuration
  create_directories
  update_shell_config
  create_config_summary
  
  echo ""
  echo "PiTrac environment configuration completed!"
  echo ""
  echo "To use the new environment variables, either:"
  echo "1. Restart your shell session (logout and login)"
  echo "2. Run: source $(get_shell_config_file)"
  echo ""
  echo "Configuration summary: $HOME/.pitrac_config_summary"
}

# Check if environment is configured
is_pitrac_environment_installed() {
  # Check if environment is configured
  local config_file
  config_file="$(get_shell_config_file)"
  
  [ -f "$config_file" ] && grep -q "PITRAC_ROOT=" "$config_file" && return 0
  return 1
}

# Non-interactive setup with defaults (for automated installs)
setup_default_environment() {
  echo "Setting up default PiTrac environment..."
  
  # Use defaults
  PITRAC_ROOT="$DEFAULT_PITRAC_ROOT"
  PITRAC_IMAGE_DIR="$DEFAULT_IMAGE_DIR"
  PITRAC_WEB_DIR="$DEFAULT_WEB_DIR"
  PITRAC_MSG_BROKER_IP="localhost"
  PITRAC_E6_HOST=""
  PITRAC_GSPRO_HOST=""
  PITRAC_SLOT1_CAMERA="4"
  PITRAC_SLOT2_CAMERA="4"
  
  create_directories
  update_shell_config
  create_config_summary
  
  echo "Default PiTrac environment configured"
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  # Check for command line arguments
  if [[ "${1:-}" == "--default" ]] || [[ "${1:-}" == "--non-interactive" ]]; then
    setup_default_environment
  else
    run_interactive_setup
  fi
fi