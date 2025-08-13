#!/usr/bin/env bash
set -euo pipefail

# System Configuration Script for PiTrac
# Handles Pi-specific hardware and OS configuration

# Configuration
CONFIG_FILE="/boot/firmware/config.txt"
FORCE="${FORCE:-0}"

# Use sudo only if not already root
if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi
export DEBIAN_FRONTEND=noninteractive

# Utilities
need_cmd() { 
  command -v "$1" >/dev/null 2>&1
}

# Detect Pi model
detect_pi_model() {
  if grep -q "Raspberry Pi 5" /proc/cpuinfo 2>/dev/null; then
    echo "5"
  elif grep -q "Raspberry Pi 4" /proc/cpuinfo 2>/dev/null; then
    echo "4"
  else
    echo "unknown"
  fi
}

# Check if NVME hardware is present
detect_nvme_hardware() {
  # Check if PCIe NVME device is present
  lsblk | grep -q "nvme" 2>/dev/null && return 0
  # Check if PCIe is available but not configured
  [ -d "/sys/class/pci_bus" ] && return 0
  return 1
}

# Check current config.txt settings
check_config_setting() {
  local setting="$1"
  if [ -f "$CONFIG_FILE" ]; then
    grep -q "^$setting" "$CONFIG_FILE" 2>/dev/null && return 0
    grep -q "^#.*$setting" "$CONFIG_FILE" 2>/dev/null && return 1
  fi
  return 1
}

# Add or update config.txt setting
update_config_setting() {
  local setting="$1"
  local value="$2"
  local full_setting="${setting}=${value}"
  
  if [ ! -f "$CONFIG_FILE" ]; then
    echo "ERROR: $CONFIG_FILE not found. Are you on Raspberry Pi OS?"
    return 1
  fi
  
  echo "Updating $CONFIG_FILE: $full_setting"
  
  # Backup original if not already done
  if [ ! -f "${CONFIG_FILE}.ORIGINAL" ]; then
    $SUDO cp "$CONFIG_FILE" "${CONFIG_FILE}.ORIGINAL"
  fi
  
  # Remove existing setting (commented or not)
  $SUDO sed -i "/^#*${setting}=/d" "$CONFIG_FILE"
  
  # Add new setting before the final [all] section
  if grep -q "^\[all\]" "$CONFIG_FILE"; then
    $SUDO sed -i "/^\[all\]/i\\
# PiTrac configuration\\
$full_setting\\
" "$CONFIG_FILE"
  else
    # No [all] section, append to end
    echo "" | $SUDO tee -a "$CONFIG_FILE" >/dev/null
    echo "# PiTrac configuration" | $SUDO tee -a "$CONFIG_FILE" >/dev/null
    echo "$full_setting" | $SUDO tee -a "$CONFIG_FILE" >/dev/null
  fi
}

# Configure clock settings for consistent timing
configure_clock() {
  local pi_model
  pi_model="$(detect_pi_model)"
  
  echo "Configuring clock settings for Pi $pi_model..."
  
  # Force turbo mode for consistent timing (required for both Pi 4 and 5)
  if ! check_config_setting "force_turbo"; then
    echo "Enabling force_turbo for consistent timing"
    update_config_setting "force_turbo" "1"
  else
    echo "force_turbo already configured"
  fi
  
  # Pi 5 specific: arm_boost
  if [ "$pi_model" = "5" ]; then
    if ! check_config_setting "arm_boost"; then
      echo "Enabling arm_boost for Pi 5"
      update_config_setting "arm_boost" "1"
    else
      echo "arm_boost already configured"
    fi
  fi
}

# Configure PCIe/NVME if hardware is detected
configure_nvme() {
  if ! detect_nvme_hardware; then
    echo "No NVME hardware detected, skipping PCIe configuration"
    return 0
  fi
  
  echo "NVME hardware detected, configuring PCIe..."
  
  # Enable PCIe interface
  if ! check_config_setting "dtparam=pciex1" && ! check_config_setting "dtparam=nvme"; then
    echo "Enabling PCIe interface for NVME"
    update_config_setting "dtparam" "nvme"
  else
    echo "PCIe already configured for NVME"
  fi
  
  # Check if boot order configuration is needed
  if ! $SUDO test -f /etc/default/rpi-eeprom-update; then
    echo "Note: For NVME boot, you may need to configure boot order with 'sudo raspi-config'"
    echo "Go to Advanced Options → Boot Order → NVMe/USB Boot"
  fi
}

# Enable SPI interface (required for LGPIO)
configure_spi() {
  echo "Configuring SPI interface for GPIO access..."
  
  local spi_setting="dtparam=spi"
  if ! check_config_setting "$spi_setting"; then
    echo "Enabling SPI interface"
    update_config_setting "dtparam" "spi=on"
  else
    echo "SPI already configured"
  fi
  
  # Verify SPI devices exist (may require reboot)
  if [ -e /dev/spidev0.0 ] && [ -e /dev/spidev0.1 ]; then
    echo "SPI devices found: /dev/spidev0.0 and /dev/spidev0.1"
  else
    echo "SPI devices not found yet. You may need to reboot for SPI to become available."
  fi
}

# Configure swap space for compilation (if needed)
configure_swap() {
  local current_swap
  current_swap=$(free -m | awk '/^Swap:/ {print $2}')
  
  # If less than 2GB swap and less than 8GB total memory, increase swap
  local total_memory
  total_memory=$(free -m | awk '/^Mem:/ {print $2}')
  
  if [ "$current_swap" -lt 2048 ] && [ "$total_memory" -lt 8192 ]; then
    echo "Increasing swap space for compilation (current: ${current_swap}MB, total memory: ${total_memory}MB)"
    
    # Check if dphys-swapfile is available
    if [ -f /etc/dphys-swapfile ] && [ -f /sbin/dphys-swapfile ]; then
      echo "Configuring swap via dphys-swapfile..."
      
      # Backup original
      if [ ! -f /etc/dphys-swapfile.ORIGINAL ]; then
        $SUDO cp /etc/dphys-swapfile /etc/dphys-swapfile.ORIGINAL
      fi
      
      # Set swap size to 4GB for compilation
      $SUDO sed -i 's/^CONF_SWAPSIZE=.*/CONF_SWAPSIZE=4096/' /etc/dphys-swapfile
      
      # Also update max swap if needed
      if [ -f /sbin/dphys-swapfile ]; then
        $SUDO sed -i 's/^CONF_MAXSWAP=.*/CONF_MAXSWAP=4096/' /sbin/dphys-swapfile 2>/dev/null || true
      fi
      
      echo "Swap configuration updated. Restart dphys-swapfile service..."
      $SUDO systemctl restart dphys-swapfile
    else
      echo "dphys-swapfile not available, skipping swap configuration"
    fi
  else
    echo "Sufficient swap space available (${current_swap}MB) for compilation"
  fi
}

# Check if system is configured
is_system_config_installed() {
  # Check if basic Pi configuration is done
  check_config_setting "force_turbo" && return 0
  return 1
}

# Main configuration
configure_system() {
  local pi_model
  pi_model="$(detect_pi_model)"
  
  if [ "$pi_model" = "unknown" ]; then
    echo "Warning: Could not detect Pi model. Some configurations may not apply."
  else
    echo "Detected Raspberry Pi $pi_model"
  fi
  
  echo "Configuring system settings for PiTrac..."
  
  # Configure clock settings for consistent timing
  configure_clock
  
  # Configure NVME if hardware is present
  configure_nvme
  
  # Enable SPI for GPIO access
  configure_spi
  
  # Configure swap space if needed for compilation
  configure_swap
  
  echo "System configuration completed!"
  
  # Check if reboot is recommended
  local needs_reboot=false
  if ! [ -e /dev/spidev0.0 ] && check_config_setting "dtparam=spi"; then
    needs_reboot=true
  fi
  if detect_nvme_hardware && check_config_setting "dtparam=nvme"; then
    if ! lsblk | grep -q "nvme" 2>/dev/null; then
      needs_reboot=true
    fi
  fi
  
  if [ "$needs_reboot" = true ]; then
    echo ""
    echo "*** REBOOT RECOMMENDED ***"
    echo "Some hardware changes require a reboot to take effect:"
    echo "- SPI interface for GPIO access"
    echo "- PCIe/NVME interface"
    echo ""
    echo "Run 'sudo reboot' when convenient to complete the configuration."
  fi
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  configure_system
fi