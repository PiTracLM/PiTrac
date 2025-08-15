#!/usr/bin/env bash
set -euo pipefail

# PiTrac Build Script
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Load defaults from config file
load_defaults "pitrac-build" "$@"

# Configuration from defaults
PITRAC_REPO="${PITRAC_REPO:-https://github.com/jamespilgrim/PiTrac.git}"
PITRAC_BRANCH="${PITRAC_BRANCH:-main}"
PITRAC_PR="${PITRAC_PR:-0}"
BUILD_DIR="${BUILD_DIR:-$HOME/Dev}"
BUILD_DIR="${BUILD_DIR/#\~/$HOME}"  # Expand tilde
PITRAC_DIR="${BUILD_DIR}/PiTrac"
FORCE_CLONE="${FORCE_CLONE:-0}"
SETUP_GUI="${SETUP_GUI:-1}"
CONFIGURE_SHELL="${CONFIGURE_SHELL:-0}"
BUILD_CORES="${BUILD_CORES:-0}"
CLEAN_BUILD="${CLEAN_BUILD:-0}"
SKIP_GIT_PULL="${SKIP_GIT_PULL:-0}"

PITRAC_SLOT1_CAMERA_TYPE="${PITRAC_SLOT1_CAMERA_TYPE:-4}"
PITRAC_SLOT2_CAMERA_TYPE="${PITRAC_SLOT2_CAMERA_TYPE:-4}"
PITRAC_MSG_BROKER_FULL_ADDRESS="${PITRAC_MSG_BROKER_FULL_ADDRESS:-tcp://localhost:61616}"
PITRAC_BASE_IMAGE_LOGGING_DIR="${PITRAC_BASE_IMAGE_LOGGING_DIR:-~/LM_Shares/Images/}"
PITRAC_WEBSERVER_SHARE_DIR="${PITRAC_WEBSERVER_SHARE_DIR:-~/LM_Shares/WebShare/}"

# Check if PiTrac is already cloned
check_pitrac_source() {
    if [ -d "$PITRAC_DIR" ]; then
        if [ -d "$PITRAC_DIR/.git" ]; then
            log_success "PiTrac source found at $PITRAC_DIR"
            
                    cd "$PITRAC_DIR"
            local current_branch
            current_branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
            log_info "Current branch: $current_branch"
            
            if [ "$current_branch" != "$PITRAC_BRANCH" ] && [ "$FORCE_CLONE" = "0" ]; then
                log_warn "Repository is on branch '$current_branch' but config specifies '$PITRAC_BRANCH'"
                if ! is_non_interactive; then
                    read -p "Switch to branch $PITRAC_BRANCH? (y/N): " -n 1 -r
                    echo
                    if [[ $REPLY =~ ^[Yy]$ ]]; then
                        git checkout "$PITRAC_BRANCH" || log_error "Failed to switch branch"
                    fi
                fi
            fi
            return 0
        else
            log_warn "Directory exists but is not a git repository"
            return 1
        fi
    fi
    return 1
}

# Clone or update PiTrac
get_pitrac_source() {
    if [ "$FORCE_CLONE" = "1" ] && [ -d "$PITRAC_DIR" ]; then
        log_warn "Force clone requested, removing existing repository..."
        rm -rf "$PITRAC_DIR"
    fi
    
    if check_pitrac_source; then
        # Ask user if they want to skip git pull (unless already set)
        if [ "$SKIP_GIT_PULL" != "1" ] && ! is_non_interactive; then
            log_info "Repository found at $PITRAC_DIR"
            read -p "Skip git pull/fetch and use existing code? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                SKIP_GIT_PULL=1
            fi
        fi
        
        if [ "$SKIP_GIT_PULL" = "1" ]; then
            log_info "Skipping git pull (using existing code)"
            cd "$PITRAC_DIR"
        else
            log_info "Updating existing PiTrac repository..."
            cd "$PITRAC_DIR"
            
                if ! git diff --quiet || ! git diff --cached --quiet; then
                log_warn "Local changes detected, stashing..."
                git stash push -m "Auto-stash before update $(date +%Y%m%d_%H%M%S)"
            fi
            
            if [ "$PITRAC_PR" != "0" ] && [ -n "$PITRAC_PR" ]; then
                log_info "Fetching and checking out pull request #$PITRAC_PR..."
                git fetch origin "pull/$PITRAC_PR/head:pr-$PITRAC_PR"
                git checkout "pr-$PITRAC_PR"
            else
                git fetch origin
                git pull origin "$PITRAC_BRANCH" || log_warn "Could not update (might have conflicts)"
            fi
        fi
    else
        log_info "Cloning PiTrac repository..."
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"
        
        if ! git clone "$PITRAC_REPO"; then
            log_error "Failed to clone PiTrac repository"
            return 1
        fi
        
        cd "$PITRAC_DIR"
        
        if [ "$PITRAC_PR" != "0" ] && [ -n "$PITRAC_PR" ]; then
            log_info "Fetching and checking out pull request #$PITRAC_PR..."
            git fetch origin "pull/$PITRAC_PR/head:pr-$PITRAC_PR"
            git checkout "pr-$PITRAC_PR"
        elif [ "$PITRAC_BRANCH" != "main" ]; then
            log_info "Checking out branch: $PITRAC_BRANCH"
            git checkout "$PITRAC_BRANCH" || git checkout -b "$PITRAC_BRANCH" "origin/$PITRAC_BRANCH"
        fi
        
        if [ ! -d "$PITRAC_DIR" ]; then
            log_error "Repository cloned but directory not found"
            return 1
        fi
        
        log_success "Repository cloned successfully"
    fi
    return 0
}

