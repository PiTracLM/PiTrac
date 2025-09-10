#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/common.sh"

load_defaults "fmt" "$@"

FMT_VERSION="${FMT_VERSION:-${fmt_version:-9.1.0}}"
FORCE="${FORCE:-${force:-0}}"
BUILD_FROM_SOURCE="${BUILD_FROM_SOURCE:-${build_from_source:-0}}"
BUILD_TYPE="${BUILD_TYPE:-${build_type:-Release}}"
INSTALL_PREFIX="${INSTALL_PREFIX:-${install_prefix:-/usr/local}}"
BUILD_SHARED_LIBS="${BUILD_SHARED_LIBS:-${build_shared_libs:-1}}"
BUILD_CORES="${BUILD_CORES:-${build_cores:-0}}"

is_fmt_installed() {
    if pkg-config --exists fmt 2>/dev/null; then
        return 0
    fi
    
    if [ -f "$INSTALL_PREFIX/lib/libfmt.a" ] || [ -f "/usr/lib/libfmt.a" ] || \
       [ -f "$INSTALL_PREFIX/lib/libfmt.so" ] || [ -f "/usr/lib/x86_64-linux-gnu/libfmt.so" ] || \
       [ -f "/usr/lib/aarch64-linux-gnu/libfmt.so" ]; then
        return 0
    fi
    
    return 1
}

precheck() {
    if is_fmt_installed && [ "$FORCE" != "1" ]; then
        local version=""
        if pkg-config --exists fmt 2>/dev/null; then
            version=$(pkg-config --modversion fmt 2>/dev/null || echo "")
            [ -n "$version" ] && version=" (version $version)"
        fi
        log_info "fmt is already installed${version}"
        return 1
    fi
    return 0
}

install_from_package() {
    log_info "Installing fmt from package manager..."
    ensure_package libfmt-dev
}

build_from_source() {
    log_info "Building fmt from source (version $FMT_VERSION)..."
    
    ensure_package cmake
    ensure_package build-essential
    
    local build_dir="/tmp/fmt_build_$$"
    mkdir -p "$build_dir"
    cd "$build_dir"
    
    log_info "Downloading fmt v${FMT_VERSION}..."
    local url="https://github.com/fmtlib/fmt/archive/refs/tags/${FMT_VERSION}.tar.gz"
    
    if ! download_with_progress "$url" "fmt-${FMT_VERSION}.tar.gz"; then
        log_error "Failed to download fmt source"
        cd /
        rm -rf "$build_dir"
        return 1
    fi
    
    log_info "Extracting source..."
    tar -xzf "fmt-${FMT_VERSION}.tar.gz"
    cd "fmt-${FMT_VERSION}"
    
    mkdir -p build
    cd build
    
    log_info "Configuring with CMake..."
    local shared_libs_flag="OFF"
    [ "$BUILD_SHARED_LIBS" = "1" ] && shared_libs_flag="ON"
    
    cmake .. \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DFMT_DOC=OFF \
        -DFMT_TEST=OFF \
        -DBUILD_SHARED_LIBS="$shared_libs_flag" \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    
    local cores="$BUILD_CORES"
    [ "$cores" = "0" ] && cores=$(get_cpu_cores)
    
    log_info "Building with $cores cores..."
    if run_with_progress "make -j$cores" "Building fmt" "/tmp/fmt_build.log"; then
        log_success "Build completed"
    else
        log_error "Build failed. Check /tmp/fmt_build.log for details"
        cd /
        rm -rf "$build_dir"
        return 1
    fi
    
    log_info "Installing fmt to $INSTALL_PREFIX..."
    $SUDO make install
    
    if [ -f /etc/ld.so.conf ]; then
        $SUDO ldconfig
    fi
    
    if ! pkg-config --exists fmt 2>/dev/null; then
        create_pkgconfig_file
    fi
    
    cd /
    rm -rf "$build_dir"
    
    log_success "fmt ${FMT_VERSION} built and installed from source"
    return 0
}

create_pkgconfig_file() {
    log_info "Creating pkg-config file for fmt..."
    
    local pc_dir="$INSTALL_PREFIX/lib/pkgconfig"
    $SUDO mkdir -p "$pc_dir"
    
    cat << EOF | $SUDO tee "$pc_dir/fmt.pc" > /dev/null
prefix=$INSTALL_PREFIX
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: fmt
Description: A modern formatting library
Version: $FMT_VERSION
Libs: -L\${libdir} -lfmt
Cflags: -I\${includedir}
EOF
    
    export PKG_CONFIG_PATH="$pc_dir:${PKG_CONFIG_PATH:-}"
    
    log_success "pkg-config file created at $pc_dir/fmt.pc"
}

install_fmt() {
    log_info "=== fmt Library Installation ==="
    
    if ! run_preflight_checks "fmt"; then
        return 1
    fi
    
    if ! precheck; then
        log_info "Skipping fmt installation"
        return 0
    fi
    
    if [ "$BUILD_FROM_SOURCE" != "1" ]; then
        if install_from_package; then
            if is_fmt_installed; then
                log_success "fmt successfully installed from package manager"
                return 0
            fi
        else
            log_info "Package installation failed or not available, will build from source"
        fi
    fi
    
    if build_from_source; then
        if is_fmt_installed; then
            log_success "fmt installation completed successfully"
            
            if pkg-config --exists fmt 2>/dev/null; then
                local version=$(pkg-config --modversion fmt)
                local cflags=$(pkg-config --cflags fmt)
                local libs=$(pkg-config --libs fmt)
                log_info "Installed version: $version"
                log_info "Compile flags: $cflags"
                log_info "Link flags: $libs"
            fi
            return 0
        else
            log_error "fmt installation verification failed"
            return 1
        fi
    else
        log_error "Failed to install fmt"
        return 1
    fi
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    install_fmt
    exit $?
fi