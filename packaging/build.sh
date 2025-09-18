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

    log_info "Checking for pre-built artifacts..."

    if [ ! -f "$ARTIFACT_DIR/opencv-4.11.0-arm64.tar.gz" ]; then
        missing+=("opencv")
    fi
    if [ ! -f "$ARTIFACT_DIR/lgpio-0.2.2-arm64.tar.gz" ]; then
        missing+=("lgpio")
    fi
    if [ ! -f "$ARTIFACT_DIR/msgpack-cxx-6.1.1-arm64.tar.gz" ]; then
        missing+=("msgpack")
    fi

    if [ ${#missing[@]} -gt 0 ]; then
        log_warn "Missing artifacts: ${missing[*]}"
        return 1
    else
        log_success "All artifacts present"
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
               liblgpio-dev liblgpio1 libmsgpack-cxx-dev \
               libapr1 libaprutil1 libapr1-dev libaprutil1-dev; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            missing_deps+=("$pkg")
        fi
    done
    
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

    # ZeroMQ message library
    if ! dpkg -l | grep -q "^ii  libzmq3-dev"; then
        missing_deps+=("libzmq3-dev")
    fi
    if ! dpkg -l | grep -q "^ii  cppzmq-dev"; then
        missing_deps+=("cppzmq-dev")
    fi

    if [ ${#missing_deps[@]} -gt 0 ]; then
        log_warn "Missing build dependencies: ${missing_deps[*]}"
        log_info "Installing missing dependencies..."
        apt-get update
        apt-get install -y "${missing_deps[@]}"
    fi

    log_info "Extracting pre-built dependencies..."
    mkdir -p /usr/lib/pitrac
    extract_all_dependencies "$ARTIFACT_DIR" "/usr/lib/pitrac"
    

    if [[ ! -d /opt/opencv/include/opencv4 ]]; then
        log_info "  Setting up OpenCV headers..."
        tar xzf "$ARTIFACT_DIR/opencv-4.11.0-arm64.tar.gz" -C /tmp/
        mkdir -p /opt/opencv
        cp -r /tmp/opencv/* /opt/opencv/
        rm -rf /tmp/opencv
    else
        log_info "  OpenCV headers already installed"
    fi

    # Update library cache
    ldconfig

    # Configure libcamera using common function
    configure_libcamera

    # Create pkg-config files using common function
    create_pkgconfig_files

    # Build PiTrac
    log_info "Building PiTrac..."
    cd "$REPO_ROOT/Software/LMSourceCode/ImageProcessing"

    # Apply Boost C++20 fix using common function
    apply_boost_cxx20_fix

    # Set build environment
    export PKG_CONFIG_PATH="/opt/opencv/lib/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig"
    export LD_LIBRARY_PATH="/usr/lib/pitrac:${LD_LIBRARY_PATH:-}"
    export CMAKE_PREFIX_PATH="/opt/opencv"
    export CPLUS_INCLUDE_PATH="/opt/opencv/include/opencv4"
    export PITRAC_ROOT="$REPO_ROOT/Software/LMSourceCode"
    export CXXFLAGS="-I/opt/opencv/include/opencv4"

    # Create dummy closed source file if needed
    mkdir -p ClosedSourceObjectFiles
    touch ClosedSourceObjectFiles/gs_e6_response.cpp.o

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

    install_test_images "/usr/share/pitrac/test-images" "$REPO_ROOT"
    
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

    # ZeroMQ is configured through the application - no service configuration needed
    log_info "ZeroMQ libraries are now installed and available for PiTrac"

    # Clean up old ActiveMQ installation if it exists (from previous versions)
    log_info "Checking for and removing old ActiveMQ installation..."

    # Stop ActiveMQ service if running
    if systemctl is-active --quiet activemq; then
        log_info "Stopping ActiveMQ service..."
        systemctl stop activemq || true
    fi

    # Disable ActiveMQ service
    if systemctl list-unit-files | grep -q "activemq.service"; then
        log_info "Disabling ActiveMQ service..."
        systemctl disable activemq 2>/dev/null || true
    fi

    # Remove ActiveMQ configuration files
    if [[ -d /etc/activemq ]] || [[ -d /usr/share/activemq ]]; then
        log_info "Removing ActiveMQ configuration and files..."
        rm -rf /etc/activemq 2>/dev/null || true
        rm -rf /usr/share/activemq/conf/activemq.xml 2>/dev/null || true
    fi

    # Remove old ActiveMQ-CPP headers and libraries if they exist
    if [[ -d /opt/activemq-cpp ]]; then
        log_info "Removing old ActiveMQ-CPP installation..."
        rm -rf /opt/activemq-cpp
    fi

    # Clean up ActiveMQ template files from PiTrac
    if [[ -f /usr/share/pitrac/templates/activemq.xml.template ]]; then
        log_info "Removing old ActiveMQ templates..."
        rm -f /usr/share/pitrac/templates/activemq.xml.template
        rm -f /usr/share/pitrac/templates/log4j2.properties.template
        rm -f /usr/share/pitrac/templates/activemq-options.template
    fi

    # Remove ActiveMQ service installer script
    if [[ -f /usr/lib/pitrac/activemq-service-install.sh ]]; then
        rm -f /usr/lib/pitrac/activemq-service-install.sh
    fi

    # Note: We don't uninstall the activemq package itself in case user needs it for other purposes
    # They can remove it manually with: sudo apt remove activemq

    if command -v activemq &>/dev/null; then
        log_warn "ActiveMQ package is still installed but no longer used by PiTrac"
        log_info "You can remove it with: sudo apt remove activemq"
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
    echo "ZeroMQ status:"
    echo "  ZeroMQ libraries are installed and ready for use"
    echo "  No broker service required - ZeroMQ is embedded in the application"
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