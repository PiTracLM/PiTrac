#!/usr/bin/env bash
# POC build orchestrator
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ARTIFACT_DIR="$SCRIPT_DIR/deps-artifacts"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'
log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
log_success() { echo -e "${GREEN}[✓]${NC} $*"; }

# Setup QEMU for cross-platform builds
setup_qemu() {
    if ! docker run --rm --privileged multiarch/qemu-user-static --reset -p yes &>/dev/null; then
        log_warn "Setting up QEMU for ARM64 emulation..."
        docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
    fi
}

ACTION="${1:-build}"
FORCE_REBUILD="${2:-false}"

show_usage() {
    echo "Usage: $0 [action] [force-rebuild]"
    echo ""
    echo "Actions:"
    echo "  deps      - Build dependency artifacts if missing"
    echo "  build     - Build PiTrac using artifacts (default)"
    echo "  all       - Build deps then PiTrac"
    echo "  dev       - Build and install directly on Pi (for development)"
    echo "  clean     - Remove all artifacts and Docker images"
    echo "  shell     - Interactive shell with artifacts"
    echo ""
    echo "Options:"
    echo "  force-rebuild - Force rebuild even if artifacts exist"
    echo ""
    echo "Examples:"
    echo "  $0              # Build PiTrac (deps must exist)"
    echo "  $0 deps         # Build dependency artifacts"
    echo "  $0 all          # Build everything from scratch"
    echo "  $0 dev          # Build and install on Pi (incremental)"
    echo "  $0 dev force    # Clean build and install on Pi"
    echo "  $0 all true     # Force rebuild everything"
}

