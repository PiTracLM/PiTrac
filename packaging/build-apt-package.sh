#!/usr/bin/env bash
# PiTrac APT Package Builder v2
# Builds .deb package from pre-built artifacts
set -euo pipefail

VERSION="${PITRAC_VERSION:-1.0.0}"
ARCH="${PITRAC_ARCH:-arm64}"
MAINTAINER="PiTrac Team <team@pitrac.io>"
DESCRIPTION="Open-source DIY golf launch monitor for Raspberry Pi"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
log_success() { echo -e "${GREEN}[✓]${NC} $*"; }
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
POC_DIR="$SCRIPT_DIR"
BUILD_DIR="$POC_DIR/build/package"
DEB_DIR="$BUILD_DIR/debian"
PACKAGE_NAME="pitrac_${VERSION}_${ARCH}"

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    local missing=0
    for artifact in opencv-4.11.0-arm64.tar.gz activemq-cpp-3.9.5-arm64.tar.gz \
                    lgpio-0.2.2-arm64.tar.gz msgpack-cxx-6.1.1-arm64.tar.gz \
                    tomee-10.1.0-plume-arm64.tar.gz; do
        if [[ ! -f "$POC_DIR/deps-artifacts/$artifact" ]]; then
            log_error "Missing artifact: $artifact"
            missing=1
        fi
    done
    
    if [[ ! -f "$POC_DIR/deps-artifacts/golfsim-1.0.0-noarch.war" ]]; then
        log_warn "golfsim.war not found. Run ./scripts/build-webapp.sh to build it"
        log_warn "Continuing without web application..."
    else
        log_success "Found pre-built web application"
    fi
    
    if [[ $missing -eq 1 ]]; then
        log_error "Run ./scripts/build-all-deps.sh first to build all artifacts"
        exit 1
    fi
    
    if ! command -v docker &> /dev/null; then
        log_error "Docker is required but not installed"
        exit 1
    fi
    
    log_success "All prerequisites found"
}

# Install pre-built webapp if available
install_webapp() {
    local webapp_artifact="$POC_DIR/deps-artifacts/golfsim-1.0.0-noarch.war"
    
    if [[ -f "$webapp_artifact" ]]; then
        log_info "Installing pre-built web application..."
        mkdir -p "$DEB_DIR/usr/share/pitrac/webapp"
        cp "$webapp_artifact" "$DEB_DIR/usr/share/pitrac/webapp/golfsim.war"
        log_success "Web application installed"
        return 0
    else
        log_warn "Web application not found. Skipping..."
        return 1
    fi
}

# Extract PiTrac binary from Docker container
extract_pitrac_binary() {
    log_info "Extracting PiTrac binary from Docker build..."
    
    # Check if we already have the built binary from a previous run
    if [[ -f "$REPO_ROOT/Software/LMSourceCode/ImageProcessing/build/pitrac_lm" ]]; then
        log_info "Using existing built binary"
        cp "$REPO_ROOT/Software/LMSourceCode/ImageProcessing/build/pitrac_lm" "$BUILD_DIR/pitrac_lm.tmp"
        log_success "Binary found and copied"
    else
        log_info "Binary not found, extracting from Docker container..."
        
        # Create a container from the built image to extract the binary
        local container_id=$(docker create pitrac-poc:arm64 2>/dev/null)
        
        if [[ -z "$container_id" ]]; then
            log_error "No PiTrac Docker image found. Run ./build.sh first!"
            exit 1
        fi
        
        # Extract the binary
        docker cp "$container_id:/build/Software/LMSourceCode/ImageProcessing/build/pitrac_lm" "$BUILD_DIR/pitrac_lm.tmp"
        docker rm "$container_id" > /dev/null
        
        if [[ ! -f "$BUILD_DIR/pitrac_lm.tmp" ]]; then
            log_error "Failed to extract binary from Docker"
            exit 1
        fi
        
        log_success "Binary extracted from Docker"
    fi
}

