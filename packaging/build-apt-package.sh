#!/usr/bin/env bash
# PiTrac APT Package Builder v2
# Builds .deb package from pre-built artifacts
set -euo pipefail

VERSION="${PITRAC_VERSION:-1.0.0}"
ARCH="${PITRAC_ARCH:-arm64}"
MAINTAINER="PiTrac Team <team@pitrac.io>"
DESCRIPTION="Open-source DIY golf launch monitor for Raspberry Pi"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -f "$SCRIPT_DIR/src/lib/pitrac-common-functions.sh" ]]; then
    source "$SCRIPT_DIR/src/lib/pitrac-common-functions.sh"
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
fi

REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
POC_DIR="$SCRIPT_DIR"
BUILD_DIR="$POC_DIR/build/package"
DEB_DIR="$BUILD_DIR/debian"
PACKAGE_NAME="pitrac_${VERSION}_${ARCH}"

check_prerequisites() {
    log_info "Checking prerequisites..."
    
    local missing=0
    for artifact in opencv-4.11.0-arm64.tar.gz \
                    lgpio-0.2.2-arm64.tar.gz msgpack-cxx-6.1.1-arm64.tar.gz; do
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

extract_pitrac_binary() {
    log_info "Extracting PiTrac binary from Docker build..."
    
    if [[ -f "$REPO_ROOT/Software/LMSourceCode/ImageProcessing/build/pitrac_lm" ]]; then
        log_info "Using existing built binary"
        cp "$REPO_ROOT/Software/LMSourceCode/ImageProcessing/build/pitrac_lm" "$BUILD_DIR/pitrac_lm.tmp"
        log_success "Binary found and copied"
    else
        log_info "Binary not found, extracting from Docker container..."
        
        local container_id=$(docker create pitrac-poc:arm64 2>/dev/null)
        
        if [[ -z "$container_id" ]]; then
            log_error "No PiTrac Docker image found. Run ./build.sh first!"
            exit 1
        fi
        
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

prepare_build_env() {
    log_info "Preparing build environment..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$DEB_DIR"/{DEBIAN,usr/{bin,lib/pitrac,share/{pitrac,doc/pitrac}},etc/pitrac,opt/pitrac,var/lib/pitrac}
    mkdir -p "$DEB_DIR/etc/systemd/system"
    mkdir -p "$DEB_DIR/usr/share/pitrac"/{webapp,test-images,calibration,templates}
    mkdir -p "$DEB_DIR/etc/pitrac/config"
    log_success "Build directories created"
}

install_binaries() {
    log_info "Installing binaries..."
    
    install -m 755 "$BUILD_DIR/pitrac_lm.tmp" "$DEB_DIR/usr/lib/pitrac/pitrac_lm"
    
    install -m 755 "$BUILD_DIR/pitrac-cli" "$DEB_DIR/usr/bin/pitrac"
    
    log_success "Binaries installed"
}

install_test_resources() {
    log_info "Installing test images and calibration tools..."

    install_test_images "$DEB_DIR/usr/share/pitrac/test-images" "$REPO_ROOT"
    install_test_suites "$DEB_DIR/usr/share/pitrac/test-suites" "$REPO_ROOT"
    install_camera_tools "$DEB_DIR/usr/lib/pitrac" "$REPO_ROOT"
    
    log_info "Staging ONNX models..."
    local models_dir="$REPO_ROOT/Software/LMSourceCode/ml_models"
    if [[ -d "$models_dir" ]]; then
        mkdir -p "$DEB_DIR/usr/share/pitrac/models"
        local models_found=0
        for model_path in "$models_dir"/*/weights/best.onnx; do
            if [[ -f "$model_path" ]]; then
                local model_name=$(basename "$(dirname "$(dirname "$model_path")")")
                mkdir -p "$DEB_DIR/usr/share/pitrac/models/$model_name"
                cp "$model_path" "$DEB_DIR/usr/share/pitrac/models/$model_name/best.onnx"
                log_info "  Staged model: $model_name/best.onnx"
                ((models_found++))
            fi
        done
        if [[ $models_found -gt 0 ]]; then
            log_success "Staged $models_found ONNX model(s)"
        fi
    else
        log_warn "ONNX models directory not found: $models_dir"
    fi
    
    local calib_dir="$REPO_ROOT/Software/CalibrateCameraDistortions"
    if [[ -d "$calib_dir" ]]; then
        cp "$calib_dir/CameraCalibration.py" "$DEB_DIR/usr/share/pitrac/calibration/" 2>/dev/null || true
        cp "$calib_dir/checkerboard.png" "$DEB_DIR/usr/share/pitrac/calibration/" 2>/dev/null || true
        log_success "Calibration tools installed"
    else
        log_warn "Calibration tools not found"
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
    
    extract_all_dependencies "$POC_DIR/deps-artifacts" "$lib_dir"
    
    log_info "  PiTrac Web Server..."
    WEB_SERVER_DIR="$REPO_ROOT/Software/web-server"
    if [[ -d "$WEB_SERVER_DIR" ]]; then
        mkdir -p "$DEB_DIR/usr/lib/pitrac/web-server"
        cp -r "$WEB_SERVER_DIR"/* "$DEB_DIR/usr/lib/pitrac/web-server/"
        log_info "    Web server files copied"
    else
        log_error "  Web server not found at $WEB_SERVER_DIR"
        exit 1
    fi
    
    strip --strip-unneeded "$lib_dir"/*.so* 2>/dev/null || true
    
    log_success "Dependencies bundled"
}

create_configs() {
    log_info "Creating configs..."
    
    # cp "$SCRIPT_DIR/templates/pitrac.yaml" "$DEB_DIR/etc/pitrac/pitrac.yaml"
    # cp "$DEB_DIR/etc/pitrac/pitrac.yaml" "$DEB_DIR/usr/share/pitrac/config.yaml.default"
    
    cp "$SCRIPT_DIR/templates/golf_sim_config.json" "$DEB_DIR/etc/pitrac/golf_sim_config.json"
    cp "$SCRIPT_DIR/templates/golf_sim_config.json" "$DEB_DIR/usr/share/pitrac/golf_sim_config.json.default"
    
    if [[ -d "$SCRIPT_DIR/templates/config" ]]; then
        # cp "$SCRIPT_DIR/templates/config/parameter-mappings.yaml" "$DEB_DIR/etc/pitrac/config/"
        # log_info "Parameter mappings installed"
    else
        log_warn "Configuration templates not found in $SCRIPT_DIR/templates/config/"
    fi

    cp "$SCRIPT_DIR/templates/pitrac-web.service.template" "$DEB_DIR/usr/share/pitrac/templates/pitrac-web.service.template"
    
    
    
    if [[ -f "$SCRIPT_DIR/src/lib/web-service-install.sh" ]]; then
        cp "$SCRIPT_DIR/src/lib/web-service-install.sh" "$DEB_DIR/usr/lib/pitrac/web-service-install.sh"
        chmod 755 "$DEB_DIR/usr/lib/pitrac/web-service-install.sh"
    fi
    
    if [[ -f "$SCRIPT_DIR/src/lib/pitrac-common-functions.sh" ]]; then
        cp "$SCRIPT_DIR/src/lib/pitrac-common-functions.sh" "$DEB_DIR/usr/lib/pitrac/pitrac-common-functions.sh"
        chmod 644 "$DEB_DIR/usr/lib/pitrac/pitrac-common-functions.sh"
    fi
    

    log_success "Configs created"
}

create_debian_control() {
    log_info "Creating Debian control files..."
    
    local size=$(du -sk "$DEB_DIR" | cut -f1)
    
    cat > "$DEB_DIR/DEBIAN/control" << EOF
Package: pitrac
Version: $VERSION
Architecture: $ARCH
Maintainer: $MAINTAINER
Installed-Size: $size
Depends: libc6 (>= 2.36), libstdc++6 (>= 12), libgcc-s1 (>= 12), libcamera0.0.3, libcamera-dev, libcamera-tools, rpicam-apps, libboost-system1.74.0, libboost-thread1.74.0, libboost-program-options1.74.0, libboost-filesystem1.74.0, libboost-log1.74.0, libboost-regex1.74.0, libboost-timer1.74.0, libfmt9, libssl3, libtbb12, libgstreamer1.0-0, libgstreamer-plugins-base1.0-0, libgtk-3-0, libavcodec59, libavformat59, libswscale6, libopenexr-3-1-30, libavutil57, libavdevice59, libexif12, libjpeg62-turbo, libtiff6, libpng16-16, libdrm2, libx11-6, libepoxy0, libqt5core5a, libqt5widgets5, libqt5gui5, libapr1, libaprutil1, libuuid1, libzmq3-dev, python3-zmq, gpiod, net-tools, python3, python3-opencv, python3-numpy, python3-yaml, yq
Recommends: maven
Section: misc
Priority: optional
Homepage: https://github.com/jamespilgrim/PiTrac
Description: $DESCRIPTION
 PiTrac uses Raspberry Pi cameras to track golf ball
 launch parameters. Includes pre-built binaries with
 OpenCV 4.11.0, ZeroMQ, and Python web server.
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