check_artifacts() {
    local missing=()
    local use_debs="${USE_DEB_PACKAGES:-true}"

    log_info "Checking for pre-built artifacts..."

    if [[ "$use_debs" == "true" ]]; then
        # Check for DEB packages first
        if [ ! -f "$ARTIFACT_DIR/libopencv4.11_4.11.0-1_arm64.deb" ] && [ ! -f "$ARTIFACT_DIR/libopencv-dev_4.11.0-1_arm64.deb" ]; then
            if [ ! -f "$ARTIFACT_DIR/opencv-4.11.0-arm64.tar.gz" ]; then
                missing+=("opencv")
            fi
        fi
        if [ ! -f "$ARTIFACT_DIR/libactivemq-cpp_3.9.5-1_arm64.deb" ] && [ ! -f "$ARTIFACT_DIR/libactivemq-cpp-dev_3.9.5-1_arm64.deb" ]; then
            if [ ! -f "$ARTIFACT_DIR/activemq-cpp-3.9.5-arm64.tar.gz" ]; then
                missing+=("activemq")
            fi
        fi
        if [ ! -f "$ARTIFACT_DIR/liblgpio1_0.2.2-1_arm64.deb" ]; then
            if [ ! -f "$ARTIFACT_DIR/lgpio-0.2.2-arm64.tar.gz" ]; then
                missing+=("lgpio")
            fi
        fi
        if [ ! -f "$ARTIFACT_DIR/libmsgpack-cxx-dev_6.1.1-1_all.deb" ]; then
            if [ ! -f "$ARTIFACT_DIR/msgpack-cxx-6.1.1-arm64.tar.gz" ]; then
                missing+=("msgpack")
            fi
        fi
        if [ ! -f "$ARTIFACT_DIR/libonnxruntime1.17.3_1.17.3-xnnpack-verified_arm64.deb" ]; then
            missing+=("onnxruntime")
        fi
    else
        # Check for tar.gz packages
        if [ ! -f "$ARTIFACT_DIR/opencv-4.11.0-arm64.tar.gz" ]; then
            missing+=("opencv")
        fi
        if [ ! -f "$ARTIFACT_DIR/activemq-cpp-3.9.5-arm64.tar.gz" ]; then
            missing+=("activemq")
        fi
        if [ ! -f "$ARTIFACT_DIR/lgpio-0.2.2-arm64.tar.gz" ]; then
            missing+=("lgpio")
        fi
        if [ ! -f "$ARTIFACT_DIR/msgpack-cxx-6.1.1-arm64.tar.gz" ]; then
            missing+=("msgpack")
        fi
    fi

    if [ ${#missing[@]} -gt 0 ]; then
        log_warn "Missing artifacts: ${missing[*]}"
        return 1
    else
        if [[ "$use_debs" == "true" ]] && [ -f "$ARTIFACT_DIR/libopencv4.11_4.11.0-1_arm64.deb" ]; then
            log_success "All DEB packages present"
        else
            log_success "All artifacts present"
        fi
        return 0
    fi
}

build_deps() {
    log_info "Building dependency artifacts..."

    if [ "$FORCE_REBUILD" = "true" ]; then
        log_warn "Force rebuild enabled - removing existing artifacts"
        rm -f "$ARTIFACT_DIR"/*.tar.gz
    fi

    # Build all dependencies
    "$SCRIPT_DIR/scripts/build-all-deps.sh"
}

build_pitrac() {
    log_info "Building PiTrac with pre-built artifacts..."

    # Check artifacts exist
    if ! check_artifacts; then
        log_error "Missing dependency artifacts. Run: $0 deps"
        exit 1
    fi

    # Generate Bashly CLI if needed
    if [[ ! -f "$SCRIPT_DIR/pitrac" ]]; then
        log_warn "Bashly CLI not found, generating it now..."
        if [[ -f "$SCRIPT_DIR/generate.sh" ]]; then
            cd "$SCRIPT_DIR"
            ./generate.sh
            cd - > /dev/null
            log_success "Generated pitrac CLI tool"
        else
            log_error "generate.sh not found!"
            exit 1
        fi
    fi

    # Setup QEMU for ARM64 emulation on x86_64
    setup_qemu

    # Build Docker image
    log_info "Building PiTrac Docker image..."
    docker build \
        --platform=linux/arm64 \
        -f "$SCRIPT_DIR/Dockerfile.pitrac" \
        -t pitrac-poc:arm64 \
        "$SCRIPT_DIR"

    # Run build
    log_info "Running PiTrac build..."
    docker run \
        --rm \
        --platform=linux/arm64 \
        -v "$REPO_ROOT:/build:rw" \
        -u "$(id -u):$(id -g)" \
        # --memory=16g \
        # --memory-swap=24g \
        # --cpus="8" \
        pitrac-poc:arm64

    # Check result
    BINARY="$REPO_ROOT/Software/LMSourceCode/ImageProcessing/build/pitrac_lm"
    if [ -f "$BINARY" ]; then
        log_success "Build successful!"
        log_info "Binary: $BINARY"
        log_info "Size: $(du -h "$BINARY" | cut -f1)"
        file "$BINARY"
    else
        log_error "Build failed - binary not found"
        exit 1
    fi
}

run_shell() {
    log_info "Starting interactive shell with pre-built artifacts..."

    # Check artifacts exist
    if ! check_artifacts; then
        log_error "Missing dependency artifacts. Run: $0 deps"
        exit 1
    fi

    # Setup QEMU for ARM64 emulation on x86_64
    setup_qemu

    # Ensure image exists
    if ! docker image inspect pitrac-poc:arm64 &>/dev/null; then
        log_info "Building Docker image first..."
        docker build \
            --platform=linux/arm64 \
            -f "$SCRIPT_DIR/Dockerfile.pitrac" \
            -t pitrac-poc:arm64 \
            "$SCRIPT_DIR"
    fi

    docker run \
        --rm -it \
        --platform=linux/arm64 \
        -v "$REPO_ROOT:/build:rw" \
        -u "$(id -u):$(id -g)" \
        pitrac-poc:arm64 \
        /bin/bash
}

clean_all() {
    log_warn "Cleaning all POC artifacts and images..."

    # Remove artifacts
    rm -rf "$ARTIFACT_DIR"/*.tar.gz
    rm -rf "$ARTIFACT_DIR"/*.metadata

    # Remove Docker images
    docker rmi opencv-builder:arm64 2>/dev/null || true
    docker rmi activemq-builder:arm64 2>/dev/null || true
    docker rmi lgpio-builder:arm64 2>/dev/null || true
    docker rmi pitrac-poc:arm64 2>/dev/null || true

    log_success "Cleaned all POC resources"
}

build_dev() {
    log_info "PiTrac Development Build - Direct Pi Installation"

    # Source common functions
    if [[ -f "$SCRIPT_DIR/src/lib/pitrac-common-functions.sh" ]]; then
        source "$SCRIPT_DIR/src/lib/pitrac-common-functions.sh"
    fi

    # Check if running on Raspberry Pi
    if ! grep -q "Raspberry Pi" /proc/cpuinfo 2>/dev/null; then
        log_error "Dev mode must be run on a Raspberry Pi"
        exit 1
    fi

    # Check for sudo
    if [[ $EUID -ne 0 ]]; then
        log_error "Dev mode requires root privileges to install to system locations"
        log_info "Please run: sudo ./build.sh dev"
        exit 1
    fi

    # Check for force rebuild flag
    if [[ "${2:-}" == "force" ]]; then
        FORCE_REBUILD="true"
        log_info "Force rebuild requested - will clean build directory"
    fi

    log_info "Regenerating pitrac CLI tool..."
    if [[ -f "$SCRIPT_DIR/generate.sh" ]]; then
        cd "$SCRIPT_DIR"
        ./generate.sh
        cd - > /dev/null
        log_success "Regenerated pitrac CLI tool"
    else
        log_error "generate.sh not found!"
        exit 1
    fi

    # Check artifacts exist
    if ! check_artifacts; then
        log_error "Missing dependency artifacts. These should be in git."
        log_error "Try: git lfs pull"
        exit 1
    fi

    # Check build dependencies (matching Dockerfile.pitrac)
    log_info "Checking build dependencies..."
    local missing_deps=()

    # Build tools
    for pkg in build-essential meson ninja-build pkg-config git; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            missing_deps+=("$pkg")
        fi
    done

    # Boost libraries (runtime and dev)
    for pkg in libboost-system1.74.0 libboost-thread1.74.0 libboost-filesystem1.74.0 \
               libboost-program-options1.74.0 libboost-timer1.74.0 libboost-log1.74.0 \
               libboost-regex1.74.0 libboost-dev libboost-all-dev libyaml-cpp-dev; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            missing_deps+=("$pkg")
        fi
    done

    # Core libraries
    for pkg in libcamera0.0.3 libcamera-dev libcamera-tools libfmt-dev libssl-dev libssl3 \
               libmsgpack-cxx-dev \
               libapr1 libaprutil1 libapr1-dev libaprutil1-dev; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            missing_deps+=("$pkg")
        fi
    done

    # ========================================================================
    # lgpio Library Strategy
    # ========================================================================
    # lgpio is available in both custom debs (deps-artifacts/) and system repos
    #
    # Problem: Version mismatch causes conflicts:
    #   - Custom: liblgpio1 (0.2.2-1)
    #   - System: liblgpio1 (0.2.2-1~rpt1) + liblgpio-dev (0.2.2-1~rpt1)
    #   - Custom package declares "Conflicts: liblgpio-dev"
    #   - System packages like libcamera-dev depend on liblgpio-dev
    #
    # Solution: Prefer system packages when available
    #   1. Remove custom lgpio before apt-get install (line ~426)
    #   2. Let apt install system lgpio packages
    #   3. extract_all_dependencies() will skip custom lgpio if system exists
    # ========================================================================
    if ! dpkg -l | grep -qE "^ii\s+(liblgpio1|liblgpio-dev)"; then
        log_info "lgpio not installed - will use custom package from deps-artifacts"
    else
        log_info "System lgpio packages detected - will use those instead of custom"
    fi
    
    if ! command -v rpicam-hello &> /dev/null && ! command -v libcamera-hello &> /dev/null; then
        if apt-cache show rpicam-apps &> /dev/null; then
            missing_deps+=("rpicam-apps")
        elif apt-cache show libcamera-apps &> /dev/null; then
            missing_deps+=("libcamera-apps")
        else
            log_warning "Neither rpicam-apps nor libcamera-apps available in repositories"
        fi
    fi

    # OpenCV runtime dependencies
    for pkg in libgtk-3-0 libavcodec59 libavformat59 libswscale6 libtbb12 \
               libgstreamer1.0-0 libgstreamer-plugins-base1.0-0 libopenexr-3-1-30; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            missing_deps+=("$pkg")
        fi
    done

    # FFmpeg development libraries
    for pkg in libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libavdevice-dev; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            missing_deps+=("$pkg")
        fi
    done

    # Image libraries
    for pkg in libexif-dev libjpeg-dev libtiff-dev libpng-dev; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            missing_deps+=("$pkg")
        fi
    done

    # Display/GUI libraries
    for pkg in libdrm-dev libx11-dev libxext-dev libepoxy-dev qtbase5-dev qt5-qmake; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            missing_deps+=("$pkg")
        fi
    done

    # Python runtime dependencies for CLI tool
    for pkg in python3 python3-pip python3-yaml python3-opencv python3-numpy; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            missing_deps+=("$pkg")
        fi
    done

    # Configuration parsing tools
    if ! dpkg -l | grep -q "^ii  yq"; then
        missing_deps+=("yq")
    fi

    # ActiveMQ message broker
    if ! dpkg -l | grep -q "^ii  activemq"; then
        missing_deps+=("activemq")
    fi

    # ========================================================================
    # Fix initramfs-tools configuration issues on Raspberry Pi
    # ========================================================================
    # Issue: Recent Pi OS images (2024-11-13+) changed MODULES=dep which causes
    # "failed to determine device for /" errors during initramfs generation
    # Reference: https://github.com/RPi-Distro/repo/issues/382
    #
    # Solution: Change back to MODULES=most for proper Pi support (including NVMe boot)
    # ========================================================================

    local initramfs_conf="/etc/initramfs-tools/initramfs.conf"
    if [[ -f "$initramfs_conf" ]]; then
        if grep -q "^MODULES=dep" "$initramfs_conf"; then
            log_warn "Detected problematic MODULES=dep in initramfs configuration"
            log_info "Fixing initramfs.conf for Raspberry Pi compatibility..."
            sed -i.bak 's/^MODULES=dep/MODULES=most/' "$initramfs_conf"
            log_success "Changed MODULES=dep to MODULES=most"

            # If initramfs-tools is in a broken state, try to fix it now
            if dpkg -l | grep -E "^[a-z]F\s+initramfs-tools"; then
                log_info "Attempting to repair initramfs-tools package..."
                if dpkg --configure initramfs-tools 2>&1 | tee /tmp/initramfs-fix.log; then
                    log_success "initramfs-tools repaired successfully"
                else
                    log_warn "initramfs-tools still has issues, will use INITRD=No fallback"
                fi
            fi
        elif grep -q "^MODULES=most" "$initramfs_conf"; then
            log_info "initramfs.conf already correctly configured (MODULES=most)"
        else
            log_warn "initramfs.conf has non-standard MODULES setting"
            log_info "Adding MODULES=most to initramfs.conf..."
            sed -i.bak '/^#.*MODULES/a MODULES=most' "$initramfs_conf"
            log_success "Set MODULES=most in initramfs.conf"
        fi
    else
        log_warn "initramfs.conf not found, will use INITRD=No for package operations"
    fi

    # Fix any remaining broken packages
    log_info "Checking for broken package states..."
    if dpkg -l | grep -qE "^[a-z][^i]"; then
        log_warn "Found packages in broken state, attempting repair..."
        # Try normal configure first (now that initramfs.conf is fixed)
        if ! dpkg --configure -a 2>&1; then
            log_warn "Normal repair failed, using INITRD=No fallback..."
            INITRD=No dpkg --configure -a 2>&1 || true
        fi
        log_success "Package state cleanup complete"
    else
        log_info "No broken packages detected"
    fi

    if [ ${#missing_deps[@]} -gt 0 ]; then
        log_warn "Missing build dependencies: ${missing_deps[*]}"
        log_info "Installing missing dependencies..."

        # Remove custom lgpio package if it exists to avoid conflicts with system packages
        # System packages (libcamera-dev, etc.) may pull in liblgpio-dev from repos
        # Our custom package has version 0.2.2-1 but system has 0.2.2-1~rpt1
        if dpkg -l | grep -qE "^ii\s+liblgpio1\s+0\.2\.2-1\s"; then
            log_warn "Removing custom liblgpio1 package to avoid apt conflicts..."
            log_info "System lgpio packages will be used instead (from repos)"
            dpkg --remove --force-depends liblgpio1 2>/dev/null || true
        fi

        apt-get update
        # Use INITRD=No as safety measure - these are just libraries, not kernel modules
        INITRD=No apt-get install -y "${missing_deps[@]}"
    fi

    log_info "Installing pre-built dependencies..."
    mkdir -p /usr/lib/pitrac
    extract_all_dependencies "$ARTIFACT_DIR" "/usr/lib/pitrac"

    # Update library cache
    ldconfig

    # Build PiTrac
    log_info "Building PiTrac..."
    cd "$REPO_ROOT/Software/LMSourceCode/ImageProcessing"

    # Apply Boost C++20 fix using common function
    apply_boost_cxx20_fix

    # Set build environment
    # DEB packages now install to standard Debian locations
    export PKG_CONFIG_PATH="/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig"
    export LD_LIBRARY_PATH="/usr/lib/aarch64-linux-gnu:/usr/lib/pitrac:${LD_LIBRARY_PATH:-}"
    export CMAKE_PREFIX_PATH="/usr"
    export CPLUS_INCLUDE_PATH="/usr/include/opencv4:/usr/include/activemq-cpp-3.9.5"
    export PITRAC_ROOT="$REPO_ROOT/Software/LMSourceCode"
    export CXXFLAGS="-I/usr/include/opencv4"

    # Create dummy closed source file if needed
    mkdir -p ClosedSourceObjectFiles
    touch ClosedSourceObjectFiles/gs_e6_response.cpp.o

    # ========================================================================
    # Detect stale build directory from tarball/Docker installation
    # ========================================================================
    # Issue: If build directory was created when using tarball extraction to /opt/,
    # Meson caches those paths. When switching to DEB packages (which install to
    # /usr/lib/aarch64-linux-gnu/), the cached paths cause linker failures.
    #
    # Solution: Detect /opt/ paths in build.ninja and force rebuild if DEB packages
    # are installed (which use /usr/ paths instead).
    # ========================================================================
    if [[ -f "build/build.ninja" ]]; then
        if grep -q "/opt/opencv\|/opt/activemq" build/build.ninja 2>/dev/null; then
            # Check if DEB packages are installed (they use /usr/, not /opt/)
            if dpkg -l 2>/dev/null | grep -qE "^ii\s+(libopencv4\.11|libactivemq-cpp)\s"; then
                log_warn "Detected build directory with /opt/ paths but DEB packages use /usr/"
                log_warn "This causes linker failures - cached paths are stale"
                log_info "Automatically cleaning build directory for compatibility..."
                rm -rf build
                FORCE_REBUILD="true"

                # Warn if old /opt/ installations still exist alongside DEB packages
                if [[ -d "/opt/opencv" ]] || [[ -d "/opt/activemq-cpp" ]]; then
                    echo
                    log_warn "Old tarball installations detected in /opt/ alongside DEB packages"
                    log_warn "This may cause runtime library conflicts"
                    log_info "Recommended: Run 'sudo ./uninstall-tar-deps.sh' to remove old installations"
                    echo
                fi
            elif [[ ! -d "/opt/opencv" ]] && [[ ! -d "/opt/activemq-cpp" ]]; then
                log_warn "Detected build directory expects /opt/ libraries but they don't exist"
                log_info "Cleaning build directory - will reconfigure for system paths..."
                rm -rf build
                FORCE_REBUILD="true"
            fi
        fi
    fi

    # Determine if we need a clean build
    if [[ "$FORCE_REBUILD" == "true" ]]; then
        log_info "Force rebuild requested - cleaning build directory..."
        rm -rf build
    fi

    # Only run meson setup if build directory doesn't exist
    if [[ ! -d "build" ]]; then
        log_info "Configuring build with Meson..."
        meson setup build --buildtype=release -Denable_recompile_closed_source=false
    elif [[ "meson.build" -nt "build/build.ninja" ]] 2>/dev/null; then
        log_warn "meson.build has changed - reconfiguration recommended"
        log_info "Run 'sudo ./build.sh dev force' to reconfigure the build system"
    else
        log_info "Using incremental build (use 'sudo ./build.sh dev force' for clean build)"
    fi

    log_info "Building with Ninja..."
    ninja -C build pitrac_lm

    # Check if build succeeded
    if [[ ! -f "build/pitrac_lm" ]]; then
        log_error "Build failed - binary not found"
        exit 1
    fi

    # Install binary
    log_info "Installing PiTrac binary..."
    install -m 755 build/pitrac_lm /usr/lib/pitrac/pitrac_lm

    log_info "Installing CLI tool..."
    install -m 755 "$SCRIPT_DIR/pitrac" /usr/bin/pitrac


    install_camera_tools "/usr/lib/pitrac" "$REPO_ROOT"

    # Configure libcamera AFTER camera tools are installed
    # This ensures IMX296 sensor files are available to be copied
    configure_libcamera

    install_test_images "/usr/share/pitrac/test-images" "$REPO_ROOT"

    install_test_suites "/usr/share/pitrac/test-suites" "$REPO_ROOT"

    install_onnx_models "$REPO_ROOT" "${SUDO_USER:-$(whoami)}"

    # Install calibration tools
    log_info "Installing calibration tools..."
    mkdir -p /usr/share/pitrac/calibration
    local calib_dir="$REPO_ROOT/Software/CalibrateCameraDistortions"
    if [[ -d "$calib_dir" ]]; then
        cp "$calib_dir/CameraCalibration.py" /usr/share/pitrac/calibration/ 2>/dev/null || true
        cp "$calib_dir/checkerboard.png" /usr/share/pitrac/calibration/ 2>/dev/null || true
    fi

    # Create calibration wizard
    cat > /usr/lib/pitrac/calibration-wizard << 'EOF'
#!/bin/bash
if [[ -f /usr/share/pitrac/calibration/CameraCalibration.py ]]; then
    cd /usr/share/pitrac/calibration
    python3 CameraCalibration.py "$@"
else
    echo "Calibration tools not installed"
    exit 1
fi
EOF
    chmod 755 /usr/lib/pitrac/calibration-wizard

    # Install config templates (only if they don't exist)
    log_info "Installing configuration templates..."
    mkdir -p /etc/pitrac
    mkdir -p /etc/pitrac/config
    
    if [[ ! -f /etc/pitrac/pitrac.yaml ]]; then
        cp "$SCRIPT_DIR/templates/pitrac.yaml" /etc/pitrac/pitrac.yaml
    else
        log_info "  pitrac.yaml already exists, skipping"
    fi

    if [[ ! -f /etc/pitrac/golf_sim_config.json ]]; then
        cp "$SCRIPT_DIR/templates/golf_sim_config.json" /etc/pitrac/golf_sim_config.json
    else
        log_info "  golf_sim_config.json already exists, skipping"
    fi
    
    if [[ -d "$SCRIPT_DIR/templates/config" ]]; then
        if [[ ! -f /etc/pitrac/config/parameter-mappings.yaml ]]; then
            cp "$SCRIPT_DIR/templates/config/parameter-mappings.yaml" /etc/pitrac/config/parameter-mappings.yaml
            log_info "  parameter-mappings.yaml installed"
        else
            log_info "  parameter-mappings.yaml already exists, skipping"
        fi
        
    fi

    # Configure ActiveMQ
    if command -v activemq &>/dev/null || [[ -f /usr/share/activemq/bin/activemq ]]; then
        log_info "Configuring ActiveMQ using template system..."
        
        mkdir -p /usr/share/pitrac/templates
        cp "$SCRIPT_DIR/templates/activemq.xml.template" /usr/share/pitrac/templates/
        cp "$SCRIPT_DIR/templates/log4j2.properties.template" /usr/share/pitrac/templates/
        cp "$SCRIPT_DIR/templates/activemq-options.template" /usr/share/pitrac/templates/
        
        mkdir -p /usr/lib/pitrac
        cp "$SCRIPT_DIR/src/lib/activemq-service-install.sh" /usr/lib/pitrac/
        chmod 755 /usr/lib/pitrac/activemq-service-install.sh
        
        log_info "Installing ActiveMQ configuration..."
        if /usr/lib/pitrac/activemq-service-install.sh install activemq; then
            log_success "ActiveMQ configuration installed successfully"
            
            log_info "Restarting ActiveMQ service..."
            # First enable the service (for boot)
            systemctl enable activemq 2>/dev/null || true
            

            manage_service_restart "activemq"
            
            if systemctl is-active --quiet activemq; then
                
                # Verify it's actually listening
                if netstat -tln 2>/dev/null | grep -q ":61616 "; then
                    log_success "ActiveMQ broker listening on port 61616"
                else
                    log_warn "ActiveMQ started but not listening on port 61616 yet"
                    log_info "It may take a few seconds to fully initialize"
                fi
                
                /usr/lib/pitrac/activemq-service-install.sh verify || true
            else
                log_warn "ActiveMQ configured but may need manual restart"
                log_info "Check logs with: journalctl -u activemq -n 50"
            fi
        else
            log_error "Failed to configure ActiveMQ"
            log_info "Try running manually: /usr/lib/pitrac/activemq-service-install.sh install"
        fi
    else
        log_error "ActiveMQ installation failed! This is a critical component."
        log_info "Try manually installing with: sudo apt install activemq"
        exit 1
    fi

    # Clean up old PiTrac systemd service and processes if they exist
    log_info "Checking for existing PiTrac systemd service and processes..."
    
    # Check if old service exists and is running
    if systemctl list-unit-files | grep -q "pitrac.service"; then
        log_warn "Found existing pitrac.service - will clean it up as PiTrac is now managed via web interface"
        
        # Stop the service if it's running
        if systemctl is-active --quiet pitrac.service; then
            log_info "Stopping pitrac.service..."
            systemctl stop pitrac.service || true
            sleep 2
        fi
        
        # Disable the service
        log_info "Disabling pitrac.service..."
        systemctl disable pitrac.service 2>/dev/null || true
        
        # Remove the service files from all possible locations
        log_info "Removing service files..."
        rm -f /etc/systemd/system/pitrac.service
        rm -f /lib/systemd/system/pitrac.service
        rm -f /usr/lib/systemd/system/pitrac.service
        
        # Reload systemd
        systemctl daemon-reload
        
        log_success "Old pitrac.service removed successfully"
    fi
    
    # Kill any lingering pitrac_lm processes (these might be holding GPIO/SPI resources)
    if pgrep -x "pitrac_lm" > /dev/null; then
        log_warn "Found running pitrac_lm processes - cleaning them up..."
        
        # First try graceful termination
        pkill -TERM pitrac_lm 2>/dev/null || true
        sleep 2
        
        # If still running, force kill
        if pgrep -x "pitrac_lm" > /dev/null; then
            log_info "Force killing remaining pitrac_lm processes..."
            pkill -9 pitrac_lm 2>/dev/null || true
            sleep 1
        fi
        
        log_success "Cleaned up old pitrac_lm processes"
    fi
    
    # Clean up PID and lock files
    log_info "Cleaning up PID/lock files..."
    rm -f /var/run/pitrac/*.pid 2>/dev/null || true
    rm -f /var/run/pitrac/*.lock 2>/dev/null || true
    rm -f "${HOME}/.pitrac/run"/*.pid 2>/dev/null || true
    rm -f "${HOME}/.pitrac/run"/*.lock 2>/dev/null || true
    if [[ -n "${SUDO_USER}" ]]; then
        rm -f "/home/${SUDO_USER}/.pitrac/run"/*.pid 2>/dev/null || true
        rm -f "/home/${SUDO_USER}/.pitrac/run"/*.lock 2>/dev/null || true
    fi
    
    # Reset GPIO if possible (GPIO 25 is used by PiTrac for pulse strobe)
    log_info "Attempting to reset GPIO resources..."
    if [ -d "/sys/class/gpio/gpio25" ]; then
        echo "25" | tee /sys/class/gpio/unexport > /dev/null 2>&1 || true
    fi
    
    # Give the system a moment to release resources
    sleep 1
    
    log_info "Installing web server and ActiveMQ services..."
    
    mkdir -p /usr/share/pitrac/templates
    cp "$SCRIPT_DIR/templates/pitrac-web.service.template" /usr/share/pitrac/templates/
    cp "$SCRIPT_DIR/templates/activemq.xml.template" /usr/share/pitrac/templates/ 2>/dev/null || true
    cp "$SCRIPT_DIR/templates/log4j2.properties.template" /usr/share/pitrac/templates/ 2>/dev/null || true
    cp "$SCRIPT_DIR/templates/activemq-options.template" /usr/share/pitrac/templates/ 2>/dev/null || true
    
    
    if [[ -f "$SCRIPT_DIR/src/lib/activemq-service-install.sh" ]]; then
        cp "$SCRIPT_DIR/src/lib/activemq-service-install.sh" /usr/lib/pitrac/
        chmod 755 /usr/lib/pitrac/activemq-service-install.sh
    fi
    
    if [[ -f "$SCRIPT_DIR/src/lib/web-service-install.sh" ]]; then
        cp "$SCRIPT_DIR/src/lib/web-service-install.sh" /usr/lib/pitrac/
        chmod 755 /usr/lib/pitrac/web-service-install.sh
    fi
    
    if [[ -f "$SCRIPT_DIR/src/lib/pitrac-common-functions.sh" ]]; then
        cp "$SCRIPT_DIR/src/lib/pitrac-common-functions.sh" /usr/lib/pitrac/
        chmod 644 /usr/lib/pitrac/pitrac-common-functions.sh
    fi
    
    INSTALL_USER="${SUDO_USER:-$(whoami)}"

    # Install Python web server (always update)
    log_info "Installing/Updating PiTrac web server..."
    WEB_SERVER_DIR="$REPO_ROOT/Software/web-server"
    if [[ -d "$WEB_SERVER_DIR" ]]; then
        update_web_server() {
            log_info "Cleaning previous web server installation..."
            rm -rf /usr/lib/pitrac/web-server
            mkdir -p /usr/lib/pitrac/web-server

            log_info "Copying latest web server files..."
            cp -r "$WEB_SERVER_DIR"/* /usr/lib/pitrac/web-server/

            install_python_dependencies "/usr/lib/pitrac/web-server"

            log_info "Installing web server service for user: $INSTALL_USER"
            if [[ -x /usr/lib/pitrac/web-service-install.sh ]]; then
                /usr/lib/pitrac/web-service-install.sh install "$INSTALL_USER"
            else
                log_error "Web service installer not found"
            fi
        }

        if systemctl is-active --quiet pitrac-web.service; then
            manage_service_restart "pitrac-web.service" update_web_server
        else
            update_web_server
            log_info "Web server installed but not started (was not running before)"
        fi

        log_success "Web server updated"
    else
        log_warn "Web server source not found at $WEB_SERVER_DIR"
    fi

    # Configure cameras after web server is installed
    log_info "Configuring cameras (if connected)..."
    if [[ -x "$SCRIPT_DIR/scripts/configure-cameras.sh" ]]; then
        # Run camera configuration script
        # It will skip if no cameras are detected
        "$SCRIPT_DIR/scripts/configure-cameras.sh" || {
            log_warn "Camera configuration failed or skipped (non-critical)"
        }
    else
        log_warn "Camera configuration script not found - skipping camera setup"
    fi

    create_pitrac_directories

    # Update systemd
    systemctl daemon-reload

    log_success "Development build complete!"
    echo ""
    echo "PiTrac has been installed to system locations:"
    echo "  Binary: /usr/lib/pitrac/pitrac_lm"
    echo "  CLI: /usr/bin/pitrac (regenerated)"
    echo "  Libraries: /usr/lib/pitrac/"
    echo "  Configs: /etc/pitrac/"
    echo "  Web Server: /usr/lib/pitrac/web-server (updated)"
    echo ""
    echo ""
    echo "Web server status:"
    if systemctl is-active --quiet pitrac-web.service; then
        echo "  Web service is running"
        echo "  Access dashboard at: http://$(hostname -I | cut -d' ' -f1):8080"
        echo "  Use the Start/Stop buttons in the dashboard to control PiTrac"
    else
        echo "  Web service is not running"
        echo "  Start with: sudo systemctl start pitrac-web.service"
        echo "  Enable on boot: sudo systemctl enable pitrac-web.service"
    fi
    echo ""
    echo "ActiveMQ status:"
    if systemctl is-active --quiet activemq; then
        echo "  ActiveMQ broker is running on port 61616"
    else
        echo "  ActiveMQ is not running"
        echo "  Start with: sudo systemctl start activemq"
    fi
    echo ""
    echo "Manual testing (optional):"
    echo "  pitrac test quick   # Test image processing locally"
    echo "  pitrac help         # Show CLI commands"
    echo ""
    echo "To rebuild after code changes:"
    echo "  sudo ./build.sh dev         # Fast incremental build (only changed files)"
    echo "  sudo ./build.sh dev force   # Full clean rebuild"
}

# Main execution
main() {
    log_info "PiTrac POC Build System"
    log_info "Action: $ACTION"

    case "$ACTION" in
        deps)
            build_deps
            ;;
        build)
            build_pitrac
            ;;
        all)
            build_deps
            build_pitrac
            ;;
        dev)
            build_dev
            ;;
        shell)
            run_shell
            ;;
        clean)
            clean_all
            ;;
        help|--help|-h)
            show_usage
            exit 0
            ;;
        *)
            log_error "Unknown action: $ACTION"
            show_usage
            exit 1
            ;;
    esac

    log_success "Done!"
}

# Check Docker (not needed for dev mode)
if [[ "$ACTION" != "dev" ]]; then
    if ! command -v docker &>/dev/null; then
        log_error "Docker is required but not installed"
        exit 1
    fi
fi

# Ensure artifact directory exists
mkdir -p "$ARTIFACT_DIR"

# Run main
main