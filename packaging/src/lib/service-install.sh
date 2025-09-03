#!/usr/bin/env bash

set -euo pipefail

install_pitrac_service() {
    local install_user="${1:-$(whoami)}"
    local service_template="${2:-/usr/share/pitrac/templates/pitrac.service.template}"
    local target_service="/etc/systemd/system/pitrac.service"
    
    if [[ "$install_user" == "root" ]]; then
        if [[ -n "${SUDO_USER:-}" ]] && [[ "$SUDO_USER" != "root" ]]; then
            install_user="$SUDO_USER"
            echo "Installing service for actual user: $install_user"
        else
            echo "Error: Cannot install service for root user" >&2
            echo "Please specify a non-root user: $0 install <username>" >&2
            return 1
        fi
    fi
    
    if ! id "$install_user" &>/dev/null; then
        echo "Error: User '$install_user' does not exist" >&2
        return 1
    fi
    
    if [[ ! "$install_user" =~ ^[a-zA-Z0-9_-]+$ ]]; then
        echo "Error: Username contains invalid characters" >&2
        return 1
    fi
    
    local install_group
    install_group=$(id -gn "$install_user")
    
    local user_home
    user_home=$(getent passwd "$install_user" | cut -d: -f6)
    
    if [[ -z "$user_home" ]]; then
        echo "Error: Could not determine home directory for user '$install_user'" >&2
        return 1
    fi
    
    echo "Installing PiTrac service for user: $install_user"
    echo "  User home: $user_home"
    echo "  User group: $install_group"
    
    if [[ ! -f "$service_template" ]]; then
        echo "Error: Service template not found at $service_template" >&2
        return 1
    fi
    
    if [[ ! "$user_home" =~ ^/.+ ]]; then
        echo "Error: Invalid home directory path: $user_home" >&2
        return 1
    fi
    
    local temp_service
    temp_service=$(mktemp /tmp/pitrac.service.XXXXXX) || {
        echo "Error: Failed to create temp file" >&2
        return 1
    }
    
    # Use a function-local trap that properly handles the variable scope
    trap 'rm -f '"$temp_service"'' RETURN INT TERM
    
    local escaped_user
    local escaped_group
    local escaped_home
    escaped_user=$(printf '%s' "$install_user" | sed 's/[[\.*^$()+?{|]/\\&/g')
    escaped_group=$(printf '%s' "$install_group" | sed 's/[[\.*^$()+?{|]/\\&/g')
    escaped_home=$(printf '%s' "$user_home" | sed 's/[[\.*^$()+?{|]/\\&/g')
    
    sed -e "s|@PITRAC_USER@|$escaped_user|g" \
        -e "s|@PITRAC_GROUP@|$escaped_group|g" \
        -e "s|@PITRAC_HOME@|$escaped_home|g" \
        "$service_template" > "$temp_service"
    
    if [[ -f "$target_service" ]]; then
        local backup_service="${target_service}.bak.$(date +%s)"
        echo "Backing up existing service file to $backup_service"
        sudo cp "$target_service" "$backup_service" || {
            echo "Error: Failed to create backup" >&2
            return 1
        }
    fi
    
    echo "Installing service file to $target_service"
    if ! sudo install -m 644 "$temp_service" "$target_service"; then
        echo "Error: Failed to install service file" >&2
        if [[ -n "${backup_service:-}" ]] && [[ -f "$backup_service" ]]; then
            echo "Attempting to restore previous service file..."
            sudo cp "$backup_service" "$target_service"
        fi
        return 1
    fi
    
    # Clean up old backups (keep only last 3)
    sudo find /etc/systemd/system -name "pitrac.service.bak.*" -type f 2>/dev/null | \
        sort -r | tail -n +4 | xargs -r sudo rm -f
    
    echo "Creating required directories..."
    sudo -u "$install_user" mkdir -p \
        "$user_home/LM_Shares/Images" \
        "$user_home/LM_Shares/WebShare" \
        "$user_home/.pitrac/config" \
        "$user_home/.pitrac/state" \
        "$user_home/.pitrac/logs" \
        "$user_home/.pitrac/calibration" \
        "$user_home/.pitrac/cache"
    
    echo "Reloading systemd daemon..."
    sudo systemctl daemon-reload
    
    echo "Enabling PiTrac service..."
    sudo systemctl enable pitrac.service
    
    if ! verify_service_health; then
        echo "Warning: Service verification failed - please check configuration" >&2
    fi
    
    echo "Service installation complete!"
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

# Direct execution block removed - these functions are now called via bashly commands