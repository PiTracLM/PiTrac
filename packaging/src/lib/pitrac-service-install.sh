#!/usr/bin/env bash

set -euo pipefail

if [[ -f "$(dirname "${BASH_SOURCE[0]}")/pitrac-common-functions.sh" ]]; then
    source "$(dirname "${BASH_SOURCE[0]}")/pitrac-common-functions.sh"
else
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'
    
    log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
    log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
    log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
    log_success() { echo -e "${GREEN}[✓]${NC} $*"; }
    
    install_service_from_template() {
        log_error "Common functions not available, cannot install service"
        return 1
    }
fi

install_pitrac_service() {
    local install_user="${1:-$(whoami)}"
    
    log_info "Cleaning up any existing PiTrac processes..."
    
    if pgrep -f "pitrac_lm" > /dev/null 2>&1; then
        log_warn "Found existing pitrac_lm processes, stopping them..."
        pkill -f "pitrac_lm" 2>/dev/null || true
        sleep 1
    fi
    
    if [[ -f /var/run/pitrac/pitrac.pid ]]; then
        log_info "Removing stale PID file..."
        sudo rm -f /var/run/pitrac/pitrac.pid 2>/dev/null || true
    fi
    
    sudo mkdir -p /var/run/pitrac
    sudo chown "$install_user:$install_user" /var/run/pitrac 2>/dev/null || true
    
    if ! install_service_from_template "pitrac" "$install_user"; then
        log_error "Failed to install PiTrac service"
        return 1
    fi
    
    local user_home
    user_home=$(getent passwd "$install_user" | cut -d: -f6)
    
    log_info "Creating required directories..."
    if [[ $EUID -eq 0 ]]; then
        sudo -u "$install_user" mkdir -p \
            "$user_home/LM_Shares/Images" \
            "$user_home/LM_Shares/WebShare" \
            "$user_home/.pitrac/config" \
            "$user_home/.pitrac/state" \
            "$user_home/.pitrac/logs" \
            "$user_home/.pitrac/calibration" \
            "$user_home/.pitrac/cache"
    else
        mkdir -p \
            "$user_home/LM_Shares/Images" \
            "$user_home/LM_Shares/WebShare" \
            "$user_home/.pitrac/config" \
            "$user_home/.pitrac/state" \
            "$user_home/.pitrac/logs" \
            "$user_home/.pitrac/calibration" \
            "$user_home/.pitrac/cache"
    fi
    
    if ! verify_service_health; then
        log_warn "Service verification failed - please check configuration"
    fi
    
    log_info "Service installation complete!"
    echo ""
    echo "To start the service:"
    echo "  sudo systemctl start pitrac"
    echo ""
    echo "To check service status:"
    echo "  sudo systemctl status pitrac"
    echo ""
    echo "To view service logs:"
    echo "  sudo journalctl -u pitrac -f"
    
    return 0
}

update_service_user() {
    local new_user="${1:-$(whoami)}"
    
    echo "Updating PiTrac service to run as user: $new_user"
    
    if systemctl is-active pitrac &>/dev/null; then
        echo "Stopping PiTrac service..."
        sudo systemctl stop pitrac
    fi
    
    install_pitrac_service "$new_user"
}

uninstall_pitrac_service() {
    echo "Uninstalling PiTrac service..."
    
    if systemctl list-unit-files | grep -q pitrac.service; then
        sudo systemctl stop pitrac 2>/dev/null || true
        sudo systemctl disable pitrac 2>/dev/null || true
    fi
    
    if pgrep -f "pitrac_lm" > /dev/null 2>&1; then
        echo "Stopping remaining pitrac_lm processes..."
        pkill -f "pitrac_lm" 2>/dev/null || true
    fi
    
    if [[ -f /var/run/pitrac/pitrac.pid ]]; then
        sudo rm -f /var/run/pitrac/pitrac.pid 2>/dev/null || true
    fi
    
    if [[ -f "/etc/systemd/system/pitrac.service" ]]; then
        echo "Removing service file..."
        sudo rm -f "/etc/systemd/system/pitrac.service"
    fi
    
    sudo systemctl daemon-reload
    
    echo "Service uninstallation complete"
}

detect_environment() {
    if [[ -f /.dockerenv ]] || grep -q docker /proc/1/cgroup 2>/dev/null; then
        echo "container"
    elif [[ ! -x "$(command -v systemctl)" ]]; then
        echo "no-systemd"
    elif [[ "$EUID" -ne 0 ]] && systemctl --user status >/dev/null 2>&1; then
        echo "user-systemd"
    else
        echo "system-systemd"
    fi
}

get_service_user() {
    if [[ -f "/etc/systemd/system/pitrac.service" ]]; then
        grep "^User=" "/etc/systemd/system/pitrac.service" | cut -d= -f2
    else
        echo ""
    fi
}

is_service_installed() {
    systemctl list-unit-files | grep -q pitrac.service
}

verify_service_health() {
    local max_attempts=10
    local attempt=0
    
    echo "Verifying service configuration..."
    
    if ! sudo systemd-analyze verify pitrac.service 2>/dev/null; then
        echo "Warning: Service file has configuration issues"
    fi
    
    if ! systemctl status pitrac >/dev/null 2>&1; then
        echo "Error: Service cannot be loaded" >&2
        return 1
    fi
    
    echo "Service configuration verified successfully"
    return 0
}

# Main execution block - handle command line arguments
# Only run if called directly, not when included in pitrac main script
# Check if the script name ends with pitrac-service-install.sh
if [[ "${BASH_SOURCE[0]}" == "${0}" ]] && [[ "${0}" == *"pitrac-service-install.sh" ]]; then
    action="${1:-}"
    user="${2:-}"
    
    case "$action" in
        install)
            install_pitrac_service "$user"
            ;;
        update-user)
            update_service_user "$user"
            ;;
        uninstall)
            uninstall_pitrac_service
            ;;
        verify)
            verify_service_health
            ;;
        *)
            echo "Usage: $0 {install|update-user|uninstall|verify} [username]"
            echo ""
            echo "Actions:"
            echo "  install [user]    - Install PiTrac service for specified user"
            echo "  update-user [user] - Update service to run as different user"
            echo "  uninstall         - Remove PiTrac service"
            echo "  verify            - Verify service configuration"
            exit 1
            ;;
    esac
fi