create_cli_wrapper() {
    log_info "Creating CLI wrapper..."
    
    # First, generate the Bashly script if it doesn't exist
    if [[ ! -f "$SCRIPT_DIR/pitrac" ]]; then
        log_info "Generating Bashly CLI script..."
        if [[ -f "$SCRIPT_DIR/generate.sh" ]]; then
            cd "$SCRIPT_DIR"
            ./generate.sh
            cd - > /dev/null
        else
            log_error "Cannot generate Bashly script - generate.sh not found"
            exit 1
        fi
    fi
    
    if [[ ! -f "$SCRIPT_DIR/pitrac" ]]; then
        log_error "Bashly CLI not found! Run ./generate.sh first"
        exit 1
    fi
    log_info "Using Bashly-generated CLI"
    cp "$SCRIPT_DIR/pitrac" "$BUILD_DIR/pitrac-cli"
    
    chmod 755 "$BUILD_DIR/pitrac-cli"
    log_success "CLI wrapper created"
}

# Prepare build environment
prepare_build_env() {
    log_info "Preparing build environment..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$DEB_DIR"/{DEBIAN,usr/{bin,lib/pitrac,share/{pitrac,doc/pitrac}},etc/pitrac,opt/{tomee,pitrac},var/lib/pitrac}
    mkdir -p "$DEB_DIR/etc/systemd/system"
    mkdir -p "$DEB_DIR/usr/share/pitrac"/{webapp,test-images,calibration,templates}
    mkdir -p "$DEB_DIR/etc/pitrac/config"
    log_success "Build directories created"
}

# Install binaries
install_binaries() {
    log_info "Installing binaries..."
    
    # Install main binary
    install -m 755 "$BUILD_DIR/pitrac_lm.tmp" "$DEB_DIR/usr/lib/pitrac/pitrac_lm"
    
    # Install CLI tool (Bashly-generated script is self-contained)
    install -m 755 "$BUILD_DIR/pitrac-cli" "$DEB_DIR/usr/bin/pitrac"
    
    log_success "Binaries installed"
}

