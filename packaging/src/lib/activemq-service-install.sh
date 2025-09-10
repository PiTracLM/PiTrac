#!/usr/bin/env bash
# ActiveMQ service configuration installer for PiTrac

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
log_success() { echo -e "${GREEN}[✓]${NC} $*"; }

DEFAULT_BROKER_NAME="localhost"
DEFAULT_BIND_ADDRESS="0.0.0.0"
DEFAULT_PORT="61616"
DEFAULT_STOMP_PORT="61613"
DEFAULT_LOG_LEVEL="INFO"
DEFAULT_CONSOLE_LOG_LEVEL="WARN"
DEFAULT_PITRAC_PASSWORD="pitrac123"

install_activemq_config() {
    local install_user="${1:-activemq}"
    local config_dir="${2:-/etc/activemq/instances-available/main}"
    local data_dir="${3:-/var/lib/activemq}"
    
    log_info "Installing ActiveMQ configuration for PiTrac"
    log_info "  User: $install_user"
    log_info "  Config directory: $config_dir"
    log_info "  Data directory: $data_dir"
    
    if ! command -v activemq &>/dev/null && [[ ! -f /usr/share/activemq/bin/activemq ]]; then
        log_error "ActiveMQ is not installed. Please install with: apt install activemq"
        return 1
    fi
    
    if ! id "$install_user" &>/dev/null; then
        log_warn "User '$install_user' does not exist. Will be created by ActiveMQ package."
    fi
    
    log_info "Creating ActiveMQ directories..."
    sudo mkdir -p "$config_dir"
    sudo mkdir -p "$data_dir"/{main,conf,data,tmp,kahadb}
    sudo mkdir -p "$data_dir/log"
    sudo mkdir -p /etc/activemq/instances-enabled
    
    local template_dir="${PITRAC_TEMPLATE_DIR:-/usr/share/pitrac/templates}"
    if [[ ! -d "$template_dir" ]]; then
        template_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../templates" && pwd)"
    fi
    
    local activemq_xml_template="$template_dir/activemq.xml.template"
    local log4j_template="$template_dir/log4j2.properties.template"
    local options_template="$template_dir/activemq-options.template"
    
    if [[ ! -f "$activemq_xml_template" ]]; then
        log_error "ActiveMQ XML template not found at: $activemq_xml_template"
        return 1
    fi
    
    if [[ ! -f "$log4j_template" ]]; then
        log_error "Log4j2 template not found at: $log4j_template"
        return 1
    fi
    
    if [[ ! -f "$options_template" ]]; then
        log_warn "ActiveMQ options template not found at: $options_template"
    fi
    
    local broker_name="${ACTIVEMQ_BROKER_NAME:-$DEFAULT_BROKER_NAME}"
    local bind_address="${ACTIVEMQ_BIND_ADDRESS:-$DEFAULT_BIND_ADDRESS}"
    local port="${ACTIVEMQ_PORT:-$DEFAULT_PORT}"
    local stomp_port="${ACTIVEMQ_STOMP_PORT:-$DEFAULT_STOMP_PORT}"
    local log_level="${ACTIVEMQ_LOG_LEVEL:-$DEFAULT_LOG_LEVEL}"
    local console_log_level="${ACTIVEMQ_CONSOLE_LOG_LEVEL:-$DEFAULT_CONSOLE_LOG_LEVEL}"
    local pitrac_password="${ACTIVEMQ_PITRAC_PASSWORD:-$DEFAULT_PITRAC_PASSWORD}"
    local log_dir="$data_dir/log"
    
    log_info "Configuring ActiveMQ with:"
    log_info "  Broker name: $broker_name"
    log_info "  Bind address: $bind_address"
    log_info "  OpenWire port: $port"
    log_info "  STOMP port: $stomp_port"
    log_info "  Log level: $log_level"
    
    local temp_xml=$(mktemp /tmp/activemq.xml.XXXXXX)
    local temp_log4j=$(mktemp /tmp/log4j2.properties.XXXXXX)
    local temp_options=$(mktemp /tmp/activemq-options.XXXXXX)
    
    trap 'rm -f '"${temp_xml}"' '"${temp_log4j}"' '"${temp_options}"'' EXIT INT TERM
    
    escape_for_sed() {
        printf '%s' "$1" | sed 's/[[\.*^$()+?{|]/\\&/g'
    }
    
    log_info "Processing ActiveMQ configuration template..."
    sed -e "s|@ACTIVEMQ_BROKER_NAME@|$(escape_for_sed "$broker_name")|g" \
        -e "s|@ACTIVEMQ_DATA_DIR@|$(escape_for_sed "$data_dir")|g" \
        -e "s|@ACTIVEMQ_BIND_ADDRESS@|$(escape_for_sed "$bind_address")|g" \
        -e "s|@ACTIVEMQ_PORT@|$(escape_for_sed "$port")|g" \
        -e "s|@ACTIVEMQ_STOMP_PORT@|$(escape_for_sed "$stomp_port")|g" \
        -e "s|@ACTIVEMQ_PITRAC_PASSWORD@|$(escape_for_sed "$pitrac_password")|g" \
        "$activemq_xml_template" > "$temp_xml"
    
    log_info "Processing Log4j2 configuration template..."
    sed -e "s|@ACTIVEMQ_USER@|$(escape_for_sed "$install_user")|g" \
        -e "s|@ACTIVEMQ_LOG_DIR@|$(escape_for_sed "$log_dir")|g" \
        -e "s|@ACTIVEMQ_LOG_LEVEL@|$(escape_for_sed "$log_level")|g" \
        -e "s|@ACTIVEMQ_CONSOLE_LOG_LEVEL@|$(escape_for_sed "$console_log_level")|g" \
        "$log4j_template" > "$temp_log4j"
    
    if [[ -f "$options_template" ]]; then
        log_info "Processing ActiveMQ options template..."
        sed -e "s|@ACTIVEMQ_INSTANCE_NAME@|main|g" \
            -e "s|@ACTIVEMQ_DATA_DIR@|$(escape_for_sed "$data_dir")|g" \
            -e "s|@ACTIVEMQ_LOG_DIR@|$(escape_for_sed "$log_dir")|g" \
            "$options_template" > "$temp_options"
    fi
    
    if [[ -f "$config_dir/activemq.xml" ]]; then
        local backup_file="$config_dir/activemq.xml.bak.$(date +%s)"
        log_info "Backing up existing activemq.xml to $backup_file"
        sudo cp "$config_dir/activemq.xml" "$backup_file"
    fi
    
    if [[ -f "$config_dir/log4j2.properties" ]]; then
        local backup_file="$config_dir/log4j2.properties.bak.$(date +%s)"
        log_info "Backing up existing log4j2.properties to $backup_file"
        sudo cp "$config_dir/log4j2.properties" "$backup_file"
    fi
    
    log_info "Installing ActiveMQ configuration files..."
    sudo install -m 644 -o root -g root "$temp_xml" "$config_dir/activemq.xml"
    sudo install -m 644 -o root -g root "$temp_log4j" "$config_dir/log4j2.properties"
    
    if [[ -f "$temp_options" ]]; then
        sudo install -m 644 -o root -g root "$temp_options" "$config_dir/options"
    fi
    
    if [[ ! -f "$config_dir/jetty.xml" ]]; then
        log_info "Creating basic jetty.xml for web console..."
        sudo tee "$config_dir/jetty.xml" > /dev/null <<'EOF'
<beans xmlns="http://www.springframework.org/schema/beans"
       xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
       xsi:schemaLocation="http://www.springframework.org/schema/beans http://www.springframework.org/schema/beans/spring-beans.xsd">
    
    <bean id="jettyPort" class="org.apache.activemq.web.WebConsolePort" init-method="start">
        <property name="host" value="0.0.0.0"/>
        <property name="port" value="8161"/>
    </bean>
    
</beans>
EOF
    fi
    
    if [[ ! -f "$config_dir/credentials.properties" ]]; then
        log_info "Creating credentials.properties..."
        sudo tee "$config_dir/credentials.properties" > /dev/null <<EOF
# ActiveMQ credentials
activemq.username=admin
activemq.password=admin
guest.password=guest
EOF
        sudo chmod 600 "$config_dir/credentials.properties"
    fi
    
    log_info "Copying configurations to ActiveMQ instance directories..."
    sudo cp -f "$config_dir"/*.xml "$data_dir/main/" 2>/dev/null || true
    sudo cp -f "$config_dir"/*.properties "$data_dir/main/" 2>/dev/null || true
    sudo cp -f "$config_dir"/*.xml "$data_dir/conf/" 2>/dev/null || true
    sudo cp -f "$config_dir"/*.properties "$data_dir/conf/" 2>/dev/null || true
    
    if [[ ! -e /etc/activemq/instances-enabled/main ]]; then
        log_info "Enabling ActiveMQ main instance..."
        sudo ln -sf /etc/activemq/instances-available/main /etc/activemq/instances-enabled/main
    fi
    
    if id "$install_user" &>/dev/null; then
        log_info "Setting ownership for ActiveMQ directories..."
        sudo chown -R "$install_user:$install_user" "$data_dir"
        sudo chown -R "$install_user:$install_user" "$config_dir"
    fi
    
    log_info "Cleaning up old configuration backups..."
    sudo find "$config_dir" -name "*.bak.*" -type f 2>/dev/null | \
        sort -r | tail -n +4 | xargs -r sudo rm -f
    
    log_success "ActiveMQ configuration installed successfully!"
    
    echo ""
    log_info "Next steps:"
    log_info "  1. Restart ActiveMQ service: sudo systemctl restart activemq"
    log_info "  2. Check service status: sudo systemctl status activemq"
    log_info "  3. View logs: sudo journalctl -u activemq -f"
    log_info "  4. Access web console: http://localhost:8161/admin (admin/admin)"
    
    return 0
}

update_activemq_config() {
    local install_user="${1:-activemq}"
    
    log_info "Updating ActiveMQ configuration..."
    
    if systemctl is-active --quiet activemq 2>/dev/null; then
        log_info "Stopping ActiveMQ service for configuration update..."
        sudo systemctl stop activemq
    fi
    
    install_activemq_config "$install_user"
    
    log_info "Restarting ActiveMQ service..."
    sudo systemctl start activemq
    
    return 0
}

verify_activemq_config() {
    local config_dir="/etc/activemq/instances-available/main"
    local data_dir="/var/lib/activemq"
    
    log_info "Verifying ActiveMQ configuration..."
    
    local errors=0
    
    if [[ ! -f "$config_dir/activemq.xml" ]]; then
        log_error "Missing activemq.xml in $config_dir"
        ((errors++))
    fi
    
    if [[ ! -f "$config_dir/log4j2.properties" ]]; then
        log_error "Missing log4j2.properties in $config_dir"
        ((errors++))
    fi
    
    if [[ ! -e /etc/activemq/instances-enabled/main ]]; then
        log_error "Main instance not enabled"
        ((errors++))
    fi
    
    for dir in "$data_dir" "$data_dir/main" "$data_dir/conf" "$data_dir/data" "$data_dir/tmp"; do
        if [[ ! -d "$dir" ]]; then
            log_error "Missing directory: $dir"
            ((errors++))
        fi
    done
    
    local service_found=0
    
    if systemctl list-units --all 2>/dev/null | grep "activemq.service" >/dev/null 2>&1; then
        service_found=1
    elif systemctl list-unit-files 2>/dev/null | grep "activemq.service" >/dev/null 2>&1; then
        service_found=1
    elif [[ -f /etc/init.d/activemq ]]; then
        service_found=1
    elif systemctl status activemq 2>&1 | grep -q "Generated by systemd-sysv-generator"; then
        service_found=1
    fi
    
    if [[ $service_found -eq 0 ]]; then
        log_error "ActiveMQ service not found"
        ((errors++))
    fi
    
    if command -v xmllint &>/dev/null; then
        if ! xmllint --noout "$config_dir/activemq.xml" 2>/dev/null; then
            log_error "Invalid XML in activemq.xml"
            ((errors++))
        fi
    fi
    
    if [[ $errors -eq 0 ]]; then
        log_success "ActiveMQ configuration verified successfully!"
        
        if systemctl is-active --quiet activemq; then
            log_success "ActiveMQ service is running"
            
            if netstat -tln 2>/dev/null | grep -q ":61616 "; then
                log_success "ActiveMQ broker is listening on port 61616"
            else
                log_warn "ActiveMQ broker not listening on port 61616"
            fi
        else
            log_warn "ActiveMQ service is not running"
        fi
        
        return 0
    else
        log_error "Found $errors configuration errors"
        return 1
    fi
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]] && [[ "${0}" == *"activemq-service-install.sh" ]]; then
    action="${1:-}"
    user="${2:-activemq}"
    
    case "$action" in
        install)
            install_activemq_config "$user"
            ;;
        update)
            update_activemq_config "$user"
            ;;
        verify)
            verify_activemq_config
            ;;
        *)
            echo "Usage: $0 {install|update|verify} [username]"
            echo ""
            echo "Actions:"
            echo "  install [user]    - Install ActiveMQ configuration"
            echo "  update [user]     - Update ActiveMQ configuration"
            echo "  verify            - Verify ActiveMQ configuration"
            exit 1
            ;;
    esac
fi
