#!/usr/bin/env bash
set -euo pipefail

# PiTrac Hardened Installation System
# Version 2.0 - With dependency resolution and rollback support

# Ensure we are running under bash
if [ -z "${BASH_VERSION:-}" ]; then
  echo "This script must be run with bash, not sh."
  exit 1
fi

# Paths and configuration
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
DEP_RESOLVER="${SCRIPT_DIR}/scripts/dep_resolver.sh"
LOCK_FILE="${SCRIPT_DIR}/.pitrac_install.lock"
SESSION_LOG="${SCRIPT_DIR}/.session.log"

# Handle sudo for both Docker and Pi
SUDO=""
if command -v sudo >/dev/null 2>&1 && [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
fi
export DEBIAN_FRONTEND=noninteractive

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() { echo -e "${BLUE}[INFO]${NC} $*" | tee -a "$SESSION_LOG"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*" | tee -a "$SESSION_LOG"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" | tee -a "$SESSION_LOG"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $*" | tee -a "$SESSION_LOG"; }

# Initialize session
init_session() {
    echo "# PiTrac hardened installation session - $(date)" > "$SESSION_LOG"
    log_info "Starting PiTrac hardened installation system"
    
    # Check if another installation is running
    if [ -f "$LOCK_FILE" ]; then
        local lock_pid
        lock_pid=$(cat "$LOCK_FILE" 2>/dev/null || echo "")
        if [ -n "$lock_pid" ] && kill -0 "$lock_pid" 2>/dev/null; then
            log_error "Another installation is running (PID: $lock_pid)"
            exit 1
        else
            log_warn "Stale lock file found, removing"
            rm -f "$LOCK_FILE"
        fi
    fi
    
    # Create lock file
    echo $$ > "$LOCK_FILE"
    trap cleanup_session EXIT
}

cleanup_session() {
    log_info "Cleaning up session"
    rm -f "$LOCK_FILE"
}

# Enhanced package management
apt_ensure() {
  local need=()
  for p in "$@"; do 
    if ! dpkg -s "$p" >/dev/null 2>&1; then
      need+=("$p")
    fi
  done
  
  if [ "${#need[@]}" -gt 0 ]; then
    log_info "Installing system packages: ${need[*]}"
    
    # Update package lists if they're old
    local last_update
    if [ -f /var/lib/apt/periodic/update-success-stamp ]; then
      last_update=$(stat -c %Y /var/lib/apt/periodic/update-success-stamp)
    else
      last_update=0
    fi
    
    local current_time
    current_time=$(date +%s)
    
    # Update if stamp is older than 1 hour
    if [ $((current_time - last_update)) -gt 3600 ]; then
      log_info "Updating package lists..."
      $SUDO apt-get update
    fi
    
    # Install packages with retry logic
    local attempt=1
    local max_attempts=3
    
    while [ $attempt -le $max_attempts ]; do
      log_info "Installing packages (attempt $attempt/$max_attempts)..."
      
      if $SUDO apt-get install -y --no-install-recommends "${need[@]}"; then
        log_success "System packages installed successfully"
        return 0
      else
        log_warn "Package installation failed, attempt $attempt/$max_attempts"
        if [ $attempt -lt $max_attempts ]; then
          log_info "Retrying in 5 seconds..."
          sleep 5
          $SUDO apt-get update
        fi
        ((attempt++))
      fi
    done
    
    log_error "Failed to install system packages after $max_attempts attempts"
    return 1
  fi
}

# Install prerequisites
install_prerequisites() {
    log_info "Installing prerequisites..."
    
    # Essential packages for the system to function
    apt_ensure dialog curl wget ca-certificates
    
    # Install vim if requested
    if ! command -v vim >/dev/null 2>&1; then
        log_info "Installing vim..."
        apt_ensure vim
    fi
    
    # Copy vimrc if present
    if [ -f "${SCRIPT_DIR}/assets/vimrc" ]; then
        log_info "Copying vimrc to ~/.vimrc"
        cp "${SCRIPT_DIR}/assets/vimrc" ~/.vimrc
    fi
    
    # Make dependency resolver executable
    chmod +x "$DEP_RESOLVER" 2>/dev/null || true
    
    # Verify dependency resolver works
    if ! "$DEP_RESOLVER" list >/dev/null 2>&1; then
        log_error "Dependency resolver is not working properly"
        exit 1
    fi
    
    log_success "Prerequisites installed"
}

# Enhanced connectivity check
check_internet_connectivity() {
    log_info "Checking internet connectivity..."
    
    # First try ping if available
    if command -v ping >/dev/null 2>&1; then
        if ping -c 1 -W 3 8.8.8.8 >/dev/null 2>&1; then
            log_success "Internet connectivity verified (ICMP)"
            return 0
        fi
    fi
    
    # Try HTTP connectivity test
    local test_urls=(
        "http://connectivitycheck.gstatic.com/generate_204"
        "http://clients3.google.com/generate_204"
        "http://www.msftconnecttest.com/connecttest.txt"
    )
    
    for url in "${test_urls[@]}"; do
        if command -v curl >/dev/null 2>&1; then
            if curl -fsS --max-time 5 "$url" >/dev/null 2>&1; then
                log_success "Internet connectivity verified (HTTP)"
                return 0
            fi
        elif command -v wget >/dev/null 2>&1; then
            if wget -q --timeout=5 -O /dev/null "$url" 2>/dev/null; then
                log_success "Internet connectivity verified (HTTP)"
                return 0
            fi
        fi
    done
    
    log_error "Internet connectivity check failed"
    log_error "Please check your network connection and try again"
    exit 1
}

# Enhanced menu system with dependency awareness
show_installation_status() {
    local package="$1"
    if "$DEP_RESOLVER" verify "$package" >/dev/null 2>&1; then
        echo "[INSTALLED]"
    else
        echo "[NOT INSTALLED]"
    fi
}

# Menu configuration
BUILD="6.12.34-1"
VERSION="2.0-hardened"
TITLE="PiTrac Hardened Installer - Raspberry Pi OS >= $BUILD"
HEIGHT=20
WIDTH=70
MENU_HEIGHT=12

# Available packages
declare -A PACKAGES=(
    ["activemq-broker"]="ActiveMQ Broker 6.1.7"
    ["activemq-cpp"]="ActiveMQ C++ CMS"
    ["boost"]="Boost 1.74 Libraries"
    ["opencv"]="OpenCV 4.11.0"
    ["msgpack"]="MessagePack C++"
    ["lgpio"]="LGPIO Library"
    ["libcamera"]="Libcamera & RpiCam Apps"
    ["java"]="Java 17 OpenJDK"
    ["maven"]="Apache Maven"
    ["tomee"]="Apache TomEE 10.1.0"
    ["pitrac-deps"]="PiTrac Dependencies"
)

declare -A CONFIG_PACKAGES=(
    ["system-config"]="Pi System Configuration"
    ["camera-config"]="Camera Configuration"
    ["pitrac-environment"]="PiTrac Environment Setup"
    ["network-services"]="Network Services (NAS/Samba/SSH)"
    ["dev-environment"]="Development Environment"
)

show_main_menu() {
    while true; do
        local main_options=(
            1 "Install Software"
            2 "System Configuration"
            3 "Build PiTrac"
            4 "Run PiTrac Launch Monitor"
            5 "Verify Installations"
            6 "System Maintenance"
            7 "Test Image Processing (No Camera)"
            8 "View Logs"
            9 "Exit"
        )
        
        local main_choice
        main_choice=$(dialog --clear \
            --title "$TITLE" \
            --ok-label "OK" \
            --cancel-label "Exit" \
            --menu "Version $VERSION" \
            $HEIGHT $WIDTH $MENU_HEIGHT \
            "${main_options[@]}" \
            2>&1 >/dev/tty)
            
        local exit_code=$?
        if [ $exit_code -ne 0 ]; then
            clear
            log_info "Exiting installer"
            exit 0
        fi
        
        case "$main_choice" in
            1) show_install_menu ;;
            2) show_config_menu ;;
            3) build_pitrac_menu ;;
            4) run_pitrac_menu ;;
            5) show_verify_menu ;;
            6) show_maintenance_menu ;;
            7) show_test_processing_menu ;;
            8) show_logs_menu ;;
            9) clear; exit 0 ;;
            *) log_error "Invalid selection: $main_choice" ;;
        esac
    done
}

