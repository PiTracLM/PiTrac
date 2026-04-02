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
                    sed -i 's/# *"camera_timeout_value_ms": *[0-9][0-9]*/"camera_timeout_value_ms": 86400000/' "$config_file"
                else
                    sudo cp "$example_file" "$config_file"
                    sudo sed -i 's/# *"camera_timeout_value_ms": *[0-9][0-9]*/"camera_timeout_value_ms": 86400000/' "$config_file"
                fi
                log_success "Created ${config_file} with extended timeout"
            elif [[ -f "$config_file" ]]; then
                if ! grep -q '"camera_timeout_value_ms": *86400000' "$config_file"; then
                    log_info "Updating ${pipeline} camera timeout to 86400000ms..."
                    if [[ -w "$config_file" ]]; then
                        sed -i 's/# *"camera_timeout_value_ms": *[0-9][0-9]*/"camera_timeout_value_ms": 86400000/' "$config_file"
                        sed -i 's/"camera_timeout_value_ms": *[0-9][0-9]*/"camera_timeout_value_ms": 86400000/' "$config_file"
                    else
                        sudo sed -i 's/# *"camera_timeout_value_ms": *[0-9][0-9]*/"camera_timeout_value_ms": 86400000/' "$config_file"
                        sudo sed -i 's/"camera_timeout_value_ms": *[0-9][0-9]*/"camera_timeout_value_ms": 86400000/' "$config_file"
                    fi
                    log_success "Updated ${pipeline} camera timeout"
                else
                    log_info "${pipeline} camera timeout already correct"
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

        # Also install ncnn models (param + bin files directly in model dirs)
        for param_file in "$models_dir"/*/best.ncnn.param; do
            if [[ -f "$param_file" ]]; then
                local model_name=$(basename "$(dirname "$param_file")")
                local bin_file="$(dirname "$param_file")/best.ncnn.bin"
                mkdir -p "$system_models_dir/$model_name"
                cp "$param_file" "$system_models_dir/$model_name/"
                [[ -f "$bin_file" ]] && cp "$bin_file" "$system_models_dir/$model_name/"
                log_info "  Installed model: $model_name/best.ncnn.{param,bin}"
                models_found=$((models_found + 1))
            fi
        done

        if [[ $models_found -gt 0 ]]; then
            # Set proper permissions - models should be readable by all users
            chmod -R a+r "$system_models_dir" 2>/dev/null || true
            find "$system_models_dir" -type d -exec chmod 755 {} \; 2>/dev/null || true
            log_success "Installed $models_found model(s) to $system_models_dir"
        else
            log_warn "No models found in $models_dir"
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

    # gpiozero (in requirements.txt) needs these system GPIO backends on Raspberry Pi OS.
    # They're not available on PyPI so we install them via apt.
    for pkg in python3-lgpio python3-rpi-lgpio; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            log_info "Installing system GPIO backend: $pkg"
            INITRD=No apt-get install -y "$pkg" 2>/dev/null || log_warn "Could not install $pkg"
        fi
    done

    if [[ $EUID -eq 0 ]]; then
        if pip3 install -r "$web_server_dir/requirements.txt" --break-system-packages --ignore-installed 2>/dev/null; then
            log_success "Python dependencies installed successfully"
        elif pip3 install -r "$web_server_dir/requirements.txt" --ignore-installed 2>/dev/null; then
            log_success "Python dependencies installed successfully"
        else
            log_error "Failed to install Python dependencies"
            log_info "Try manually: pip3 install -r $web_server_dir/requirements.txt --break-system-packages --ignore-installed"
            return 1
        fi
    else
        if sudo pip3 install -r "$web_server_dir/requirements.txt" --break-system-packages --ignore-installed 2>/dev/null; then
            log_success "Python dependencies installed successfully"
        elif sudo pip3 install -r "$web_server_dir/requirements.txt" --ignore-installed 2>/dev/null; then
            log_success "Python dependencies installed successfully"
        else
            log_error "Failed to install Python dependencies"
            log_info "Try manually: sudo pip3 install -r $web_server_dir/requirements.txt --break-system-packages --ignore-installed"
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

# ========================================================================
# Detect Debian distribution codename
# ========================================================================
# Returns: bookworm, trixie, or unknown
# Used for: Multi-distribution APT repository support
# ========================================================================
detect_debian_codename() {
    local codename="unknown"

    # Try lsb_release first (most reliable)
    if command -v lsb_release &> /dev/null; then
        codename=$(lsb_release -cs 2>/dev/null)
    fi

    # Fallback to /etc/os-release
    if [[ "$codename" == "unknown" ]] && [[ -f /etc/os-release ]]; then
        codename=$(grep "^VERSION_CODENAME=" /etc/os-release | cut -d'=' -f2)
    fi

    # Fallback to /etc/debian_version (numeric to codename mapping)
    if [[ "$codename" == "unknown" ]] && [[ -f /etc/debian_version ]]; then
        local version=$(cat /etc/debian_version)
        case "$version" in
            12.*) codename="bookworm" ;;
            13.*) codename="trixie" ;;
        esac
    fi

    echo "$codename"
}

# ========================================================================
# Configure PiTrac APT Repository
# ========================================================================
# Adds the PiTrac APT repository with GPG key and distribution detection
# Returns: 0 on success, 1 if repo unavailable
# ========================================================================
configure_pitrac_apt_repo() {
    local repo_url="https://pitraclm.github.io/packages"
    local key_url="$repo_url/pitrac-repo.asc"
    local sources_file="/etc/apt/sources.list.d/pitrac.list"
    local keyring_file="/usr/share/keyrings/pitrac.gpg"

    log_info "Configuring PiTrac APT repository..."

    # Detect Debian codename
    local codename=$(detect_debian_codename)

    if [[ "$codename" != "bookworm" ]] && [[ "$codename" != "trixie" ]]; then
        log_warn "Unsupported Debian version: $codename"
        log_info "Supported: bookworm (12.x), trixie (13.x)"
        return 1
    fi

    log_info "Detected Debian $codename"

    # Check if repository is accessible
    if ! curl --head --silent --fail "$repo_url/dists/$codename/Release" > /dev/null 2>&1; then
        log_warn "PiTrac APT repository not accessible at $repo_url"
        log_info "Check network connectivity and try again"
        return 1
    fi

    # Download and install GPG key
    log_info "Installing repository GPG key..."
    # Remove existing key file to avoid overwrite prompts
    rm -f "$keyring_file"
    if ! curl -fsSL "$key_url" | gpg --dearmor -o "$keyring_file" 2>/dev/null; then
        log_error "Failed to download repository GPG key"
        return 1
    fi

    # Create APT sources list entry
    log_info "Adding repository to APT sources..."
    echo "deb [arch=arm64 signed-by=$keyring_file] $repo_url $codename main" > "$sources_file"

    # Update APT cache
    log_info "Updating APT package index..."
    if ! apt-get update 2>&1 | grep -v "^Ign:" | grep -v "^Get:" | grep -v "^Hit:"; then
        log_error "Failed to update APT cache"
        rm -f "$sources_file" "$keyring_file"
        return 1
    fi

    log_success "PiTrac APT repository configured for Debian $codename"
    return 0
}

# ========================================================================
# Install Dependencies from APT Repository
# ========================================================================
# Installs PiTrac dependencies from the configured APT repo
# Returns: 0 on success, 1 on failure
# ========================================================================
install_dependencies_from_apt() {
    log_info "Installing PiTrac dependencies from APT repository..."

    # ========================================================================
    # PiTrac custom dependency packages (from pitraclm.github.io/packages)
    # ========================================================================
    # lgpio is NOT here — system liblgpio1 is used instead.
    # python3-lgpio/python3-rpi-lgpio depend on the RPi Foundation version
    # and break if a custom build with a different version string is installed.
    # liblgpio-dev is in the system deps block in build.sh.
    # ========================================================================
    local packages=(
        "libmsgpack-cxx-dev"      # MessagePack C++ (header-only)
        "libactivemq-cpp"         # ActiveMQ C++ client runtime
        "libactivemq-cpp-dev"     # ActiveMQ C++ client headers
        "libopencv4.13"           # OpenCV runtime (Pi5-optimized build)
        "libopencv-dev"           # OpenCV development headers
        "libncnn-dev"             # ncnn inference framework (static lib + headers)
        "libonnxruntime1.17.3"    # ONNX Runtime with XNNPACK (1.22.x has Pi5 issues)
    )

    # Check which packages are available
    log_info "Verifying package availability..."
    local available_packages=()
    local missing_packages=()

    for pkg in "${packages[@]}"; do
        if apt-cache show "$pkg" &>/dev/null; then
            available_packages+=("$pkg")
        else
            missing_packages+=("$pkg")
        fi
    done

    if [ ${#missing_packages[@]} -gt 0 ]; then
        log_warn "Some packages not available in APT: ${missing_packages[*]}"
    fi

    if [ ${#available_packages[@]} -eq 0 ]; then
        log_error "No packages available from APT repository"
        return 1
    fi

    # Install available packages
    log_info "Installing packages: ${available_packages[*]}"
    if ! INITRD=No apt-get install -y "${available_packages[@]}"; then
        log_error "Failed to install packages from APT repository"
        return 1
    fi

    log_success "Installed ${#available_packages[@]} packages from APT repository"

    # Update library cache
    ldconfig

    return 0
}