# Install test images and calibration tools
install_test_resources() {
    log_info "Installing test images and calibration tools..."
    
    local test_images_dir="$REPO_ROOT/Software/LMSourceCode/Images"
    if [[ -d "$test_images_dir" ]]; then
        if [[ -f "$test_images_dir/gs_log_img__log_ball_final_found_ball_img.png" ]]; then
            cp "$test_images_dir/gs_log_img__log_ball_final_found_ball_img.png" \
               "$DEB_DIR/usr/share/pitrac/test-images/teed-ball.png"
        fi
        if [[ -f "$test_images_dir/log_cam2_last_strobed_img.png" ]]; then
            cp "$test_images_dir/log_cam2_last_strobed_img.png" \
               "$DEB_DIR/usr/share/pitrac/test-images/strobed.png"
        fi
        log_success "Test images installed"
    else
        log_warn "Test images directory not found"
    fi
    
    local calib_dir="$REPO_ROOT/Software/CalibrateCameraDistortions"
    if [[ -d "$calib_dir" ]]; then
        cp "$calib_dir/CameraCalibration.py" "$DEB_DIR/usr/share/pitrac/calibration/" 2>/dev/null || true
        cp "$calib_dir/checkerboard.png" "$DEB_DIR/usr/share/pitrac/calibration/" 2>/dev/null || true
        log_success "Calibration tools installed"
    else
        log_warn "Calibration tools not found"
    fi
    
    log_info "Installing camera tools..."
    local camera_tools_dir="$REPO_ROOT/Software/LMSourceCode/ImageProcessing/CameraTools"
    if [[ -d "$camera_tools_dir" ]]; then
        mkdir -p "$DEB_DIR/usr/lib/pitrac/ImageProcessing/CameraTools"
        cp -r "$camera_tools_dir"/* "$DEB_DIR/usr/lib/pitrac/ImageProcessing/CameraTools/"
        
        find "$DEB_DIR/usr/lib/pitrac/ImageProcessing/CameraTools" -name "*.sh" -type f -exec chmod 755 {} \;
        if [[ -f "$DEB_DIR/usr/lib/pitrac/ImageProcessing/CameraTools/imx296_trigger" ]]; then
            chmod 755 "$DEB_DIR/usr/lib/pitrac/ImageProcessing/CameraTools/imx296_trigger"
        fi
        
        log_success "Camera tools installed"
    else
        log_warn "Camera tools not found"
    fi
    
    cat > "$DEB_DIR/usr/lib/pitrac/calibration-wizard" << 'EOF'
#!/bin/bash
if [[ -f /usr/share/pitrac/calibration/CameraCalibration.py ]]; then
    cd /usr/share/pitrac/calibration
    python3 CameraCalibration.py "$@"
else
    echo "Calibration tools not installed"
    exit 1
fi
EOF
    chmod 755 "$DEB_DIR/usr/lib/pitrac/calibration-wizard"
}

bundle_dependencies() {
    log_info "Bundling dependencies..."
    
    local lib_dir="$DEB_DIR/usr/lib/pitrac"
    
    log_info "  OpenCV 4.11.0..."
    tar xzf "$POC_DIR/deps-artifacts/opencv-4.11.0-arm64.tar.gz" -C /tmp/
    cp -r /tmp/opencv/lib/*.so* "$lib_dir/" 2>/dev/null || true
    rm -rf /tmp/opencv
    
    log_info "  ActiveMQ-CPP 3.9.5..."
    tar xzf "$POC_DIR/deps-artifacts/activemq-cpp-3.9.5-arm64.tar.gz" -C /tmp/
    cp -r /tmp/activemq-cpp/lib/*.so* "$lib_dir/" 2>/dev/null || true
    rm -rf /tmp/activemq-cpp
    
    log_info "  lgpio 0.2.2..."
    tar xzf "$POC_DIR/deps-artifacts/lgpio-0.2.2-arm64.tar.gz" -C /tmp/
    cp -r /tmp/lgpio/lib/*.so* "$lib_dir/" 2>/dev/null || true
    rm -rf /tmp/lgpio
    
    log_info "  msgpack-cxx 6.1.1..."
    tar xzf "$POC_DIR/deps-artifacts/msgpack-cxx-6.1.1-arm64.tar.gz" -C /tmp/
    if [[ -d /tmp/msgpack/lib ]]; then
        cp -r /tmp/msgpack/lib/*.so* "$lib_dir/" 2>/dev/null || true
    fi
    rm -rf /tmp/msgpack
    
    log_info "  TomEE 10.1.0 Plume..."
    tar xzf "$POC_DIR/deps-artifacts/tomee-10.1.0-plume-arm64.tar.gz" -C "$DEB_DIR/opt/"
    
    strip --strip-unneeded "$lib_dir"/*.so* 2>/dev/null || true
    
    log_success "Dependencies bundled"
}

create_configs() {
    log_info "Creating configs..."
    
    cp "$SCRIPT_DIR/templates/pitrac.yaml" "$DEB_DIR/etc/pitrac/pitrac.yaml"
    cp "$DEB_DIR/etc/pitrac/pitrac.yaml" "$DEB_DIR/usr/share/pitrac/config.yaml.default"
    
    cp "$SCRIPT_DIR/templates/golf_sim_config.json" "$DEB_DIR/etc/pitrac/golf_sim_config.json"
    cp "$SCRIPT_DIR/templates/golf_sim_config.json" "$DEB_DIR/usr/share/pitrac/golf_sim_config.json.default"
    
    # Install configuration templates (required by generated pitrac CLI)
    if [[ -d "$SCRIPT_DIR/templates/config" ]]; then
        cp "$SCRIPT_DIR/templates/config/settings-basic.yaml" "$DEB_DIR/etc/pitrac/config/"
        cp "$SCRIPT_DIR/templates/config/settings-advanced.yaml" "$DEB_DIR/etc/pitrac/config/"
        cp "$SCRIPT_DIR/templates/config/parameter-mappings.yaml" "$DEB_DIR/etc/pitrac/config/"
        cp "$SCRIPT_DIR/templates/config/README.md" "$DEB_DIR/etc/pitrac/config/"
        log_info "Configuration templates installed"
    else
        log_warn "Configuration templates not found in $SCRIPT_DIR/templates/config/"
    fi

    cp "$SCRIPT_DIR/templates/pitrac.service.template" "$DEB_DIR/usr/share/pitrac/templates/pitrac.service.template"
    cp "$SCRIPT_DIR/templates/tomee.service" "$DEB_DIR/etc/systemd/system/tomee.service"
    
    cp "$SCRIPT_DIR/src/lib/service-install.sh" "$DEB_DIR/usr/lib/pitrac/service-install.sh"
    chmod 755 "$DEB_DIR/usr/lib/pitrac/service-install.sh"
    
    # Install tomee wrapper script
    cp "$SCRIPT_DIR/templates/tomee-wrapper.sh" "$DEB_DIR/usr/lib/pitrac/tomee-wrapper.sh"
    chmod 755 "$DEB_DIR/usr/lib/pitrac/tomee-wrapper.sh"

    log_success "Configs created"
}

create_debian_control() {
    log_info "Creating Debian control files..."
    
    # Calculate installed size
    local size=$(du -sk "$DEB_DIR" | cut -f1)
    
    # Control file
    cat > "$DEB_DIR/DEBIAN/control" << EOF
Package: pitrac
Version: $VERSION
Architecture: $ARCH
Maintainer: $MAINTAINER
Installed-Size: $size
Depends: libc6 (>= 2.36), libstdc++6 (>= 12), libgcc-s1 (>= 12), libcamera0.0.3, libcamera-dev, libcamera-tools, rpicam-apps, libboost-system1.74.0, libboost-thread1.74.0, libboost-program-options1.74.0, libboost-filesystem1.74.0, libboost-log1.74.0, libboost-regex1.74.0, libboost-timer1.74.0, libfmt9, libssl3, libtbb12, libgstreamer1.0-0, libgstreamer-plugins-base1.0-0, libgtk-3-0, libavcodec59, libavformat59, libswscale6, libopenexr-3-1-30, libavutil57, libavdevice59, libexif12, libjpeg62-turbo, libtiff6, libpng16-16, libdrm2, libx11-6, libepoxy0, libqt5core5a, libqt5widgets5, libqt5gui5, libapr1, libaprutil1, libuuid1, activemq, default-jre-headless | openjdk-17-jre-headless, gpiod, net-tools, python3, python3-opencv, python3-numpy, python3-yaml
Recommends: maven, yq
Section: misc
Priority: optional
Homepage: https://github.com/jamespilgrim/PiTrac
Description: $DESCRIPTION
 PiTrac uses Raspberry Pi cameras to track golf ball
 launch parameters. Includes pre-built binaries with
 OpenCV 4.11.0, ActiveMQ-CPP, and TomEE web server.
EOF

    cp "$SCRIPT_DIR/templates/postinst.sh" "$DEB_DIR/DEBIAN/postinst"
    chmod 755 "$DEB_DIR/DEBIAN/postinst"

    cp "$SCRIPT_DIR/templates/prerm.sh" "$DEB_DIR/DEBIAN/prerm"
    chmod 755 "$DEB_DIR/DEBIAN/prerm"

    cat > "$DEB_DIR/DEBIAN/conffiles" << EOF
/etc/pitrac/pitrac.yaml
/etc/pitrac/golf_sim_config.json
EOF

    log_success "Control files created"
}

build_package() {
    log_info "Building package..."
    
    cd "$BUILD_DIR"
    
    find debian -type d -exec chmod 755 {} \;
    find debian -type f -exec chmod 644 {} \;
    chmod 755 debian/DEBIAN/{postinst,prerm}
    chmod 755 debian/usr/bin/pitrac
    chmod 755 debian/usr/lib/pitrac/pitrac_lm
    chmod 755 debian/usr/lib/pitrac/calibration-wizard
    chmod -R 755 debian/opt/tomee/bin
    
    dpkg-deb --root-owner-group --build debian "$PACKAGE_NAME.deb"
    
    echo ""
    dpkg-deb --info "$PACKAGE_NAME.deb"
    
    log_success "Package built: $BUILD_DIR/$PACKAGE_NAME.deb"
}

main() {
    log_info "Building PiTrac package v$VERSION"
    
    check_prerequisites
    prepare_build_env
    
    extract_pitrac_binary
    create_cli_wrapper
    install_binaries
    install_test_resources
    install_webapp || log_warn "Web application not available"
    bundle_dependencies
    create_configs
    create_debian_control
    build_package
    
    echo ""
    log_success "Done!"
    echo ""
    echo "Package: $BUILD_DIR/$PACKAGE_NAME.deb"
    echo "Size: $(du -h $BUILD_DIR/$PACKAGE_NAME.deb | cut -f1)"
    echo ""
    echo "To install:"
    echo "  scp $BUILD_DIR/$PACKAGE_NAME.deb pi@raspberrypi:~/"
    echo "  ssh pi@raspberrypi"
    echo "  sudo apt install ./$PACKAGE_NAME.deb"
    echo "  pitrac setup"
    echo "  sudo reboot"
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi