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

# Remove lgpio files installed by tar.gz extraction
log_info "Removing lgpio files from system locations..."
# Headers
rm -f /usr/include/lgpio.h /usr/include/rgpio.h 2>/dev/null || true
rm -f /usr/local/include/lgpio.h /usr/local/include/rgpio.h 2>/dev/null || true
# Libraries
rm -f /usr/lib/liblgpio.so* /usr/lib/librgpio.so* 2>/dev/null || true
rm -f /usr/local/lib/liblgpio.so* /usr/local/lib/librgpio.so* 2>/dev/null || true
# Binaries
rm -f /usr/bin/rgpiod /usr/bin/rgs 2>/dev/null || true
rm -f /usr/local/bin/rgpiod /usr/local/bin/rgs 2>/dev/null || true
# Man pages
rm -rf /usr/share/man/man1/rgs.1* 2>/dev/null || true
rm -rf /usr/share/man/man3/lgpio.3* /usr/share/man/man3/rgpio.3* 2>/dev/null || true
rm -rf /usr/local/share/man/man1/rgs.1* 2>/dev/null || true
rm -rf /usr/local/share/man/man3/lgpio.3* /usr/local/share/man/man3/rgpio.3* 2>/dev/null || true
log_success "Removed lgpio files"

# Remove msgpack headers (header-only library)
log_info "Removing msgpack-cxx headers..."
rm -rf /usr/include/msgpack* 2>/dev/null || true
rm -rf /usr/local/include/msgpack* 2>/dev/null || true
rm -rf /usr/include/msgpack.hpp 2>/dev/null || true
rm -rf /usr/local/include/msgpack.hpp 2>/dev/null || true
log_success "Removed msgpack headers"

# Remove ActiveMQ-CPP headers and libraries
log_info "Removing ActiveMQ-CPP files from system locations..."
rm -rf /usr/include/activemq-cpp* 2>/dev/null || true
rm -rf /usr/local/include/activemq-cpp* 2>/dev/null || true
rm -f /usr/lib/libactivemq-cpp.so* 2>/dev/null || true
rm -f /usr/local/lib/libactivemq-cpp.so* 2>/dev/null || true
log_success "Removed ActiveMQ-CPP files"

# Remove OpenCV files if installed from tar.gz (be careful not to remove system opencv)
log_info "Removing custom OpenCV 4.11 installation..."
# Only remove if it's our custom version (4.11)
if [[ -d /usr/include/opencv4 ]] && grep -q "4.11" /usr/include/opencv4/opencv2/core/version.hpp 2>/dev/null; then
    rm -rf /usr/include/opencv4 2>/dev/null || true
    log_success "Removed OpenCV 4.11 headers"
fi
if [[ -d /usr/local/include/opencv4 ]] && grep -q "4.11" /usr/local/include/opencv4/opencv2/core/version.hpp 2>/dev/null; then
    rm -rf /usr/local/include/opencv4 2>/dev/null || true
fi
# Remove OpenCV libraries with version 4.11
rm -f /usr/lib/libopencv*4.11*.so* 2>/dev/null || true
rm -f /usr/local/lib/libopencv*4.11*.so* 2>/dev/null || true

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