#!/usr/bin/env bash
set -euo pipefail

# Apache TomEE Installation Script
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Load defaults from config file
load_defaults "tomee" "$@"

TOMEE_VERSION="${TOMEE_VERSION:-10.1.0}"
DISTRIBUTION="${DISTRIBUTION:-plume}"
FILE_NAME="apache-tomee-${TOMEE_VERSION}-${DISTRIBUTION}.zip"
URL="https://dlcdn.apache.org/tomee/tomee-${TOMEE_VERSION}/${FILE_NAME}"
INSTALL_DIR="${INSTALL_DIR:-/opt/tomee}"
MARKER="${INSTALL_DIR}/.tomee-version"
FORCE="${FORCE:-0}"


get_installed_version() {
  # 1) Our marker (most reliable)
  if [ -f "$MARKER" ]; then
    cat "$MARKER"
    return 0
  fi
  # 2) Try to infer from jar names (best-effort)
  local j
  j="$(ls -1 "${INSTALL_DIR}"/lib/tomee-*.jar 2>/dev/null | head -n1 || true)"
  if [ -n "$j" ]; then
    # matches tomee-10.1.0.jar -> 10.1.0
    basename "$j" | grep -Eo '[0-9]+(\.[0-9]+)+' || true
    return 0
  fi
  # 3) Try to parse from RELEASE-NOTES if present
  if [ -f "${INSTALL_DIR}/RELEASE-NOTES" ]; then
    grep -Eom1 'TomEE[[:space:]]+([0-9]+(\.[0-9]+)+)' "${INSTALL_DIR}/RELEASE-NOTES" | grep -Eo '[0-9]+(\.[0-9]+)+' || true
    return 0
  fi
  echo ""
}

# Check if tomee is installed
is_tomee_installed() {
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

# Pre-installation checks - returns 0 to proceed, 1 to skip
precheck() {
  if [ -d "$INSTALL_DIR" ]; then
    local cur; cur="$(get_installed_version || true)"
    if [ -n "$cur" ] && version_ge "$cur" "$TOMEE_VERSION"; then
      echo "TomEE ${cur} already installed at ${INSTALL_DIR} (>= ${TOMEE_VERSION}). Skipping."
      return 1  # Skip installation
    fi
    if [ "$FORCE" = "1" ]; then
      echo "Existing TomEE${cur:+ ${cur}} found; upgrading to ${TOMEE_VERSION} (FORCE=1)."
      backup_existing
      return 0  # Proceed with installation
    else
      echo "TomEE${cur:+ ${cur}} found at ${INSTALL_DIR}. Set FORCE=1 to upgrade to ${TOMEE_VERSION}."
      return 1  # Skip installation
    fi
  fi
  return 0  # No existing installation, proceed
}

# Install TomEE
install_tomee() {
  # Run pre-flight checks
  run_preflight_checks "tomee" || return 1

  echo "Ensuring prerequisites..."
  apt_ensure wget unzip ca-certificates file

  local WORK
  WORK="$(create_temp_dir "tomee")"
  cd "$WORK"

  echo "Downloading TomEE ${TOMEE_VERSION}..."
  download_with_progress ""$URL"" ""$FILE_NAME""

  echo "Verifying archive..."
  file "$FILE_NAME" | grep -qi 'Zip archive data' || {
    log_error "Downloaded file isn't a zip archive (mirror may have returned HTML)."
    return 1
  }

  echo "Extracting TomEE..."
  unzip -q "$FILE_NAME"

  local SRC_DIR="apache-tomee-plume-${TOMEE_VERSION}"
  [ -d "$SRC_DIR" ] || { 
    log_error "'${SRC_DIR}' not found after unzip."
    return 1
  }

  log_info "Installing to ${INSTALL_DIR}..."
  $SUDO mkdir -p "$(dirname "$INSTALL_DIR")"
  $SUDO mv "$SRC_DIR" "$INSTALL_DIR"

  # Write version marker for reliable detection
  echo "${TOMEE_VERSION}" | $SUDO tee "$MARKER" >/dev/null

  echo "TomEE ${TOMEE_VERSION} installed at ${INSTALL_DIR}"
  echo "Start: ${INSTALL_DIR}/bin/startup.sh"
  echo "Stop : ${INSTALL_DIR}/bin/shutdown.sh"
}

# Validate installation
validate_installation() {
  if [ -d "$INSTALL_DIR" ] && [ -x "${INSTALL_DIR}/bin/catalina.sh" ]; then
    local cur
    cur="$(get_installed_version || true)"
    echo "Successfully installed TomEE${cur:+ ${cur}} at ${INSTALL_DIR}!"
  else
    echo "Error: Failed to install TomEE."
    return 1
  fi
}

# Configure TomEE for web application deployment
configure_web_access() {
  local server_config="${INSTALL_DIR}/conf/server.xml"
  local context_config="${INSTALL_DIR}/conf/context.xml" 
  local users_config="${INSTALL_DIR}/conf/tomcat-users.xml"
  local manager_context="${INSTALL_DIR}/webapps/manager/META-INF/context.xml"
  
  echo "Configuring TomEE for web applications..."
  
  # Configure tomcat-users.xml for web management
  if [ -f "$users_config" ]; then
    echo "Configuring TomEE user accounts..."
    
    # Backup original
    if [ ! -f "${users_config}.ORIGINAL" ]; then
      $SUDO cp "$users_config" "${users_config}.ORIGINAL"
    fi
    
    # Remove existing users and add new configuration
    $SUDO sed -i '/<\/tomcat-users>/d' "$users_config"
    
    # Add management users
    cat << 'EOF' | $SUDO tee -a "$users_config" >/dev/null
  <role rolename="tomcat"/>
  <role rolename="admin-gui"/>
  <role rolename="manager-gui"/>
  <user username="tomcat" password="tomcat" roles="tomcat,admin-gui,manager-gui"/>
</tomcat-users>
EOF
    echo "TomEE user accounts configured (tomcat/tomcat)"
  fi
  
  # Configure manager access from remote hosts
  if [ -f "$manager_context" ]; then
    echo "Configuring remote access to TomEE manager..."
    
    # Backup original
    if [ ! -f "${manager_context}.ORIGINAL" ]; then
      $SUDO cp "$manager_context" "${manager_context}.ORIGINAL"
    fi
    
    # Allow access from any IP (change 127.0.0.1 restriction to .*)
    $SUDO sed -i 's/allow="[^"]*"/allow=".*"/g' "$manager_context"
    echo "Remote access to TomEE manager enabled"
  fi
  
  # Configure context.xml for symbolic linking
  if [ -f "$context_config" ]; then
    echo "Enabling symbolic linking in TomEE..."
    
    # Backup original
    if [ ! -f "${context_config}.ORIGINAL" ]; then
      $SUDO cp "$context_config" "${context_config}.ORIGINAL"
    fi
    
    # Add Resources allowLinking if not present
    if ! grep -q "allowLinking" "$context_config"; then
      $SUDO sed -i '/<\/Context>/i\    <Resources allowLinking="true" />' "$context_config"
      echo "Symbolic linking enabled"
    fi
  fi
  
  # Configure server.xml for PiTrac web share
  configure_pitrac_web_context "$server_config"
  
  # Disable verbose access logging
  disable_access_logging "$server_config"
  
  echo "Web configuration completed"
}

# Configure PiTrac web context in server.xml
configure_pitrac_web_context() {
  local server_config="$1"
  
  if [ ! -f "$server_config" ]; then
    echo "Warning: server.xml not found"
    return 0
  fi
  
  echo "Configuring PiTrac web context..."
  
  # Backup original
  if [ ! -f "${server_config}.ORIGINAL" ]; then
    $SUDO cp "$server_config" "${server_config}.ORIGINAL"
  fi
  
  # Get username for web share path
  local username
  username="$(whoami)"
  local web_share_path="/home/$username/LM_Shares/WebShare"
  
  # Remove existing PiTrac context if present
  $SUDO sed -i '/<Context.*golfsim.*WebShare.*\/>/d' "$server_config"
  
  # Add PiTrac web context before </Host>
  $SUDO sed -i "/<\/Host>/i\\        <Context docBase=\"$web_share_path\" path=\"/golfsim/WebShare\" />" "$server_config"
  
  echo "PiTrac web context configured at /golfsim/WebShare"
  echo "  Maps to: $web_share_path"
}

# Disable verbose access logging
disable_access_logging() {
  local server_config="$1"
  
  if [ ! -f "$server_config" ]; then
    return 0
  fi
  
  echo "Disabling verbose access logging..."
  
  # Comment out AccessLogValve to reduce log noise
  $SUDO sed -i 's/<Valve className="org.apache.catalina.valves.AccessLogValve"/<!-- <Valve className="org.apache.catalina.valves.AccessLogValve"/g' "$server_config"
  $SUDO sed -i 's/pattern="%h %l %u %t &quot;%r&quot; %s %b" \/>/pattern="%h %l %u %t &quot;%r&quot; %s %b" \/> -->/g' "$server_config"
  
  echo "Access logging disabled"
}

# Set up systemctl service for TomEE
setup_tomee_service() {
  local service_file="/etc/systemd/system/tomee.service"
  
  echo "Setting up TomEE systemctl service..."
  
  # Detect Java home
  local java_home
  if [ -d "/usr/lib/jvm/java-17-openjdk-arm64" ]; then
    java_home="/usr/lib/jvm/java-17-openjdk-arm64"
  elif [ -d "/usr/lib/jvm/java-17-openjdk-amd64" ]; then
    java_home="/usr/lib/jvm/java-17-openjdk-amd64" 
  elif [ -d "/usr/lib/jvm/default-java" ]; then
    java_home="/usr/lib/jvm/default-java"
  else
    java_home="$(dirname $(dirname $(readlink -f $(which java))))"
  fi
  
  echo "Using Java home: $java_home"
  
  # Create service file
  $SUDO tee "$service_file" >/dev/null << EOF
[Unit]
Description=Apache TomEE Application Server
After=network.target

[Service]
User=root
Type=forking
Restart=on-failure
RestartSec=10

# Java and TomEE environment
Environment=JAVA_HOME=$java_home
Environment=CATALINA_PID=${INSTALL_DIR}/temp/tomee.pid
Environment=CATALINA_HOME=${INSTALL_DIR}
Environment=CATALINA_BASE=${INSTALL_DIR}
Environment=CATALINA_OPTS='-server -Xmx1024m'
Environment=JAVA_OPTS='-Djava.awt.headless=true -Dfile.encoding=UTF-8'

# Service commands
ExecStart=${INSTALL_DIR}/bin/startup.sh
ExecStop=${INSTALL_DIR}/bin/shutdown.sh
KillSignal=SIGTERM
TimeoutStopSec=30

# Ensure proper working directory and permissions
WorkingDirectory=${INSTALL_DIR}

[Install]
WantedBy=multi-user.target
EOF

  # Set proper permissions on TomEE directories
  echo "Setting TomEE permissions..."
  $SUDO chmod -R 755 "$INSTALL_DIR"
  $SUDO chmod -R go+w "${INSTALL_DIR}/webapps"
  $SUDO chmod -R go+w "${INSTALL_DIR}/temp"
  $SUDO chmod -R go+w "${INSTALL_DIR}/logs"
  $SUDO chmod -R go+w "${INSTALL_DIR}/work"
  
  # Reload systemd and enable service
  $SUDO systemctl daemon-reload
  
  echo "Starting and enabling TomEE service..."
  $SUDO systemctl enable tomee
  
  # Start the service
  if $SUDO systemctl start tomee; then
    echo "TomEE service started successfully"
    
    # Wait for startup and check status
    echo "Waiting for TomEE to start..."
    sleep 10
    
    if $SUDO systemctl is-active tomee >/dev/null; then
      echo "TomEE service is running"
      
      # Show connection information
      local pi_ip
      pi_ip=$(hostname -I | awk '{print $1}')
      echo ""
      echo "TomEE Manager: http://$pi_ip:8080/manager/html"
      echo "TomEE Console: http://$pi_ip:8080/"
      echo "Login: tomcat/tomcat"
      echo "PiTrac WebShare: http://$pi_ip:8080/golfsim/WebShare/"
    else
      log_warn "TomEE service started but may not be healthy"
      echo "Check status with: sudo systemctl status tomee"
      echo "Check logs with: sudo tail -f ${INSTALL_DIR}/logs/catalina.out"
    fi
  else
    log_warn "Failed to start TomEE service"
    echo "Check logs with: sudo journalctl -u tomee"
  fi
}

# Verify TomEE installation and service
verify_tomee_service() {
  echo "Verifying TomEE installation..."
  
  # Check if TomEE startup script is executable
  if [ -x "${INSTALL_DIR}/bin/startup.sh" ]; then
    echo "TomEE startup script is executable"
  else
    log_error "TomEE startup script not found or not executable"
    return 1
  fi
  
  # Check service status
  if systemctl is-enabled tomee >/dev/null 2>&1; then
    echo "TomEE service is enabled"
  else
    log_warn "TomEE service is not enabled"
  fi
  
  # Check if TomEE is listening on port 8080
  if netstat -an 2>/dev/null | grep -q ":8080.*LISTEN"; then
    echo "TomEE listening on port 8080"
  else
    log_warn "TomEE not listening on port 8080"
  fi
  
  # Check web share directory
  local username
  username="$(whoami)"
  local web_share_path="/home/$username/LM_Shares/WebShare"
  
  if [ -d "$web_share_path" ]; then
    echo "PiTrac web share directory exists: $web_share_path"
  else
    log_warn "PiTrac web share directory missing: $web_share_path"
    echo "  Create it with: mkdir -p $web_share_path"
  fi
  
  echo "TomEE verification completed"
}

# Main installation
install_tomee_full() {
  # Run pre-flight checks
  run_preflight_checks "tomee" || return 1

  if precheck; then
    # precheck returned 0, meaning we should proceed with installation
    install_tomee
    configure_web_access
    setup_tomee_service
    validate_installation
    verify_tomee_service
    
    echo ""
    echo "TomEE installation and configuration completed!"
    echo ""
    echo "Service Management:"
    echo "- Start:   sudo systemctl start tomee"
    echo "- Stop:    sudo systemctl stop tomee"
    echo "- Status:  sudo systemctl status tomee" 
    echo "- Logs:    sudo tail -f ${INSTALL_DIR}/logs/catalina.out"
    echo ""
    echo "Web Access:"
    local pi_ip
    pi_ip=$(hostname -I | awk '{print $1}')
    echo "- Manager: http://$pi_ip:8080/manager/html (tomcat/tomcat)"
    echo "- Console: http://$pi_ip:8080/"
  else
    # precheck returned 1, meaning we should skip installation
    echo "TomEE installation skipped (already satisfied)"
    return 0
  fi
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  install_tomee_full
fi