show_install_menu() {
    while true; do
        local install_options=()
        install_options+=(1 "Install ALL Dependencies")
        
        local i=2
        for package in "${!PACKAGES[@]}"; do
            local status
            status=$(show_installation_status "$package")
            install_options+=($i "${PACKAGES[$package]} $status")
            ((i++))
        done
        install_options+=($i "Go Back to Main Menu")
        
        local install_choice
        install_choice=$(dialog --clear \
            --title "PiTrac Installation Menu" \
            --menu "Select what to install:" \
            $HEIGHT $WIDTH $MENU_HEIGHT \
            "${install_options[@]}" \
            2>&1 >/dev/tty)
            
        local exit_code=$?
        if [ $exit_code -ne 0 ] || [ "$install_choice" -eq $i ]; then
            break
        fi
        
        clear
        
        if [ "$install_choice" -eq 1 ]; then
            # Install all dependencies
            dialog --yesno "This will install ALL PiTrac dependencies. This may take a long time. Continue?" 8 60
            if [ $? -eq 0 ]; then
                install_all_dependencies
            fi
        else
            # Install specific package
            local package_index=$((install_choice - 2))
            local packages_array=(${!PACKAGES[@]})
            local selected_package="${packages_array[$package_index]}"
            
            if [ -n "$selected_package" ]; then
                dialog --yesno "Install ${PACKAGES[$selected_package]}?" 7 50
                if [ $? -eq 0 ]; then
                    install_single_package "$selected_package"
                fi
            fi
        fi
        
        echo
        read -rp "Press Enter to continue..."
    done
}

