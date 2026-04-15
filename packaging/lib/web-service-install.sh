#!/usr/bin/env bash
# PiTrac Web Service Installer

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

install_web_service() {
    local install_user="${1:-$(whoami)}"
    
    if ! install_service_from_template "pitrac-web" "$install_user"; then
        log_error "Failed to install web service"
        return 1
    fi
    
    log_info "Web service installation complete!"
    echo ""
    echo "To start the service:"
    echo "  sudo systemctl start pitrac-web"
    echo ""
    echo "To check service status:"
    echo "  sudo systemctl status pitrac-web"
    echo ""
    echo "To view service logs:"
    echo "  sudo journalctl -u pitrac-web -f"
    
    return 0
}

update_web_service_user() {
    local new_user="${1:-$(whoami)}"
    
    echo "Updating PiTrac Web service to run as user: $new_user"
    
    if systemctl is-active pitrac-web &>/dev/null; then
        echo "Stopping PiTrac Web service..."
        sudo systemctl stop pitrac-web
    fi
    
    install_web_service "$new_user"
}

uninstall_web_service() {
    echo "Uninstalling PiTrac Web service..."
    
    if systemctl list-unit-files | grep -q pitrac-web.service; then
        sudo systemctl stop pitrac-web 2>/dev/null || true
        sudo systemctl disable pitrac-web 2>/dev/null || true
    fi
    
    if [[ -f "/etc/systemd/system/pitrac-web.service" ]]; then
        echo "Removing service file..."
        sudo rm -f "/etc/systemd/system/pitrac-web.service"
    fi
    
    if [[ -d "/etc/systemd/system/pitrac-web.service.d" ]]; then
        echo "Removing service overrides..."
        sudo rm -rf "/etc/systemd/system/pitrac-web.service.d"
    fi
    
    sudo systemctl daemon-reload
    
    echo "Web service uninstallation complete"
}

verify_web_service() {
    echo "Verifying PiTrac Web service configuration..."
    
    local errors=0
    
    if [[ ! -f "/etc/systemd/system/pitrac-web.service" ]]; then
        log_error "Service file not found"
        ((errors++))
    fi
    
    if [[ ! -d "/usr/lib/pitrac/web-server" ]]; then
        log_error "Web server directory not found"
        ((errors++))
    fi
    
    if [[ ! -f "/usr/lib/pitrac/web-server/main.py" ]]; then
        log_error "Web server main.py not found"
        ((errors++))
    fi
    
    if ! python3 -c "import flask" 2>/dev/null; then
        log_warn "Flask not installed - run: pip3 install -r /usr/lib/pitrac/web-server/requirements.txt"
        ((errors++))
    fi
    
    if ! sudo systemd-analyze verify pitrac-web.service 2>/dev/null; then
        log_warn "Service file has configuration warnings"
    fi
    
    if [[ $errors -eq 0 ]]; then
        log_success "Web service configuration verified successfully!"
        
        if systemctl is-active --quiet pitrac-web; then
            log_success "Web service is running"
        else
            log_info "Web service is not running (start with: sudo systemctl start pitrac-web)"
        fi
        
        return 0
    else
        log_error "Found $errors configuration errors"
        return 1
    fi
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]] && [[ "${0}" == *"web-service-install.sh" ]]; then
    action="${1:-}"
    user="${2:-}"
    
    case "$action" in
        install)
            install_web_service "$user"
            ;;
        update-user)
            update_web_service_user "$user"
            ;;
        uninstall)
            uninstall_web_service
            ;;
        verify)
            verify_web_service
            ;;
        *)
            echo "Usage: $0 {install|update-user|uninstall|verify} [username]"
            echo ""
            echo "Actions:"
            echo "  install [user]    - Install web service for specified user"
            echo "  update-user [user] - Update service to run as different user"
            echo "  uninstall         - Remove web service"
            echo "  verify            - Verify service configuration"
            exit 1
            ;;
    esac
fi