# Setup environment variables
setup_environment() {
    log_info "Setting up PiTrac environment variables..."
    
    local detected_root="$(detect_pitrac_root)"
    if [ -d "$detected_root" ]; then
        export PITRAC_ROOT="$detected_root"
    else
        export PITRAC_ROOT="${PITRAC_DIR}/Software/LMSourceCode"
    fi
    
    if [ "$CONFIGURE_SHELL" != "1" ]; then
        log_info "Skipping shell configuration (CONFIGURE_SHELL=0)"
        return 0
    fi
    
    if grep -q "PITRAC_ROOT" ~/.bashrc 2>/dev/null || grep -q "PITRAC_ROOT" ~/.zshrc 2>/dev/null; then
        log_info "Environment variables already configured in shell profile"
        return 0
    fi
    
    local env_setup="
# PiTrac Build Environment Variables (if not already set)
export PITRAC_ROOT=${PITRAC_ROOT}
export PITRAC_BASE_IMAGE_LOGGING_DIR=${PITRAC_BASE_IMAGE_LOGGING_DIR}
export PITRAC_WEBSERVER_SHARE_DIR=${PITRAC_WEBSERVER_SHARE_DIR}
export PITRAC_MSG_BROKER_FULL_ADDRESS=${PITRAC_MSG_BROKER_FULL_ADDRESS}

export PITRAC_SLOT1_CAMERA_TYPE=${PITRAC_SLOT1_CAMERA_TYPE}
export PITRAC_SLOT2_CAMERA_TYPE=${PITRAC_SLOT2_CAMERA_TYPE}

export LIBCAMERA_RPI_CONFIG_FILE=${LIBCAMERA_RPI_CONFIG_FILE:-/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml}
"
    
    for profile in ~/.bashrc ~/.zshrc; do
        if [ -f "$profile" ]; then
            echo "$env_setup" >> "$profile"
            log_success "Added environment variables to $profile"
        fi
    done
    
    eval "$env_setup"
}

# Configure libcamera timeout
configure_libcamera_timeout() {
    log_info "Configuring libcamera timeout..."
    
    local rpi_config_dirs=(
        "/usr/share/libcamera/pipeline/rpi/pisp"
        "/usr/share/libcamera/pipeline/rpi/vc4"
        "/usr/local/share/libcamera/pipeline/rpi/pisp"
        "/usr/local/share/libcamera/pipeline/rpi/vc4"
    )
    
    for dir in "${rpi_config_dirs[@]}"; do
        if [ -d "$dir" ]; then
            local yaml_file="$dir/rpi_apps.yaml"
            
            if [ ! -f "$yaml_file" ] && [ -f "$dir/example.yaml" ]; then
                log_info "Creating rpi_apps.yaml from example in $dir"
                $SUDO cp "$dir/example.yaml" "$yaml_file"
            fi
            
            if [ -f "$yaml_file" ]; then
                if ! grep -q "camera_timeout_value_ms" "$yaml_file" 2>/dev/null; then
                    log_info "Adding timeout to $yaml_file"
                    log_warn "Please manually add 'camera_timeout_value_ms: 1000000' to $yaml_file"
                else
                    log_success "Timeout already configured in $yaml_file"
                fi
            fi
        fi
    done
}