show_config_menu() {
    while true; do
        local config_options=()
        config_options+=(1 "Configure ALL System Components")
        
        local i=2
        for package in "${!CONFIG_PACKAGES[@]}"; do
            local status
            status=$(show_installation_status "$package")
            config_options+=($i "${CONFIG_PACKAGES[$package]} $status")
            ((i++))
        done
        config_options+=($i "Go Back to Main Menu")
        
        local config_choice
        config_choice=$(dialog --clear \
            --title "System Configuration Menu" \
            --menu "Select configuration to apply:" \
            $HEIGHT $WIDTH $MENU_HEIGHT \
            "${config_options[@]}" \
            2>&1 >/dev/tty)
            
        local exit_code=$?
        if [ $exit_code -ne 0 ] || [ "$config_choice" -eq $i ]; then
            break
        fi
        
        clear
        
        if [ "$config_choice" -eq 1 ]; then
            dialog --yesno "This will configure ALL system components. This may require user input. Continue?" 8 60
            if [ $? -eq 0 ]; then
                configure_all_system
            fi
        else
            local package_index=$((config_choice - 2))
            local packages_array=(${!CONFIG_PACKAGES[@]})
            local selected_package="${packages_array[$package_index]}"
            
            if [ -n "$selected_package" ]; then
                dialog --yesno "Configure ${CONFIG_PACKAGES[$selected_package]}?" 7 50
                if [ $? -eq 0 ]; then
                    configure_single_component "$selected_package"
                fi
            fi
        fi
        
        echo
        read -rp "Press Enter to continue..."
    done
}

configure_all_system() {
    log_info "Configuring all system components..."
    
    local all_configs=(
        "system-config" "camera-config" "pitrac-environment" 
        "network-services" "dev-environment"
    )
    
    for config in "${all_configs[@]}"; do
        log_info "Configuring $config..."
        if ! "$DEP_RESOLVER" install "$config"; then
            log_error "Failed to configure $config"
            dialog --msgbox "Configuration failed at component: $config\\nCheck logs for details." 8 50
            return 1
        fi
    done
    
    log_success "All system components configured successfully!"
    dialog --msgbox "All system components have been configured successfully!" 8 50
}

configure_single_component() {
    local component="$1"
    
    log_info "Configuring $component..."
    
    if "$DEP_RESOLVER" install "$component"; then
        log_success "$component configured successfully"
        dialog --msgbox "${CONFIG_PACKAGES[$component]} configured successfully!" 8 50
    else
        log_error "Failed to configure $component"
        dialog --msgbox "Failed to configure ${CONFIG_PACKAGES[$component]}\\nCheck logs for details." 8 50
    fi
}

install_all_dependencies() {
    log_info "Installing all PiTrac dependencies..."
    
    # Define the complete dependency chain
    local all_packages=(
        "java" "maven" "boost" "activemq-broker" "activemq-cpp" 
        "msgpack" "lgpio" "opencv" "libcamera" "tomee" "pitrac-deps"
    )
    
    for package in "${all_packages[@]}"; do
        log_info "Installing $package and its dependencies..."
        if ! "$DEP_RESOLVER" install "$package"; then
            log_error "Failed to install $package"
            dialog --msgbox "Installation failed at package: $package\nCheck logs for details." 8 50
            return 1
        fi
    done
    
    log_success "All dependencies installed successfully!"
    dialog --msgbox "All PiTrac dependencies have been installed successfully!" 8 50
}

install_single_package() {
    local package="$1"
    
    log_info "Installing $package..."
    
    if "$DEP_RESOLVER" install "$package"; then
        log_success "$package installed successfully"
        dialog --msgbox "${PACKAGES[$package]} installed successfully!" 8 50
    else
        log_error "Failed to install $package"
        dialog --msgbox "Failed to install ${PACKAGES[$package]}\nCheck logs for details." 8 50
    fi
}

