#!/bin/bash
# Script to uninstall tar.gz-based dependencies from previous installations
# This prepares the system for the new deb-based installation

set -euo pipefail

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
log_success() { echo -e "${GREEN}[✓]${NC} $*"; }

# Check if running with sudo
if [[ $EUID -ne 0 ]]; then
    log_error "This script must be run with sudo privileges"
    log_info "Please run: sudo $0"
    exit 1
fi

log_info "Uninstalling tar.gz-based dependencies from previous PiTrac installations"
echo ""

# Remove libraries from /usr/lib/pitrac
if [[ -d /usr/lib/pitrac ]]; then
    log_info "Cleaning /usr/lib/pitrac libraries..."

    # Remove OpenCV libraries
    rm -f /usr/lib/pitrac/libopencv*.so* 2>/dev/null || true

    # Remove ActiveMQ-CPP libraries
    rm -f /usr/lib/pitrac/libactivemq*.so* 2>/dev/null || true

    # Remove lgpio libraries
    rm -f /usr/lib/pitrac/liblgpio*.so* 2>/dev/null || true

    log_success "Cleaned /usr/lib/pitrac"
else
    log_info "/usr/lib/pitrac not found - skipping"
fi

# Remove extracted headers from /opt
if [[ -d /opt/opencv ]]; then
    log_info "Removing /opt/opencv..."
    rm -rf /opt/opencv
    log_success "Removed /opt/opencv"
else
    log_info "/opt/opencv not found - skipping"
fi

if [[ -d /opt/activemq-cpp ]]; then
    log_info "Removing /opt/activemq-cpp..."
    rm -rf /opt/activemq-cpp
    log_success "Removed /opt/activemq-cpp"
else
    log_info "/opt/activemq-cpp not found - skipping"
fi

# Remove any custom pkg-config files that may have been created
if [[ -d /usr/lib/pkgconfig ]]; then
    log_info "Cleaning custom pkg-config files..."
    rm -f /usr/lib/pkgconfig/opencv4.pc 2>/dev/null || true
    rm -f /usr/lib/pkgconfig/activemq-cpp.pc 2>/dev/null || true
    rm -f /usr/lib/pkgconfig/lgpio.pc 2>/dev/null || true
fi

if [[ -d /usr/local/lib/pkgconfig ]]; then
    rm -f /usr/local/lib/pkgconfig/opencv4.pc 2>/dev/null || true
    rm -f /usr/local/lib/pkgconfig/activemq-cpp.pc 2>/dev/null || true
    rm -f /usr/local/lib/pkgconfig/lgpio.pc 2>/dev/null || true
fi

# Update library cache
log_info "Updating library cache..."
ldconfig

log_success "Tar.gz-based dependencies uninstalled"
echo ""
echo "The system is now ready for deb package installation."
echo "You can now run: sudo ./build.sh dev"
echo ""
echo "Note: This script only removes the old dependency installations."
echo "      PiTrac binaries and configuration files were not touched."