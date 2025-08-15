#!/usr/bin/env bash
set -euo pipefail

# LGPIO Library Installation Script
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Load defaults from config file
load_defaults "lgpio" "$@"

CONFIG_FILE="${CONFIG_FILE:-/boot/firmware/config.txt}"
LGPIO_URL="${LGPIO_URL:-http://abyz.me.uk/lg/lg.zip}"
BUILD_DIR="${BUILD_DIR:-/tmp/lgpio-build}"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
ENABLE_SPI="${ENABLE_SPI:-1}"
FORCE="${FORCE:-0}"

SPI_LINE_ON="dtparam=spi=on"
SPI_LINE_OFF="dtparam=spi=off"
ASSETS_DIR="${SCRIPT_DIR}/assets"

# Check if lgpio is installed
is_lgpio_installed() {
  # Check if library is available via ldconfig
  if ldconfig -p 2>/dev/null | grep -q 'liblgpio\.so'; then
    return 0
  fi
  
  # Check for pkg-config file or header
  [ -f /usr/lib/pkgconfig/lgpio.pc ] && return 0
  [ -f /usr/local/lib/pkgconfig/lgpio.pc ] && return 0
  [ -f /usr/include/lgpio.h ] && return 0
  [ -f /usr/local/include/lgpio.h ] && return 0
  
  return 1
}

# Check if SPI is enabled
spi_already_enabled() {
  [ -f "$CONFIG_FILE" ] && grep -q "^${SPI_LINE_ON}" "$CONFIG_FILE"
}

# Install LGPIO library
install_lgpio() {
  # Run pre-flight checks
  run_preflight_checks "lgpio" || return 1

  if is_lgpio_installed; then
    echo "LGPIO already installed; skipping build/install."
    return 0
  fi

  # Ensure required packages are installed
  apt_ensure wget unzip build-essential ca-certificates

  WORK_DIR="$(create_temp_dir "lgpio")"

  log_info "Download LGPIO into: $WORK_DIR"
  cd "$WORK_DIR"

  download_with_progress "http://abyz.me.uk/lg/lg.zip" "lg.zip" "Downloading LGPIO"

  echo "Unpacking LGPIO..."
  unzip -q lg.zip
  cd lg

  echo "Building LGPIO..."
  make
  $SUDO make install
  
  # Update library cache
  $SUDO ldconfig 2>/dev/null || true
  
  echo "LGPIO installed."
}

copy_lgpio_pc() {
  local target_dir="/usr/lib/pkgconfig"
  local src_pc="${ASSETS_DIR}/lgpio.pc"

  # If pkg-config already sees lgpio, skip copying
  if pkg-config --exists lgpio 2>/dev/null; then
    echo "lgpio.pc already available via pkg-config; skipping copy."
    return 0
  fi

  echo "Copying lgpio.pc to ${target_dir}..."
  if [ ! -f "$src_pc" ]; then
    log_warn "${src_pc} not found. Skipping lgpio.pc copy."
    return 0
  fi

  $SUDO mkdir -p "$target_dir"
  $SUDO cp "$src_pc" "${target_dir}/"

  if [ -f "${target_dir}/lgpio.pc" ]; then
    log_success "Successfully copied lgpio.pc to ${target_dir}"
  else
    log_error "Issue copying lgpio.pc to ${target_dir}"
    return 1
  fi
}

enable_spi() {
  if [ ! -f "$CONFIG_FILE" ]; then
    log_warn "${CONFIG_FILE} not found. Are you on Raspberry Pi OS? Skipping SPI enable."
    return 0
  fi

  if spi_already_enabled; then
    echo "SPI already enabled in $CONFIG_FILE"
  else
    echo "Enabling SPI pins in $CONFIG_FILE..."
    if grep -q "^${SPI_LINE_OFF}" "$CONFIG_FILE"; then
      $SUDO sed -i "s/^${SPI_LINE_OFF}/${SPI_LINE_ON}/" "$CONFIG_FILE"
    else
      echo "$SPI_LINE_ON" | $SUDO tee -a "$CONFIG_FILE" >/dev/null
    fi
  fi

  # Validate device nodes (may require reboot on real hardware)
  if [ -e /dev/spidev0.0 ] && [ -e /dev/spidev0.1 ]; then
    echo "SPI devices found: /dev/spidev0.0 and /dev/spidev0.1"
  else
    echo "SPI devices not found yet. You may need to reboot on Pi hardware."
  fi
}

# Main installation
install_lgpio_full() {
  # Run pre-flight checks
  run_preflight_checks "lgpio" || return 1

  install_lgpio
  copy_lgpio_pc
  enable_spi
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  install_lgpio_full
fi