build_pitrac_menu() {
    # Check if dependencies are installed first
    local missing_deps=()
    for dep in opencv libcamera msgpack lgpio activemq-cpp; do
        if ! "$DEP_RESOLVER" verify "$dep" >/dev/null 2>&1; then
            missing_deps+=("$dep")
        fi
    done
    
    if [ ${#missing_deps[@]} -gt 0 ]; then
        dialog --msgbox "Cannot build PiTrac yet!\n\nMissing dependencies:\n${missing_deps[*]}\n\nPlease install software first (option 1)" 12 50
        return
    fi
    
    # Check if configuration is done
    if [ ! -f "$HOME/.bashrc" ] || ! grep -q "PITRAC_ROOT" "$HOME/.bashrc" 2>/dev/null; then
        if [ ! -f "$HOME/.zshrc" ] || ! grep -q "PITRAC_ROOT" "$HOME/.zshrc" 2>/dev/null; then
            dialog --msgbox "PiTrac environment not configured!\n\nPlease run System Configuration first (option 2)" 10 50
            return
        fi
    fi
    
    dialog --infobox "Starting PiTrac build process..." 3 40
    
    # Run the build script
    if "${SCRIPT_DIR}/scripts/build_pitrac.sh"; then
        dialog --msgbox "PiTrac build completed successfully!\n\nYou can now test the launch monitor:\n\$PITRAC_ROOT/ImageProcessing/build/pitrac_lm --help" 10 60
    else
        dialog --msgbox "PiTrac build failed!\n\nCheck the logs for details:\n/tmp/pitrac_build.log" 10 50
    fi
}

run_pitrac_menu() {
    # Check if PiTrac is built using dep_resolver
    if ! "$DEP_RESOLVER" verify pitrac-build >/dev/null 2>&1; then
        dialog --msgbox "PiTrac is not built yet!\n\nPlease build PiTrac first (option 3)" 8 50
        return
    fi
    
    local run_options=(
        1 "Run Single-Pi Setup"
        2 "Run Two-Pi Setup (Camera 1)"
        3 "Run Two-Pi Setup (Camera 2)"
        4 "Start Camera 1 (Background)"
        5 "Start Camera 2 (Background)"
        6 "View Running Processes"
        7 "View Background Logs"
        8 "Stop All PiTrac Processes"
        9 "Test Strobe Light"
        10 "Test Camera Trigger"
        11 "Configure Run Settings"
        12 "Show Command-Line Help"
        13 "Go Back to Main Menu"
    )
    
    local run_choice
    run_choice=$(dialog --clear \
        --title "Run PiTrac Launch Monitor" \
        --menu "Select operation mode:" \
        $HEIGHT $WIDTH $MENU_HEIGHT \
        "${run_options[@]}" \
        2>&1 >/dev/tty)
        
    local exit_code=$?
    if [ $exit_code -ne 0 ] || [ "$run_choice" -eq 13 ]; then
        return 0
    fi
    
    clear
    
    case "$run_choice" in
        1)
            log_info "Starting PiTrac in Single-Pi mode..."
            log_info "Press Ctrl+C to stop"
            "${SCRIPT_DIR}/scripts/run_pitrac.sh" run
            ;;
        2)
            log_info "Starting Camera 1 (Primary Pi)..."
            log_warn "Make sure Camera 2 is running on the secondary Pi first!"
            log_info "Press Ctrl+C to stop"
            "${SCRIPT_DIR}/scripts/run_pitrac.sh" cam1
            ;;
        3)
            log_info "Starting Camera 2 (Secondary Pi)..."
            log_info "Start this BEFORE starting Camera 1!"
            log_info "Press Ctrl+C to stop"
            "${SCRIPT_DIR}/scripts/run_pitrac.sh" cam2
            ;;
        4)
            log_info "Starting Camera 1 in background..."
            nohup "${SCRIPT_DIR}/scripts/run_pitrac.sh" cam1 > /tmp/pitrac_cam1.log 2>&1 &
            local cam1_pid=$!
            echo "$cam1_pid" > /tmp/pitrac_cam1.pid
            log_success "Camera 1 started with PID: $cam1_pid"
            log_info "Log: /tmp/pitrac_cam1.log"
            sleep 2
            ;;
        5)
            log_info "Starting Camera 2 in background..."
            nohup "${SCRIPT_DIR}/scripts/run_pitrac.sh" cam2 > /tmp/pitrac_cam2.log 2>&1 &
            local cam2_pid=$!
            echo "$cam2_pid" > /tmp/pitrac_cam2.pid
            log_success "Camera 2 started with PID: $cam2_pid"
            log_info "Log: /tmp/pitrac_cam2.log"
            sleep 2
            ;;
        6)
            log_info "PiTrac Running Processes:"
            echo ""
            
            # Check Camera 1
            if [ -f /tmp/pitrac_cam1.pid ]; then
                local pid=$(cat /tmp/pitrac_cam1.pid)
                if kill -0 "$pid" 2>/dev/null; then
                    log_success "Camera 1: Running (PID: $pid)"
                else
                    log_warn "Camera 1: Not running (stale PID file)"
                fi
            else
                log_info "Camera 1: Not started"
            fi
            
            # Check Camera 2
            if [ -f /tmp/pitrac_cam2.pid ]; then
                local pid=$(cat /tmp/pitrac_cam2.pid)
                if kill -0 "$pid" 2>/dev/null; then
                    log_success "Camera 2: Running (PID: $pid)"
                else
                    log_warn "Camera 2: Not running (stale PID file)"
                fi
            else
                log_info "Camera 2: Not started"
            fi
            
            # Check for any pitrac_lm processes
            echo ""
            log_info "All pitrac_lm processes:"
            pgrep -f pitrac_lm && pgrep -f pitrac_lm -a || echo "  None found"
            ;;
        7)
            log_info "Background Process Logs:"
            echo ""
            
            local log_choice
            log_choice=$(dialog --clear \
                --title "View Background Logs" \
                --menu "Select log to view:" \
                10 50 3 \
                1 "Camera 1 Log" \
                2 "Camera 2 Log" \
                3 "Back" \
                2>&1 >/dev/tty)
            
            case "$log_choice" in
                1)
                    if [ -f /tmp/pitrac_cam1.log ]; then
                        less /tmp/pitrac_cam1.log
                    else
                        log_warn "No Camera 1 log found"
                    fi
                    ;;
                2)
                    if [ -f /tmp/pitrac_cam2.log ]; then
                        less /tmp/pitrac_cam2.log
                    else
                        log_warn "No Camera 2 log found"
                    fi
                    ;;
            esac
            ;;
        8)
            log_warn "Stopping all PiTrac processes..."
            
            # Stop using PID files
            for pidfile in /tmp/pitrac_cam1.pid /tmp/pitrac_cam2.pid; do
                if [ -f "$pidfile" ]; then
                    local pid=$(cat "$pidfile")
                    if kill -0 "$pid" 2>/dev/null; then
                        kill "$pid"
                        log_info "Stopped process $pid"
                    fi
                    rm -f "$pidfile"
                fi
            done
            
            # Also kill any remaining pitrac_lm processes
            pkill -f pitrac_lm 2>/dev/null && log_info "Killed remaining pitrac_lm processes"
            
            log_success "All PiTrac processes stopped"
            ;;
        9)
            log_info "Testing strobe light..."
            "${SCRIPT_DIR}/scripts/run_pitrac.sh" test-strobe
            ;;
        10)
            "${SCRIPT_DIR}/scripts/run_pitrac.sh" test-trigger
            ;;
        11)
            dialog --msgbox "Edit run configuration:\n\n${SCRIPT_DIR}/scripts/defaults/run-pitrac.yaml\n\nKey settings:\n- pi_mode: single or dual\n- logging_level: info, debug, trace\n- auto_restart: enable auto-restart on failure" 14 60
            ;;
        12)
            "${SCRIPT_DIR}/scripts/run_pitrac.sh" help
            ;;
    esac
    
    echo
    read -rp "Press Enter to continue..."
}

show_verify_menu() {
    local verify_options=()
    local i=1
    
    for package in "${!PACKAGES[@]}"; do
        local status
        status=$(show_installation_status "$package")
        verify_options+=($i "${PACKAGES[$package]} $status")
        ((i++))
    done
    
    for package in "${!CONFIG_PACKAGES[@]}"; do
        local status
        status=$(show_installation_status "$package")
        verify_options+=($i "${CONFIG_PACKAGES[$package]} $status")
        ((i++))
    done
    
    verify_options+=($i "Verify All Components")
    verify_options+=($(($i + 1)) "Go Back to Main Menu")
    
    local verify_choice
    verify_choice=$(dialog --clear \
        --title "Verification Menu" \
        --menu "Select package to verify:" \
        $HEIGHT $WIDTH $MENU_HEIGHT \
        "${verify_options[@]}" \
        2>&1 >/dev/tty)
        
    local exit_code=$?
    if [ $exit_code -ne 0 ] || [ "$verify_choice" -eq $(($i + 1)) ]; then
        return 0
    fi
    
    clear
    
    if [ "$verify_choice" -eq $i ]; then
        # Verify all packages
        verify_all_components
    else
        local package_index=$((verify_choice - 1))
        
        local all_packages_array=(${!PACKAGES[@]} ${!CONFIG_PACKAGES[@]})
        local selected_package="${all_packages_array[$package_index]}"
        
        if [ -n "$selected_package" ]; then
            verify_single_component "$selected_package"
        fi
    fi
    
    echo
    read -rp "Press Enter to continue..."
}

verify_all_components() {
    log_info "Verifying all installed components..."
    
    local all_good=true
    
    for package in "${!PACKAGES[@]}"; do
        if "$DEP_RESOLVER" verify "$package" >/dev/null 2>&1; then
            log_success "${PACKAGES[$package]}: OK"
        else
            log_error "${PACKAGES[$package]}: FAILED"
            all_good=false
        fi
    done
    
    for package in "${!CONFIG_PACKAGES[@]}"; do
        if "$DEP_RESOLVER" verify "$package" >/dev/null 2>&1; then
            log_success "${CONFIG_PACKAGES[$package]}: OK"
        else
            log_error "${CONFIG_PACKAGES[$package]}: FAILED"
            all_good=false
        fi
    done
    
    if $all_good; then
        log_success "All components verified successfully"
    else
        log_warn "Some components failed verification"
    fi
}

verify_single_component() {
    local package="$1"
    
    log_info "Verifying $package..."
    
    local description=""
    if [[ -v PACKAGES[$package] ]]; then
        description="${PACKAGES[$package]}"
    elif [[ -v CONFIG_PACKAGES[$package] ]]; then
        description="${CONFIG_PACKAGES[$package]}"
    else
        description="$package"
    fi
    
    if "$DEP_RESOLVER" verify "$package"; then
        log_success "$description verified successfully"
    else
        log_error "$description verification failed"
    fi
}

show_maintenance_menu() {
    local maintenance_options=(
        1 "Update Package Lists"
        2 "Clean Package Cache"
        3 "Show Disk Usage"
        4 "Rollback Last Installation"
        5 "Reset Installation State"
        6 "Go Back to Main Menu"
    )
    
    local maintenance_choice
    maintenance_choice=$(dialog --clear \
        --title "System Maintenance" \
        --menu "Select maintenance task:" \
        $HEIGHT $WIDTH $MENU_HEIGHT \
        "${maintenance_options[@]}" \
        2>&1 >/dev/tty)
        
    local exit_code=$?
    if [ $exit_code -ne 0 ] || [ "$maintenance_choice" -eq 6 ]; then
        return 0
    fi
    
    clear
    
    case "$maintenance_choice" in
        1) 
            log_info "Updating package lists..."
            $SUDO apt-get update
            log_success "Package lists updated"
            ;;
        2)
            log_info "Cleaning package cache..."
            $SUDO apt-get clean
            $SUDO apt-get autoclean
            log_success "Package cache cleaned"
            ;;
        3)
            log_info "Disk usage summary:"
            df -h
            echo
            log_info "Package cache usage:"
            du -sh /var/cache/apt/archives/
            ;;
        4)
            dialog --yesno "This will attempt to rollback the last installation. Continue?" 7 60
            if [ $? -eq 0 ]; then
                "$DEP_RESOLVER" rollback
            fi
            ;;
        5)
            dialog --yesno "This will reset all installation state. You may need to reinstall everything. Continue?" 8 70
            if [ $? -eq 0 ]; then
                rm -f "${SCRIPT_DIR}/scripts/.install.log"
                rm -f "${SCRIPT_DIR}/scripts/.rollback.log"
                log_info "Installation state reset"
            fi
            ;;
    esac
    
    echo
    read -rp "Press Enter to continue..."
}

