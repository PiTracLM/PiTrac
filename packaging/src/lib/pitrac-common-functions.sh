#!/usr/bin/env bash
# PiTrac Common Functions

set -euo pipefail

# Color output helpers (used everywhere)
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
log_success() { echo -e "${GREEN}[✓]${NC} $*"; }

# Detect Raspberry Pi model
detect_pi_model() {
    if grep -q "Raspberry Pi.*5" /proc/cpuinfo 2>/dev/null; then
        echo "pi5"
    elif grep -q "Raspberry Pi.*4" /proc/cpuinfo 2>/dev/null; then
        echo "pi4"
    else
        echo "unknown"
    fi
}

# Apply Boost C++20 compatibility fix
apply_boost_cxx20_fix() {
    local boost_header="/usr/include/boost/asio/awaitable.hpp"
    
    if [[ -f "$boost_header" ]] && ! grep -q "#include <utility>" "$boost_header"; then
        log_info "Applying Boost 1.74 C++20 compatibility fix..."
        # Check if we need sudo
        if [[ -w "$boost_header" ]]; then
            sed -i '/namespace boost {/i #include <utility>' "$boost_header"
        else
            sudo sed -i '/namespace boost {/i #include <utility>' "$boost_header"
        fi
        log_success "Boost C++20 fix applied"
    fi
}

# Configure libcamera with extended timeout
configure_libcamera() {
    log_info "Configuring libcamera..."
    
    # Install IMX296 NOIR sensor file if available
    install_imx296_sensor_file_dev() {
        local pi_model=$(detect_pi_model)
        local source_file=""
        local dest_dir=""
        
        case "$pi_model" in
            "pi5")
                source_file="/usr/lib/pitrac/ImageProcessing/CameraTools/imx296_noir.json.PI_5_FOR_PISP_DIRECTORY"
                dest_dir="/usr/share/libcamera/ipa/rpi/pisp"
                ;;
            "pi4")
                source_file="/usr/lib/pitrac/ImageProcessing/CameraTools/imx296_noir.json.PI_4_FOR_VC4_DIRECTORY"
                dest_dir="/usr/share/libcamera/ipa/rpi/vc4"
                ;;
        esac
        
        if [[ -n "$source_file" && -f "$source_file" && -d "$dest_dir" ]]; then
            log_info "Installing IMX296 NOIR sensor configuration for $pi_model..."
            if [[ -w "$dest_dir" ]]; then
                cp "$source_file" "$dest_dir/imx296_noir.json"
                chmod 644 "$dest_dir/imx296_noir.json"
            else
                sudo cp "$source_file" "$dest_dir/imx296_noir.json"
                sudo chmod 644 "$dest_dir/imx296_noir.json"
            fi
            log_success "IMX296 NOIR sensor file installed"
        elif [[ -n "$source_file" ]]; then
            log_warn "IMX296 NOIR sensor file not found at $source_file"
            log_warn "This is only needed if using IMX296 NOIR cameras"
        fi
    }
    
    # Install sensor file first
    install_imx296_sensor_file_dev
    
    for pipeline in pisp vc4; do
        local config_dir="/usr/share/libcamera/pipeline/rpi/${pipeline}"
        local example_file="${config_dir}/example.yaml"
        local config_file="${config_dir}/rpi_apps.yaml"
        
        if [[ -d "$config_dir" ]]; then
            if [[ -f "$example_file" ]] && [[ ! -f "$config_file" ]]; then
                log_info "Creating ${pipeline} config from example..."
                # Check if we need sudo
                if [[ -w "$config_dir" ]]; then
                    cp "$example_file" "$config_file"
                    sed -i 's/# *"camera_timeout_value_ms": *[0-9]*/"camera_timeout_value_ms": 1000000/' "$config_file"
                else
                    sudo cp "$example_file" "$config_file"
                    sudo sed -i 's/# *"camera_timeout_value_ms": *[0-9]*/"camera_timeout_value_ms": 1000000/' "$config_file"
                fi
                log_success "Created ${config_file} with extended timeout"
            elif [[ -f "$config_file" ]]; then
                if grep -q '# *"camera_timeout_value_ms"' "$config_file"; then
                    log_info "Updating ${pipeline} camera timeout..."
                    if [[ -w "$config_file" ]]; then
                        sed -i 's/# *"camera_timeout_value_ms": *[0-9]*/"camera_timeout_value_ms": 1000000/' "$config_file"
                    else
                        sudo sed -i 's/# *"camera_timeout_value_ms": *[0-9]*/"camera_timeout_value_ms": 1000000/' "$config_file"
                    fi
                else
                    log_info "${pipeline} config already configured"
                fi
            fi
        fi
    done
    
    # Set up LIBCAMERA_RPI_CONFIG_FILE environment variable (CRITICAL for camera detection)
    setup_libcamera_environment
}