# Copy camera configs
copy_camera_configs() {
    log_info "Copying camera configuration files..."
    
    local pi_model=$(detect_pi_model)
    local src_dir="${PITRAC_ROOT}/ImageProcessing"
    
    if [ ! -d "$src_dir" ]; then
        log_error "ImageProcessing directory not found at $src_dir"
        return 1
    fi
    
    if [ "$pi_model" = "5" ]; then
        if [ -f "$src_dir/imx296_noir.json.PI_5_FOR_PISP_DIRECTORY" ]; then
            $SUDO cp "$src_dir/imx296_noir.json.PI_5_FOR_PISP_DIRECTORY" \
                     "/usr/share/libcamera/ipa/rpi/pisp/imx296_noir.json"
            log_success "Copied Pi 5 camera config"
        fi
    elif [ "$pi_model" = "4" ]; then
        if [ -f "$src_dir/imx296_noir.json.PI_4_FOR_VC4_DIRECTORY" ]; then
            $SUDO cp "$src_dir/imx296_noir.json.PI_4_FOR_VC4_DIRECTORY" \
                     "/usr/share/libcamera/ipa/rpi/vc4/imx296_noir.json"
            log_success "Copied Pi 4 camera config"
        fi
    else
        log_warn "Unknown Pi model, skipping camera config copy"
    fi
}

# Build PiTrac
build_pitrac() {
    log_info "Building PiTrac Launch Monitor..."
    
    local dep_resolver="${SCRIPT_DIR}/dep_resolver.sh"
    if [ -f "$dep_resolver" ]; then
        log_info "Checking and installing dependencies..."
        local deps_line=$(grep "^pitrac-build:" "${SCRIPT_DIR}/deps.conf" | cut -d: -f2)
        local deps=(${deps_line//,/ })
        
        for dep in "${deps[@]}"; do
            if [ -n "$dep" ] && [ "$dep" != "function" ]; then
                log_info "Checking dependency: $dep"
                if ! "$dep_resolver" verify "$dep" >/dev/null 2>&1; then
                    log_info "Installing $dep..."
                    if ! "$dep_resolver" install "$dep"; then
                        log_error "Failed to install $dep"
                        return 1
                    fi
                fi
            fi
        done
    else
        log_warn "Dependency resolver not found, continuing without dependency check"
    fi
    
    run_preflight_checks "pitrac-build" || return 1
    
    local build_dir="${PITRAC_ROOT}/ImageProcessing"
    
    if [ ! -d "$build_dir" ]; then
        log_error "ImageProcessing directory not found at $build_dir"
        log_error "Make sure PiTrac repository is properly cloned"
        return 1
    fi
    
    cd "$build_dir"
    
    local arch=$(uname -m)
    if [ "$arch" = "x86_64" ]; then
        log_info "Checking for ARM object files that need to be excluded..."
        for obj_file in *.o; do
            if [ -f "$obj_file" ]; then
                if file "$obj_file" 2>/dev/null | grep -q "ARM\|aarch64"; then
                    if [ ! -f "${obj_file}.bak" ]; then
                        log_info "Moving ARM object file to backup: $obj_file -> ${obj_file}.bak"
                        mv "$obj_file" "${obj_file}.bak"
                    else
                        log_info "Removing ARM object file (backup exists): $obj_file"
                        rm -f "$obj_file"
                    fi
                fi
            fi
        done
    fi
    
    if [ -f "create_closed_source_objects.sh" ]; then
        chmod +x create_closed_source_objects.sh
    fi
    
    if [ "$CLEAN_BUILD" = "1" ] && [ -d "build" ]; then
        log_info "Cleaning existing build directory..."
        rm -rf build
    fi
    
    if [ -f "${SCRIPT_DIR}/patch_meson_x86.sh" ]; then
        chmod +x "${SCRIPT_DIR}/patch_meson_x86.sh"
        "${SCRIPT_DIR}/patch_meson_x86.sh" "./meson.build"
    else
        local arch=$(uname -m)
        if [ "$arch" != "armv7l" ] && [ "$arch" != "aarch64" ]; then
            log_warn "Patch script not found, applying inline patch for x86_64..."
            if [ ! -f "meson.build.original" ]; then
                cp meson.build meson.build.original
            fi
            if ! grep -q "neon = \[\]" meson.build; then
                sed -i "1i # Patched for x86_64 compatibility\nneon = []\nuse_neon = false\n" meson.build
            fi
        fi
    fi
    
    log_info "Setting up build with meson..."
    if [ -d "build" ]; then
        log_info "Build directory exists, cleaning for fresh build..."
        rm -rf build
    fi
    
    if ! meson setup build; then
        log_error "Meson setup failed"
        return 1
    fi
    
    local cores="$BUILD_CORES"
    if [ "$cores" = "0" ]; then
        cores=$(get_cpu_cores)
    fi
    
    local total_mem
    total_mem=$(free -m | awk 'NR==2 {print $2}')
    if [ "$total_mem" -lt 4096 ]; then
        cores=2
        log_warn "Low memory detected, using only $cores cores for compilation"
    fi
    
    log_info "Compiling PiTrac with $cores cores (this may take a while)..."
    
    if run_with_progress "ninja -C build -j$cores" "Building PiTrac" "/tmp/pitrac_build.log"; then
        log_success "PiTrac build completed successfully!"
    else
        log_error "Build failed. Check /tmp/pitrac_build.log for details"
        return 1
    fi
    
    log_info "Testing build..."
    if [ -f "build/pitrac_lm" ]; then
        if [ -x "build/pitrac_lm" ]; then
            local output
            output=$(build/pitrac_lm --help 2>&1 | head -1 || true)
            if [ -n "$output" ]; then
                log_success "PiTrac Launch Monitor built successfully!"
                log_info "Binary location: $(pwd)/build/pitrac_lm"
            else
                log_warn "Binary exists but produces no output (may require hardware)"
                log_success "Build completed - binary created at $(pwd)/build/pitrac_lm"
            fi
        else
            log_error "Launch Monitor binary not executable"
            return 1
        fi
    else
        log_error "Launch Monitor binary not found"
        return 1
    fi
}

# Setup GUI if TomEE is installed
setup_gui() {
    if [ "$SETUP_GUI" != "1" ]; then
        log_info "Skipping GUI setup (SETUP_GUI=0)"
        return 0
    fi
    
    if ! [ -d "/opt/tomee" ]; then
        log_info "TomEE not installed, skipping GUI setup"
        return 0
    fi
    
    log_info "Setting up PiTrac GUI..."
    
    local webapp_dir="$HOME/Dev/WebAppDev"
    mkdir -p "$webapp_dir"
    cd "$webapp_dir"
    
    if [ -f "${PITRAC_ROOT}/ImageProcessing/golfsim_tomee_webapp/refresh_from_dev.sh" ]; then
        cp "${PITRAC_ROOT}/ImageProcessing/golfsim_tomee_webapp/refresh_from_dev.sh" .
        chmod +x refresh_from_dev.sh
        
        log_info "Running refresh script..."
        ./refresh_from_dev.sh
        
        if need_cmd mvn; then
            log_info "Building web application with Maven..."
            mvn package
            
            if [ -f "target/golfsim.war" ]; then
                $SUDO cp target/golfsim.war /opt/tomee/webapps/
                log_success "GUI deployed to TomEE"
                
                local ip_addr
                ip_addr=$(hostname -I | awk '{print $1}')
                log_info "Access the GUI at: http://${ip_addr}:8080/golfsim/monitor"
            else
                log_error "WAR file not created"
            fi
        else
            log_warn "Maven not installed, skipping GUI build"
            log_info "Install Maven with: sudo apt-get install maven"
        fi
    else
        log_warn "Refresh script not found in repository"
    fi
}

is_pitrac_built() {
    [ -f "${PITRAC_ROOT}/ImageProcessing/build/pitrac_lm" ] && \
    "${PITRAC_ROOT}/ImageProcessing/build/pitrac_lm" --help >/dev/null 2>&1
}

main() {
    log_info "=== PiTrac Build Process ==="
    
    local missing_deps=()
    
    if ! need_cmd meson; then
        missing_deps+=("meson")
    fi
    
    if ! need_cmd ninja; then
        missing_deps+=("ninja-build")
    fi
    
    if ! need_cmd git; then
        missing_deps+=("git")
    fi
    
    if [ ${#missing_deps[@]} -gt 0 ]; then
        log_error "Missing required tools: ${missing_deps[*]}"
        log_info "Installing missing dependencies..."
        apt_ensure "${missing_deps[@]}"
    fi
    
    if ! get_pitrac_source; then
        log_error "Failed to get PiTrac source code"
        return 1
    fi
    
    setup_environment
    
    configure_libcamera_timeout
    copy_camera_configs
    
    if ! build_pitrac; then
        log_error "Build failed"
        return 1
    fi
    
    setup_gui
    
    log_success "=== PiTrac Build Complete ==="
    log_info ""
    log_info "Next steps:"
    if [ "$CONFIGURE_SHELL" = "1" ]; then
        log_info "1. Restart your shell or run: source ~/.bashrc"
    fi
    log_info "2. Test the launch monitor: $PITRAC_ROOT/ImageProcessing/build/pitrac_lm --help"
    log_info "3. Configure your cameras and start testing!"
    
    return 0
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi