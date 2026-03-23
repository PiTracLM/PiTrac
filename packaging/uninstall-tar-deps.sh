#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#
# Uninstall Tarball-Based Dependencies
#
# This script removes old tarball-based installations from /opt/
# that conflict with the new DEB package installations in /usr/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/src/lib/pitrac-common-functions.sh"

echo "========================================="
echo "PiTrac Tarball Dependency Removal"
echo "========================================="
echo

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   log_error "This script must be run as root (use sudo)"
   exit 1
fi

# Track if anything was removed
REMOVED_ANYTHING=false

# Check for old tarball installations
if [[ -d "/opt/opencv" ]]; then
    log_warn "Found tarball-based OpenCV installation in /opt/opencv"
    log_warn "This conflicts with DEB package installation in /usr/"
    read -p "Remove /opt/opencv? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        log_info "Removing /opt/opencv..."
        rm -rf /opt/opencv
        REMOVED_ANYTHING=true
        log_success "Removed /opt/opencv"
    else
        log_info "Skipped /opt/opencv removal"
    fi
fi

if [[ -d "/opt/activemq-cpp" ]]; then
    log_warn "Found tarball-based ActiveMQ-CPP installation in /opt/activemq-cpp"
    log_warn "This conflicts with DEB package installation in /usr/"
    read -p "Remove /opt/activemq-cpp? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        log_info "Removing /opt/activemq-cpp..."
        rm -rf /opt/activemq-cpp
        REMOVED_ANYTHING=true
        log_success "Removed /opt/activemq-cpp"
    else
        log_info "Skipped /opt/activemq-cpp removal"
    fi
fi

if [[ -d "/opt/lgpio" ]]; then
    log_warn "Found tarball-based lgpio installation in /opt/lgpio"
    log_warn "This conflicts with DEB package installation in /usr/"
    read -p "Remove /opt/lgpio? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        log_info "Removing /opt/lgpio..."
        rm -rf /opt/lgpio
        REMOVED_ANYTHING=true
        log_success "Removed /opt/lgpio"
    else
        log_info "Skipped /opt/lgpio removal"
    fi
fi

if [[ -d "/opt/msgpack" ]]; then
    log_warn "Found tarball-based msgpack installation in /opt/msgpack"
    log_warn "This conflicts with DEB package installation in /usr/"
    read -p "Remove /opt/msgpack? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        log_info "Removing /opt/msgpack..."
        rm -rf /opt/msgpack
        REMOVED_ANYTHING=true
        log_success "Removed /opt/msgpack"
    else
        log_info "Skipped /opt/msgpack removal"
    fi
fi

# Update library cache if anything was removed
if [[ "$REMOVED_ANYTHING" == "true" ]]; then
    log_info "Updating library cache..."
    ldconfig
    log_success "Library cache updated"

    echo
    log_success "Tarball dependencies removed"
    log_info "Next steps:"
    log_info "  1. Clean build directory: cd ~/PiTrac/Software/LMSourceCode/ImageProcessing && rm -rf build"
    log_info "  2. Rebuild: cd ~/PiTrac/packaging && sudo ./build.sh dev"
else
    log_info "No tarball installations found in /opt/"
fi

echo
echo "Done!"