# Set up libcamera environment variable
setup_libcamera_environment() {
    local pi_model=$(detect_pi_model)
    local config_file=""
    
    case "$pi_model" in
        "pi5")
            config_file="/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml"
            ;;
        "pi4")
            config_file="/usr/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml"
            ;;
    esac
    
    if [[ -n "$config_file" && -f "$config_file" ]]; then
        log_info "Setting up libcamera environment for $pi_model..."
        
        # Set for current session
        export LIBCAMERA_RPI_CONFIG_FILE="$config_file"
        
        # Add to system environment (for services)
        local env_file="/etc/environment"
        if ! grep -q "LIBCAMERA_RPI_CONFIG_FILE" "$env_file" 2>/dev/null; then
            if [[ -w "$env_file" ]]; then
                echo "LIBCAMERA_RPI_CONFIG_FILE=\"$config_file\"" >> "$env_file"
            else
                echo "LIBCAMERA_RPI_CONFIG_FILE=\"$config_file\"" | sudo tee -a "$env_file" >/dev/null
            fi
            log_success "Added LIBCAMERA_RPI_CONFIG_FILE to system environment"
        fi
        
        log_success "libcamera environment configured"
    fi
}

# Create pkg-config files for libraries that don't have them
create_pkgconfig_files() {
    log_info "Creating pkg-config files..."
    
    # Check if we need sudo
    local need_sudo=""
    if [[ ! -w /usr/lib/pkgconfig ]] && [[ ! -w /usr/lib ]]; then
        need_sudo="sudo"
    fi
    
    $need_sudo mkdir -p /usr/lib/pkgconfig
    
    # Create lgpio.pc if it doesn't exist
    if [[ ! -f /usr/lib/pkgconfig/lgpio.pc ]]; then
        log_info "Creating lgpio.pc pkg-config file..."
        if [[ -n "$need_sudo" ]]; then
            sudo tee /usr/lib/pkgconfig/lgpio.pc > /dev/null << 'EOF'
prefix=/usr
exec_prefix=${prefix}
libdir=${exec_prefix}/lib/aarch64-linux-gnu
includedir=${prefix}/include

Name: lgpio
Description: GPIO library for Linux
Version: 0.2.2
Libs: -L${libdir} -llgpio
Cflags: -I${includedir}
EOF
        else
            cat > /usr/lib/pkgconfig/lgpio.pc << 'EOF'
prefix=/usr
exec_prefix=${prefix}
libdir=${exec_prefix}/lib/aarch64-linux-gnu
includedir=${prefix}/include

Name: lgpio
Description: GPIO library for Linux
Version: 0.2.2
Libs: -L${libdir} -llgpio
Cflags: -I${includedir}
EOF
        fi
        log_success "Created lgpio.pc"
    fi
    
    # Create msgpack-cxx.pc if it doesn't exist
    if [[ ! -f /usr/lib/pkgconfig/msgpack-cxx.pc ]]; then
        log_info "Creating msgpack-cxx.pc pkg-config file..."
        if [[ -n "$need_sudo" ]]; then
            sudo tee /usr/lib/pkgconfig/msgpack-cxx.pc > /dev/null << 'EOF'
prefix=/usr
exec_prefix=${prefix}
includedir=${prefix}/include

Name: msgpack-cxx
Description: MessagePack implementation for C++
Version: 4.1.3
Cflags: -I${includedir}
EOF
        else
            cat > /usr/lib/pkgconfig/msgpack-cxx.pc << 'EOF'
prefix=/usr
exec_prefix=${prefix}
includedir=${prefix}/include

Name: msgpack-cxx
Description: MessagePack implementation for C++
Version: 4.1.3
Cflags: -I${includedir}
EOF
        fi
        log_success "Created msgpack-cxx.pc"
    fi
}

# Get the actual user (not root) who is installing
get_install_user() {
    local user="${SUDO_USER:-$(whoami)}"
    
    # If we're root and there's no SUDO_USER, we can't determine the actual user
    if [[ "$user" == "root" ]] && [[ -z "${SUDO_USER:-}" ]]; then
        echo ""
        return 1
    fi
    
    echo "$user"
    return 0
}

install_test_images() {
    local dest_dir="${1:-/usr/share/pitrac/test-images}"
    local repo_root="${2:-${REPO_ROOT:-/opt/PiTrac}}"

    log_info "Installing test images..."

    local test_images_dir="$repo_root/Software/LMSourceCode/Images"
    if [[ -d "$test_images_dir" ]]; then
        mkdir -p "$dest_dir"

        if [[ -f "$test_images_dir/gs_log_img__log_ball_final_found_ball_img.png" ]]; then
            cp "$test_images_dir/gs_log_img__log_ball_final_found_ball_img.png" \
               "$dest_dir/teed-ball.png"
        fi
        if [[ -f "$test_images_dir/log_cam2_last_strobed_img.png" ]]; then
            cp "$test_images_dir/log_cam2_last_strobed_img.png" \
               "$dest_dir/strobed.png"
        fi

        for img in "$test_images_dir"/*.png "$test_images_dir"/*.jpg "$test_images_dir"/*.jpeg; do
            if [[ -f "$img" ]]; then
                local basename=$(basename "$img")
                if [[ "$basename" != "gs_log_img__log_ball_final_found_ball_img.png" ]] && \
                   [[ "$basename" != "log_cam2_last_strobed_img.png" ]]; then
                    cp "$img" "$dest_dir/"
                fi
            fi
        done

        log_success "Test images installed"
    else
        log_warn "Test images directory not found: $test_images_dir"
    fi
}

install_test_suites() {
    local dest_dir="${1:-/usr/share/pitrac/test-suites}"
    local repo_root="${2:-${REPO_ROOT:-/opt/PiTrac}}"

    log_info "Installing test suites for automated testing..."

    local testing_dir="$repo_root/Software/LMSourceCode/Testing"
    if [[ -d "$testing_dir" ]]; then
        mkdir -p "$dest_dir"

        if [[ -d "$testing_dir/TestSuite_2025_02_07" ]]; then
            log_info "  Copying TestSuite_2025_02_07..."
            cp -r "$testing_dir/TestSuite_2025_02_07" "$dest_dir/"
            log_success "  TestSuite_2025_02_07 installed"
        fi

        if [[ -d "$testing_dir/Left-Handed-Shots" ]]; then
            log_info "  Copying Left-Handed-Shots test suite..."
            cp -r "$testing_dir/Left-Handed-Shots" "$dest_dir/"
            log_success "  Left-Handed-Shots test suite installed"
        fi

        log_success "Test suites installed to $dest_dir"
    else
        log_warn "Testing directory not found: $testing_dir"
    fi
}

install_onnx_models() {
    local repo_root="${1:-${REPO_ROOT:-/opt/PiTrac}}"
    local install_user="${2:-${SUDO_USER:-$(whoami)}}"

    log_info "Installing ONNX models for AI detection..."

    local models_dir="$repo_root/Software/LMSourceCode/ml_models"
    if [[ -d "$models_dir" ]]; then
        # Install to system location /etc/pitrac/models/
        local system_models_dir="/etc/pitrac/models"

        # Remove old models to prevent accumulation of outdated models
        if [[ -d "$system_models_dir" ]]; then
            log_info "Cleaning up old models in $system_models_dir..."
            rm -rf "$system_models_dir"/*
        fi

        mkdir -p "$system_models_dir"

        local models_found=0
        for model_path in "$models_dir"/*/weights/best.onnx; do
            if [[ -f "$model_path" ]]; then
                # Extract model name from path (e.g., pitrac-ball-detection-09-23-25)
                local model_name=$(basename "$(dirname "$(dirname "$model_path")")")

                # Create folder and copy model
                mkdir -p "$system_models_dir/$model_name"
                cp "$model_path" "$system_models_dir/$model_name/best.onnx"
                log_info "  Installed model: $model_name/best.onnx"
                models_found=$((models_found + 1))
            fi
        done

        if [[ $models_found -gt 0 ]]; then
            # Set proper permissions - models should be readable by all users
            chmod -R 644 "$system_models_dir"/*/*.onnx 2>/dev/null || true
            chmod -R 755 "$system_models_dir"/* 2>/dev/null || true
            chmod 755 "$system_models_dir"
            log_success "Installed $models_found ONNX model(s) to $system_models_dir"
        else
            log_warn "No ONNX models found in $models_dir"
        fi
    else
        log_warn "ONNX models directory not found: $models_dir"
    fi
}

install_camera_tools() {
    local dest_dir="${1:-/usr/lib/pitrac}"
    local repo_root="${2:-${REPO_ROOT:-/opt/PiTrac}}"
    
    log_info "Installing camera tools..."
    
    local camera_tools_dir="$repo_root/Software/LMSourceCode/ImageProcessing/CameraTools"
    local imaging_dir="$repo_root/Software/LMSourceCode/ImageProcessing"
    
    if [[ -d "$camera_tools_dir" ]]; then
        mkdir -p "$dest_dir/ImageProcessing/CameraTools"
        cp -r "$camera_tools_dir"/* "$dest_dir/ImageProcessing/CameraTools/"
        
        # Install Pi-specific IMX296 NOIR sensor files
        for sensor_file in "imx296_noir.json.PI_4_FOR_VC4_DIRECTORY" "imx296_noir.json.PI_5_FOR_PISP_DIRECTORY"; do
            if [[ -f "$imaging_dir/$sensor_file" ]]; then
                cp "$imaging_dir/$sensor_file" "$dest_dir/ImageProcessing/CameraTools/"
                chmod 644 "$dest_dir/ImageProcessing/CameraTools/$sensor_file"
                log_info "Installed sensor file: $sensor_file"
            fi
        done
        
        find "$dest_dir/ImageProcessing/CameraTools" -name "*.sh" -type f -exec chmod 755 {} \;
        if [[ -f "$dest_dir/ImageProcessing/CameraTools/imx296_trigger" ]]; then
            chmod 755 "$dest_dir/ImageProcessing/CameraTools/imx296_trigger"
        fi
        
        log_success "Camera tools installed"
    else
        log_warn "Camera tools not found: $camera_tools_dir"
    fi
}

extract_dependency() {
    local tar_file="$1"
    local lib_name="$2"
    local dest_dir="${3:-/usr/lib/pitrac}"
    
    if [[ ! -f "$tar_file" ]]; then
        log_warn "Dependency archive not found: $tar_file"
        return 1
    fi
    
    log_info "  Extracting $lib_name..."
    tar xzf "$tar_file" -C /tmp/
    
    local extracted_dir="/tmp/${lib_name}"
    if [[ ! -d "$extracted_dir" ]]; then
        extracted_dir="/tmp/${lib_name%%-*}"
    fi
    
    if [[ -d "$extracted_dir/lib" ]]; then
        cp -r "$extracted_dir"/lib/*.so* "$dest_dir/" 2>/dev/null || true
    fi
    
    rm -rf "$extracted_dir"
}

create_pitrac_directories() {
    log_info "Creating PiTrac directories..."
    
    mkdir -p /usr/lib/pitrac
    mkdir -p /usr/share/pitrac/{templates,test-images,test-suites,calibration,webapp}
    mkdir -p /var/lib/pitrac
    mkdir -p /etc/pitrac
    
    if local user=$(get_install_user); then
        local user_home=$(eval echo "~$user")
        mkdir -p "$user_home/.pitrac/config"
        mkdir -p "$user_home/.pitrac/state"
        mkdir -p "$user_home/LM_Shares/Images"
        mkdir -p "$user_home/LM_Shares/WebShare"
        
        chown -R "$user:$user" "$user_home/.pitrac"
        chown -R "$user:$user" "$user_home/LM_Shares"
    fi
    
    log_success "Directories created"
}

manage_service_restart() {
    local service_name="$1"
    local action_func="${2:-true}"
    
    log_info "Managing $service_name service..."
    
    if systemctl is-active --quiet "$service_name" 2>/dev/null; then
        log_info "Stopping $service_name..."
        systemctl stop "$service_name"
        
        if [[ -f "/etc/init.d/$service_name" ]]; then
            sleep 3
        else
            sleep 2
        fi
    fi
    
    if [[ "$action_func" != "true" ]] && type -t "$action_func" &>/dev/null; then
        $action_func
    fi
    
    log_info "Starting $service_name..."
    systemctl start "$service_name" || {
        log_warn "Failed to start $service_name"
        return 1
    }
    
    sleep 2
    if systemctl is-active --quiet "$service_name"; then
        log_success "$service_name started successfully"
    else
        log_warn "$service_name may not have started correctly"
    fi
}

set_config_permissions() {
    local file="$1"
    local owner="${2:-root:root}"
    local perms="${3:-644}"
    
    if [[ -f "$file" ]]; then
        chown "$owner" "$file"
        chmod "$perms" "$file"
        log_info "Set permissions for $(basename "$file"): $owner $perms"
    fi
}

install_deb_dependency() {
    local deb_file="$1"
    local package_name="$2"
    local skip_if_installed="${3:-false}"

    if [[ ! -f "$deb_file" ]]; then
        log_warn "DEB package not found: $deb_file"
        return 1
    fi

    # Check if package or similar is already installed
    if [[ "$skip_if_installed" == "true" ]]; then
        # For lgpio, check if system package is already installed
        if [[ "$package_name" == "liblgpio1" ]] && dpkg -l | grep -qE "^ii\s+(liblgpio1|liblgpio-dev)"; then
            log_info "  System lgpio packages already installed, skipping custom deb"
            return 0
        fi
    fi

    log_info "  Installing $package_name from deb package..."

    # Use dpkg to install the package, force overwrite if needed for our packages
    if dpkg -i "$deb_file" 2>/dev/null; then
        log_success "  $package_name installed successfully"
    else
        # Try to fix dependencies if installation fails
        log_warn "  Attempting to fix dependencies for $package_name..."
        apt-get -f install -y

        # For lgpio conflicts, skip if system version exists
        if [[ "$package_name" == "liblgpio1" ]] && dpkg -l | grep -qE "^ii\s+(liblgpio1|liblgpio-dev)"; then
            log_warn "  Using system-installed lgpio instead of custom package"
            return 0
        fi

        # Try again with force for other packages
        dpkg -i "$deb_file" || {
            log_error "  Failed to install $package_name"
            return 1
        }
    fi
}

extract_all_dependencies() {
    local artifacts_dir="${1:-/opt/PiTrac/packaging/deps-artifacts}"
    local dest_dir="${2:-/usr/lib/pitrac}"
    local use_debs="${USE_DEB_PACKAGES:-true}"

    log_info "Installing all dependencies..."

    if [[ "$use_debs" == "true" ]]; then
        # Check if deb packages exist
        if [[ -f "$artifacts_dir/libopencv4.11_4.11.0-1_arm64.deb" ]]; then
            log_info "Using DEB packages for dependency installation..."

            # Check if system lgpio is already installed
            if dpkg -l | grep -qE "^ii\s+(liblgpio1|liblgpio-dev)"; then
                log_info "System lgpio packages detected - will use those instead of custom deb"
            fi

            # Install runtime packages first (order matters for dependencies)
            # Skip lgpio if system version is installed
            install_deb_dependency "$artifacts_dir/liblgpio1_0.2.2-1_arm64.deb" "liblgpio1" "true"
            install_deb_dependency "$artifacts_dir/libactivemq-cpp_3.9.5-1_arm64.deb" "libactivemq-cpp"
            install_deb_dependency "$artifacts_dir/libopencv4.11_4.11.0-1_arm64.deb" "libopencv4.11"
            install_deb_dependency "$artifacts_dir/libonnxruntime1.17.3_1.17.3-xnnpack-verified_arm64.deb" "libonnxruntime1.17.3"

            # Install development packages (these depend on runtime packages)
            install_deb_dependency "$artifacts_dir/libactivemq-cpp-dev_3.9.5-1_arm64.deb" "libactivemq-cpp-dev"
            install_deb_dependency "$artifacts_dir/libopencv-dev_4.11.0-1_arm64.deb" "libopencv-dev"

            # msgpack is header-only, check if not already installed
            if ! dpkg -l | grep -qE "^ii\s+libmsgpack-cxx-dev"; then
                install_deb_dependency "$artifacts_dir/libmsgpack-cxx-dev_6.1.1-1_all.deb" "libmsgpack-cxx-dev"
            else
                log_info "System msgpack-cxx-dev already installed, skipping custom deb"
            fi

            log_success "All DEB packages installed"
        elif [[ -f "$artifacts_dir/opencv-4.11.0-arm64.tar.gz" ]]; then
            log_info "DEB packages not found, falling back to tar.gz extraction..."
            use_debs="false"
        else
            log_error "No dependency packages found (neither .deb nor .tar.gz)"
            return 1
        fi
    fi

    # Fallback to tar.gz extraction if DEBs not available or disabled
    if [[ "$use_debs" == "false" ]]; then
        log_info "Using tar.gz archives for dependency installation..."
        extract_dependency "$artifacts_dir/opencv-4.11.0-arm64.tar.gz" "opencv" "$dest_dir"
        extract_dependency "$artifacts_dir/activemq-cpp-3.9.5-arm64.tar.gz" "activemq-cpp" "$dest_dir"
        extract_dependency "$artifacts_dir/lgpio-0.2.2-arm64.tar.gz" "lgpio" "$dest_dir"
        extract_dependency "$artifacts_dir/msgpack-cxx-6.1.1-arm64.tar.gz" "msgpack" "$dest_dir"
    fi

    log_success "All dependencies installed"
}

install_python_dependencies() {
    local web_server_dir="${1:-/usr/lib/pitrac/web-server}"
    
    if [[ ! -d "$web_server_dir" ]]; then
        log_warn "Web server directory not found: $web_server_dir"
        return 1
    fi
    
    if [[ ! -f "$web_server_dir/requirements.txt" ]]; then
        log_warn "Requirements file not found: $web_server_dir/requirements.txt"
        return 1
    fi
    
    log_info "Installing Python dependencies for web server..."
    
    if [[ $EUID -eq 0 ]]; then
        if pip3 install -r "$web_server_dir/requirements.txt" --break-system-packages 2>/dev/null; then
            log_success "Python dependencies installed successfully"
        elif pip3 install -r "$web_server_dir/requirements.txt" 2>/dev/null; then
            log_success "Python dependencies installed successfully"
        else
            log_error "Failed to install Python dependencies"
            log_info "Try manually: pip3 install -r $web_server_dir/requirements.txt --break-system-packages"
            return 1
        fi
    else
        if sudo pip3 install -r "$web_server_dir/requirements.txt" --break-system-packages 2>/dev/null; then
            log_success "Python dependencies installed successfully"
        elif sudo pip3 install -r "$web_server_dir/requirements.txt" 2>/dev/null; then
            log_success "Python dependencies installed successfully"
        else
            log_error "Failed to install Python dependencies"
            log_info "Try manually: sudo pip3 install -r $web_server_dir/requirements.txt --break-system-packages"
            return 1
        fi
    fi
    
    return 0
}

install_service_from_template() {
    local service_name="$1"
    local install_user="$2"
    local template_file="${3:-/usr/share/pitrac/templates/${service_name}.service.template}"
    local target_file="/etc/systemd/system/${service_name}.service"
    
    if [[ -z "$service_name" ]]; then
        log_error "Service name is required"
        return 1
    fi
    
    if [[ "$install_user" == "root" ]]; then
        if [[ -n "${SUDO_USER:-}" ]] && [[ "$SUDO_USER" != "root" ]]; then
            install_user="$SUDO_USER"
            log_info "Installing $service_name service for actual user: $install_user"
        else
            log_error "Cannot install service for root user"
            log_info "Please specify a non-root user"
            return 1
        fi
    fi
    
    if ! id "$install_user" &>/dev/null; then
        log_error "User '$install_user' does not exist"
        return 1
    fi
    
    if [[ ! "$install_user" =~ ^[a-zA-Z0-9_-]+$ ]]; then
        log_error "Username contains invalid characters"
        return 1
    fi
    
    local install_group
    install_group=$(id -gn "$install_user")
    
    local user_home
    user_home=$(getent passwd "$install_user" | cut -d: -f6)
    
    if [[ -z "$user_home" ]]; then
        log_error "Could not determine home directory for user '$install_user'"
        return 1
    fi
    
    if [[ ! "$user_home" =~ ^/.+ ]]; then
        log_error "Invalid home directory path: $user_home"
        return 1
    fi
    
    log_info "Installing $service_name service:"
    log_info "  User: $install_user"
    log_info "  Group: $install_group" 
    log_info "  Home: $user_home"
    
    if [[ ! -f "$template_file" ]]; then
        log_error "Service template not found: $template_file"
        return 1
    fi
    
    local temp_service
    temp_service=$(mktemp "/tmp/${service_name}.service.XXXXXX") || {
        log_error "Failed to create temp file"
        return 1
    }
    
    trap 'rm -f '"$temp_service"'' RETURN INT TERM
    
    local escaped_user
    local escaped_group
    local escaped_home
    escaped_user=$(printf '%s' "$install_user" | sed 's/[[\.*^$()+?{|]/\\&/g')
    escaped_group=$(printf '%s' "$install_group" | sed 's/[[\.*^$()+?{|]/\\&/g')
    escaped_home=$(printf '%s' "$user_home" | sed 's/[[\.*^$()+?{|]/\\&/g')
    
    sed -e "s|@PITRAC_USER@|$escaped_user|g" \
        -e "s|@PITRAC_GROUP@|$escaped_group|g" \
        -e "s|@PITRAC_HOME@|$escaped_home|g" \
        "$template_file" > "$temp_service"
    
    if [[ -f "$target_file" ]]; then
        local backup_file="${target_file}.bak.$(date +%s)"
        log_info "Backing up existing service to $backup_file"
        if [[ -w "$target_file" ]]; then
            cp "$target_file" "$backup_file"
        else
            sudo cp "$target_file" "$backup_file"
        fi
    fi
    
    log_info "Installing service file to $target_file"
    if [[ -w "/etc/systemd/system" ]]; then
        install -m 644 "$temp_service" "$target_file"
    else
        sudo install -m 644 "$temp_service" "$target_file"
    fi
    
    if [[ -w "/etc/systemd/system" ]]; then
        find /etc/systemd/system -name "${service_name}.service.bak.*" -type f 2>/dev/null | \
            sort -r | tail -n +4 | xargs -r rm -f
    else
        sudo find /etc/systemd/system -name "${service_name}.service.bak.*" -type f 2>/dev/null | \
            sort -r | tail -n +4 | xargs -r sudo rm -f
    fi
    
    log_info "Reloading systemd daemon..."
    if command -v systemctl &>/dev/null; then
        if [[ $EUID -eq 0 ]]; then
            systemctl daemon-reload
        else
            sudo systemctl daemon-reload
        fi
    fi
    
    log_info "Enabling $service_name service..."
    if [[ $EUID -eq 0 ]]; then
        systemctl enable "${service_name}.service" 2>/dev/null || true
    else
        sudo systemctl enable "${service_name}.service" 2>/dev/null || true
    fi
    
    log_success "$service_name service installed successfully!"
    
    return 0
}