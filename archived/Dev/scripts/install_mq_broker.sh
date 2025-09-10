#!/usr/bin/env bash
set -euo pipefail

# ActiveMQ Broker Installation Script
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Load defaults from config file
load_defaults "activemq-broker" "$@"

# Use loaded defaults or environment overrides
ACTIVEMQ_VERSION="${ACTIVEMQ_VERSION:-6.1.7}"
INSTALL_DIR="${INSTALL_DIR:-/opt/apache-activemq}"
FILENAME="apache-activemq-${ACTIVEMQ_VERSION}-bin.tar.gz"
URL="https://www.apache.org/dyn/closer.cgi?filename=/activemq/${ACTIVEMQ_VERSION}/${FILENAME}&action=download"
FORCE="${FORCE:-0}"


get_installed_version() {
  # Try the activemq script
  if [ -x "${INSTALL_DIR}/bin/activemq" ]; then
    local out ver
    out="$("${INSTALL_DIR}/bin/activemq" --version 2>/dev/null || true)"
    ver="$(grep -Eo '[0-9]+(\.[0-9]+)+' <<<"$out" | head -n1 || true)"
    [ -n "$ver" ] && { echo "$ver"; return 0; }
  fi
  # Fallback: try to infer from directory contents/jars (best-effort)
  ver="$(basename "${INSTALL_DIR}" | grep -Eo '[0-9]+(\.[0-9]+)+' || true)"
  [ -n "$ver" ] && { echo "$ver"; return 0; }
  echo ""
  return 1
}

# Check if activemq is installed
is_activemq_broker_installed() {
  local ver
  ver="$(get_installed_version)"
  [ -n "$ver" ] && return 0
  return 1
}


# Backup existing installation
backup_existing() {
  local ts backup
  ts="$(date +%Y%m%d-%H%M%S)"
  backup="${INSTALL_DIR}.bak.${ts}"
  echo "Backing up existing install to: ${backup}"
  $SUDO mv "$INSTALL_DIR" "$backup"
}

# Pre-installation checks
precheck() {
  if [ -d "$INSTALL_DIR" ]; then
    local cur
    cur="$(get_installed_version || true)"
    if [ -n "$cur" ]; then
      if version_ge "$cur" "$ACTIVEMQ_VERSION"; then
        echo "ActiveMQ ${cur} already installed at ${INSTALL_DIR} (>= ${ACTIVEMQ_VERSION}). Skipping."
        return 1  # Return 1 to indicate skip
      else
        if [ "$FORCE" = "1" ]; then
          echo "ActiveMQ ${cur} found (< ${ACTIVEMQ_VERSION}). Will upgrade (FORCE=1)."
          backup_existing
          return 0  # Return 0 to indicate proceed
        else
          echo "ActiveMQ ${cur} found at ${INSTALL_DIR}. Set FORCE=1 to upgrade to ${ACTIVEMQ_VERSION}."
          return 1  # Return 1 to indicate skip
        fi
      fi
    else
      echo "ActiveMQ present at ${INSTALL_DIR}, but version unknown."
      if [ "$FORCE" = "1" ]; then
        backup_existing
        return 0  # Return 0 to indicate proceed
      else
        echo "Set FORCE=1 to overwrite. Aborting."
        return 1  # Return 1 to indicate skip
      fi
    fi
  fi
  return 0  # No existing installation, proceed
}

# Install ActiveMQ broker
install_activemq_broker() {
  # Run pre-flight checks
  run_preflight_checks "mq_broker" || return 1

  log_info "Installing prerequisite packages..."
  apt_ensure wget ca-certificates tar file

  local WORK
  WORK="$(create_temp_dir "activemq")"
  cd "$WORK"

  echo "Downloading ${FILENAME}..."
  download_with_progress ""$URL"" ""$FILENAME""

  echo "Verifying archive..."
  file "$FILENAME" | grep -qi 'gzip compressed data' || {
    log_error "Downloaded file isn't a gzip tarball (mirror may have returned HTML)."
    return 1
  }

  echo "Extracting ActiveMQ ${ACTIVEMQ_VERSION}..."
  tar -xzf "$FILENAME"

  local SRC_DIR="apache-activemq-${ACTIVEMQ_VERSION}"
  [ -d "$SRC_DIR" ] || { 
    log_error "'${SRC_DIR}' not found after extract."
    return 1
  }

  log_info "Installing to ${INSTALL_DIR}..."
  $SUDO mkdir -p "$(dirname "$INSTALL_DIR")"
  $SUDO mv "$SRC_DIR" "$INSTALL_DIR"

  # Verify installation
  if [ -x "${INSTALL_DIR}/bin/activemq" ]; then
    echo "ActiveMQ installed at: ${INSTALL_DIR}"
    echo "Start: ${INSTALL_DIR}/bin/activemq start"
    echo "Stop : ${INSTALL_DIR}/bin/activemq stop"
  else
    log_error "activemq script not found/executable in ${INSTALL_DIR}/bin"
    return 1
  fi
}

# Configure ActiveMQ for remote access
configure_remote_access() {
  local jetty_config="${INSTALL_DIR}/conf/jetty.xml"
  
  if [ ! -f "$jetty_config" ]; then
    echo "Warning: jetty.xml not found at $jetty_config"
    return 0
  fi
  
  echo "Configuring ActiveMQ for remote access..."
  
  # Backup original
  if [ ! -f "${jetty_config}.ORIGINAL" ]; then
    $SUDO cp "$jetty_config" "${jetty_config}.ORIGINAL"
  fi
  
  # Get Pi IP address
  local pi_ip
  pi_ip=$(hostname -I | awk '{print $1}' 2>/dev/null || echo "0.0.0.0")
  
  echo "Configuring ActiveMQ to listen on: $pi_ip"
  
  # Replace 127.0.0.1 with actual IP or 0.0.0.0 for all interfaces
  $SUDO sed -i "s/127\.0\.0\.1/$pi_ip/g" "$jetty_config"
  
  # Enable HTTPS connector if not already enabled
  if grep -q "Enable this connector if you wish to use https" "$jetty_config"; then
    echo "Enabling HTTPS connector..."
    # This is a more complex sed operation to uncomment the HTTPS section
    # For now, we'll just note it needs manual configuration
    echo "Note: HTTPS connector may need manual configuration in $jetty_config"
  fi
  
  echo "Remote access configuration completed"
}

# Set up systemctl service
setup_systemctl_service() {
  local service_file="/etc/systemd/system/activemq.service"
  
  echo "Setting up ActiveMQ systemctl service..."
  
  # Create service file
  $SUDO tee "$service_file" >/dev/null << EOF
[Unit]
Description=ActiveMQ Message Broker
After=network.target

[Service]
User=root
Type=forking
Restart=on-failure
RestartSec=10
ExecStart=${INSTALL_DIR}/bin/activemq start
ExecStop=${INSTALL_DIR}/bin/activemq stop
KillSignal=SIGTERM
TimeoutStopSec=30

# Ensure ActiveMQ starts in its own directory
WorkingDirectory=${INSTALL_DIR}

[Install]
WantedBy=multi-user.target
EOF

  # Reload systemd and enable service
  $SUDO systemctl daemon-reload
  
  echo "Starting and enabling ActiveMQ service..."
  $SUDO systemctl enable activemq
  
  # Start the service
  if $SUDO systemctl start activemq; then
    echo "ActiveMQ service started successfully"
    
    # Wait a moment and check status
    sleep 3
    if $SUDO systemctl is-active activemq >/dev/null; then
      echo "ActiveMQ service is running"
      
      # Show connection information
      local pi_ip
      pi_ip=$(hostname -I | awk '{print $1}')
      echo ""
      echo "ActiveMQ Web Console: http://$pi_ip:8161/admin"
      echo "Default login: admin/admin"
      echo "ActiveMQ Broker URL: tcp://$pi_ip:61616"
    else
      log_warn "ActiveMQ service started but may not be healthy"
      echo "Check status with: sudo systemctl status activemq"
    fi
  else
    log_warn "Failed to start ActiveMQ service"
    echo "Check logs with: sudo journalctl -u activemq"
  fi
}

# Verify ActiveMQ installation and service
verify_activemq_service() {
  echo "Verifying ActiveMQ installation..."
  
  # Check if ActiveMQ binary is executable
  if [ -x "${INSTALL_DIR}/bin/activemq" ]; then
    echo "ActiveMQ binary is executable"
  else
    log_error "ActiveMQ binary not found or not executable"
    return 1
  fi
  
  # Check service status
  if systemctl is-enabled activemq >/dev/null 2>&1; then
    echo "ActiveMQ service is enabled"
  else
    log_warn "ActiveMQ service is not enabled"
  fi
  
  # Check if ActiveMQ is listening on expected ports
  if netstat -an 2>/dev/null | grep -q ":61616.*LISTEN"; then
    echo "ActiveMQ broker listening on port 61616"
  else
    log_warn "ActiveMQ broker not listening on port 61616"
  fi
  
  if netstat -an 2>/dev/null | grep -q ":8161.*LISTEN"; then
    echo "ActiveMQ web console listening on port 8161"
  else
    log_warn "ActiveMQ web console not listening on port 8161"
  fi
  
  echo "ActiveMQ verification completed"
}

# Main installation
install_activemq_full() {
  # Run pre-flight checks
  run_preflight_checks "mq_broker" || return 1

  if precheck; then
    # precheck returned 0, meaning we should proceed with installation
    install_activemq_broker
    configure_remote_access
    setup_systemctl_service
    verify_activemq_service
    
    echo ""
    echo "ActiveMQ installation and configuration completed!"
    echo ""
    echo "Service Management:"
    echo "- Start:   sudo systemctl start activemq"
    echo "- Stop:    sudo systemctl stop activemq" 
    echo "- Status:  sudo systemctl status activemq"
    echo "- Logs:    sudo journalctl -u activemq -f"
  else
    # precheck returned 1, meaning we should skip installation
    echo "Installation skipped based on precheck."
    return 0
  fi
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  install_activemq_full
fi