show_test_processing_menu() {
    chmod +x "${SCRIPT_DIR}/scripts/test_image_processor.sh" 2>/dev/null || true
    chmod +x "${SCRIPT_DIR}/scripts/test_results_server.py" 2>/dev/null || true
    
    # Ensure test-processor dependencies are installed
    if ! "$DEP_RESOLVER" verify test-processor >/dev/null 2>&1; then
        dialog --yesno "Test processor dependencies not installed.\n\nInstall now?" 8 50
        if [ $? -eq 0 ]; then
            clear
            log_info "Installing test processor dependencies..."
            if ! "$DEP_RESOLVER" install test-processor; then
                log_error "Failed to install test processor dependencies"
                read -rp "Press Enter to continue..."
                return 1
            fi
        else
            return 0
        fi
    fi
    
    local test_options=(
        1 "Quick Test with Default Images"
        2 "Test with Custom Images"
        3 "List Available Test Images"
        4 "View Latest Results"
        5 "Start Results Web Server"
        6 "Go Back to Main Menu"
    )
    
    local test_choice
    test_choice=$(dialog --clear \
        --title "Test Image Processing (No Camera)" \
        --menu "Select test option:" \
        $HEIGHT $WIDTH $MENU_HEIGHT \
        "${test_options[@]}" \
        2>&1 >/dev/tty)
        
    local exit_code=$?
    if [ $exit_code -ne 0 ] || [ "$test_choice" -eq 6 ]; then
        return 0
    fi
    
    clear
    
    local local_ip
    local_ip=$(hostname -I 2>/dev/null | awk '{print $1}')
    [ -z "$local_ip" ] && local_ip="localhost"
    
    case "$test_choice" in
        1)
            log_info "Running quick test with default images..."
            "${SCRIPT_DIR}/scripts/test_image_processor.sh" quick
            if [ $? -eq 0 ]; then
                log_success "Test completed successfully"
                echo ""
                log_info "To view results in browser, run option 5 (Start Results Web Server)"
            fi
            ;;
        2)
            log_info "Test with custom images"
            
            "${SCRIPT_DIR}/scripts/test_image_processor.sh" list
            
            echo ""
            echo "Press Enter to continue to image selection..."
            read -r
            
            local teed_img
            local strobed_img
            
            local default_teed="${TEST_DIR:-${PITRAC_ROOT:-$(dirname "$SCRIPT_DIR")}/TestImages}/custom/teed.png"
            local default_strobed="${TEST_DIR:-${PITRAC_ROOT:-$(dirname "$SCRIPT_DIR")}/TestImages}/custom/strobed.png"
            
            teed_img=$(dialog --inputbox "Enter path to teed ball image:" 8 70 "$default_teed" 2>&1 >/dev/tty)
            if [ $? -ne 0 ]; then
                log_info "Cancelled"
                return 0
            fi
            
            strobed_img=$(dialog --inputbox "Enter path to strobed image:" 8 70 "$default_strobed" 2>&1 >/dev/tty)
            if [ $? -ne 0 ]; then
                log_info "Cancelled"
                return 0
            fi
            
            clear
            
            if [ -n "$teed_img" ] && [ -n "$strobed_img" ]; then
                "${SCRIPT_DIR}/scripts/test_image_processor.sh" custom "$teed_img" "$strobed_img"
            else
                log_warn "Test cancelled - images not specified"
            fi
            ;;
        3)
            log_info "Listing available test images..."
            "${SCRIPT_DIR}/scripts/test_image_processor.sh" list
            ;;
        4)
            log_info "Viewing latest test results..."
            "${SCRIPT_DIR}/scripts/test_image_processor.sh" results
            ;;
        5)
            log_info "Starting test results web server..."
            
            # Check if running over SSH
            if [ -n "${SSH_CONNECTION:-}" ]; then
                dialog --msgbox "Web server on port 8080\n\nSSH tunnel:\nssh -L 8080:localhost:8080 $(whoami)@${local_ip}\n\nDirect:\nhttp://${local_ip}:8080\n\nCtrl+C to stop" 12 60
            else
                dialog --msgbox "Web server on port 8080\n\nLocal: http://localhost:8080\nNetwork: http://${local_ip}:8080\n\nCtrl+C to stop" 10 50
            fi
            
            python3 "${SCRIPT_DIR}/scripts/test_results_server.py"
            ;;
    esac
    
    echo
    read -rp "Press Enter to continue..."
}

show_logs_menu() {
    local log_files=(
        "${SESSION_LOG}"
        "${SCRIPT_DIR}/scripts/.install.log"
        "/var/log/apt/history.log"
    )
    
    local log_options=(
        1 "View Current Session Log"
        2 "View Installation Log"
        3 "View APT History"
        4 "View All Logs"
        5 "Go Back to Main Menu"
    )
    
    local log_choice
    log_choice=$(dialog --clear \
        --title "View Logs" \
        --menu "Select log to view:" \
        $HEIGHT $WIDTH $MENU_HEIGHT \
        "${log_options[@]}" \
        2>&1 >/dev/tty)
        
    local exit_code=$?
    if [ $exit_code -ne 0 ] || [ "$log_choice" -eq 5 ]; then
        return 0
    fi
    
    clear
    
    case "$log_choice" in
        1|2|3)
            local log_file="${log_files[$((log_choice - 1))]}"
            if [ -f "$log_file" ]; then
                log_info "Viewing: $log_file"
                less "$log_file"
            else
                log_warn "Log file not found: $log_file"
            fi
            ;;
        4)
            log_info "Viewing all available logs:"
            for log_file in "${log_files[@]}"; do
                if [ -f "$log_file" ]; then
                    echo "=== $log_file ==="
                    tail -n 20 "$log_file"
                    echo
                fi
            done | less
            ;;
    esac
}

# Main
main() {
    # Initialize session
    init_session
    
    # Perform initial checks
    check_internet_connectivity
    install_prerequisites
    
    # Show menu
    show_main_menu
    
    # Cleanup
    clear
    log_success "PiTrac installer session completed"
}

# Handle signals gracefully
trap 'log_error "Installation interrupted"; cleanup_session; exit 1' INT TERM

# Run if called directly
main "$@"
