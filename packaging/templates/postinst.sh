#!/bin/bash
set -e

# Function to detect Pi model
detect_pi_model() {
    if grep -q "Raspberry Pi.*5" /proc/cpuinfo 2>/dev/null; then
        echo "pi5"
    elif grep -q "Raspberry Pi.*4" /proc/cpuinfo 2>/dev/null; then
        echo "pi4"
    else
        echo "unknown"
    fi
}

case "$1" in
    configure)
        echo "Configuring PiTrac..."

        # Detect Pi model
        PI_MODEL=$(detect_pi_model)
        echo "Detected Pi model: $PI_MODEL"


        # Get the actual user who invoked sudo (if any)
        ACTUAL_USER="${SUDO_USER:-}"
        if [ -z "$ACTUAL_USER" ]; then
            echo "Note: Run 'pitrac setup' as your regular user to complete setup"
        else
            # Add user to required groups
            usermod -a -G video,gpio,i2c,spi,dialout "$ACTUAL_USER" 2>/dev/null || true
            
            if [ -f /usr/lib/pitrac/pitrac-common-functions.sh ]; then
                . /usr/lib/pitrac/pitrac-common-functions.sh
                if [ -d /usr/share/pitrac/models ]; then
                    USER_MODELS_DIR="/home/$ACTUAL_USER/LM_Shares/models"
                    mkdir -p "$USER_MODELS_DIR"
                    cp -r /usr/share/pitrac/models/* "$USER_MODELS_DIR/" 2>/dev/null || true
                    chown -R "$ACTUAL_USER:$ACTUAL_USER" "/home/$ACTUAL_USER/LM_Shares"
                    echo "Installed ONNX models to $USER_MODELS_DIR"
                fi
            fi

            # Create user directories
            USER_HOME=$(getent passwd "$ACTUAL_USER" | cut -d: -f6)
            if [ -n "$USER_HOME" ] && [ -d "$USER_HOME" ]; then
                mkdir -p "$USER_HOME/.pitrac"/{config,cache,state,calibration}
                mkdir -p "$USER_HOME/LM_Shares"/{Images,WebShare}
                chown -R "$ACTUAL_USER:$ACTUAL_USER" "$USER_HOME/.pitrac" "$USER_HOME/LM_Shares"

                # Copy default config to user directory if it doesn't exist
                if [ ! -f "$USER_HOME/.pitrac/config/pitrac.yaml" ]; then
                    cp /etc/pitrac/pitrac.yaml "$USER_HOME/.pitrac/config/pitrac.yaml"
                    chown "$ACTUAL_USER:$ACTUAL_USER" "$USER_HOME/.pitrac/config/pitrac.yaml"
                fi
            fi
        fi

        if type -t set_config_permissions &>/dev/null; then
            chown root:root /etc/pitrac
            chmod 755 /etc/pitrac
            set_config_permissions "/etc/pitrac/pitrac.yaml"
            set_config_permissions "/etc/pitrac/golf_sim_config.json"
            
            if [ -d /etc/pitrac/config ]; then
                chown -R root:root /etc/pitrac/config
                chmod 755 /etc/pitrac/config
                find /etc/pitrac/config -type f -exec chmod 644 {} \;
            fi
        else
            chown root:root /etc/pitrac
            chmod 755 /etc/pitrac
            chown root:root /etc/pitrac/pitrac.yaml
            chmod 644 /etc/pitrac/pitrac.yaml
            if [ -f /etc/pitrac/golf_sim_config.json ]; then
                chown root:root /etc/pitrac/golf_sim_config.json
                chmod 644 /etc/pitrac/golf_sim_config.json
            fi
            
            if [ -d /etc/pitrac/config ]; then
                chown -R root:root /etc/pitrac/config
                chmod 755 /etc/pitrac/config
                find /etc/pitrac/config -type f -exec chmod 644 {} \;
            fi
        fi


        if [ -d /usr/lib/pitrac/web-server ]; then
            if [ -f /usr/lib/pitrac/pitrac-common-functions.sh ]; then
                . /usr/lib/pitrac/pitrac-common-functions.sh
                install_python_dependencies "/usr/lib/pitrac/web-server"
            else
                echo "Installing Python web server dependencies..."
                pip3 install -r /usr/lib/pitrac/web-server/requirements.txt --break-system-packages 2>/dev/null || \
                pip3 install -r /usr/lib/pitrac/web-server/requirements.txt || true
            fi
            
            if [ -x /usr/lib/pitrac/web-service-install.sh ] && [ -n "$ACTUAL_USER" ]; then
                echo "Installing PiTrac web service for user: $ACTUAL_USER"
                /usr/lib/pitrac/web-service-install.sh install "$ACTUAL_USER" || true
            elif [ -x /usr/lib/pitrac/web-service-install.sh ]; then
                echo "Note: Web service not installed. Run '/usr/lib/pitrac/web-service-install.sh install <username>' as a regular user"
            fi
        fi

        # Apply Boost C++20 fix
        if [ -f /usr/include/boost/asio/awaitable.hpp ] && ! grep -q "#include <utility>" /usr/include/boost/asio/awaitable.hpp; then
            echo "Applying Boost 1.74 C++20 compatibility fix..."
            sed -i '/namespace boost {/i #include <utility>' /usr/include/boost/asio/awaitable.hpp
        fi

        # Configure boot settings - Bookworm uses /boot/firmware for both Pi 4 and Pi 5
        if [ -f "/boot/firmware/config.txt" ]; then
            CONFIG_FILE="/boot/firmware/config.txt"
        elif [ -f "/boot/config.txt" ] && ! grep -q "DO NOT EDIT THIS FILE" /boot/config.txt 2>/dev/null; then
            CONFIG_FILE="/boot/config.txt"
        else
            # Default to new location
            CONFIG_FILE="/boot/firmware/config.txt"
        fi

        if [ -f "$CONFIG_FILE" ]; then
            echo "Configuring boot settings in $CONFIG_FILE..."

            # Add settings if not present
            grep -q "camera_auto_detect=1" "$CONFIG_FILE" || echo "camera_auto_detect=1" >> "$CONFIG_FILE"
            grep -q "dtparam=i2c_arm=on" "$CONFIG_FILE" || echo "dtparam=i2c_arm=on" >> "$CONFIG_FILE"
            grep -q "dtparam=spi=on" "$CONFIG_FILE" || echo "dtparam=spi=on" >> "$CONFIG_FILE"
            grep -q "force_turbo=1" "$CONFIG_FILE" || echo "force_turbo=1" >> "$CONFIG_FILE"

            if [ "$PI_MODEL" = "pi5" ]; then
                grep -q "arm_boost=1" "$CONFIG_FILE" || echo "arm_boost=1" >> "$CONFIG_FILE"
            else
                grep -q "gpu_mem=256" "$CONFIG_FILE" || echo "gpu_mem=256" >> "$CONFIG_FILE"
            fi
        fi

        # Configure libcamera settings
        echo "Configuring libcamera..."

        # Install IMX296 NOIR sensor file if available
        install_imx296_sensor_file() {
            local pi_model="$1"
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
            
            if [ -n "$source_file" ] && [ -f "$source_file" ] && [ -d "$dest_dir" ]; then
                echo "Installing IMX296 NOIR sensor configuration for $pi_model..."
                cp "$source_file" "$dest_dir/imx296_noir.json"
                chmod 644 "$dest_dir/imx296_noir.json"
                echo "IMX296 NOIR sensor file installed"
            elif [ -n "$source_file" ]; then
                echo "Note: IMX296 NOIR sensor file not found at $source_file"
                echo "This is only needed if using IMX296 NOIR cameras"
            fi
        }
        
        # Install sensor file for detected Pi model
        install_imx296_sensor_file "$PI_MODEL"

        # Use existing example.yaml files as base for configuration
        # Both Pi 4 (vc4) and Pi 5 (pisp) ship with example.yaml
        for pipeline in pisp vc4; do
            CAMERA_DIR="/usr/share/libcamera/pipeline/rpi/${pipeline}"
            EXAMPLE_FILE="${CAMERA_DIR}/example.yaml"
            CAMERA_CONFIG="${CAMERA_DIR}/rpi_apps.yaml"

            # Only proceed if the pipeline directory exists (Pi 4 has vc4, Pi 5 has pisp)
            if [ -d "$CAMERA_DIR" ]; then
                # If example exists but rpi_apps doesn't, copy and configure
                if [ -f "$EXAMPLE_FILE" ] && [ ! -f "$CAMERA_CONFIG" ]; then
                    echo "Creating ${pipeline} config from example..."
                    cp "$EXAMPLE_FILE" "$CAMERA_CONFIG"
                    # Uncomment and set the camera timeout to 1 second (1000000 ms)
                    sed -i 's/# *"camera_timeout_value_ms": *[0-9]*/"camera_timeout_value_ms": 1000000/' "$CAMERA_CONFIG"
                elif [ -f "$CAMERA_CONFIG" ]; then
                    # Config exists, check if timeout needs updating
                    if grep -q '# *"camera_timeout_value_ms"' "$CAMERA_CONFIG"; then
                        echo "Updating ${pipeline} camera timeout..."
                        sed -i 's/# *"camera_timeout_value_ms": *[0-9]*/"camera_timeout_value_ms": 1000000/' "$CAMERA_CONFIG"
                    fi
                fi
            fi
        done
        
        # Set up LIBCAMERA_RPI_CONFIG_FILE environment variable (CRITICAL for camera detection)
        setup_libcamera_environment_postinst() {
            local config_file=""
            
            case "$PI_MODEL" in
                "pi5")
                    config_file="/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml"
                    ;;
                "pi4")
                    config_file="/usr/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml"
                    ;;
            esac
            
            if [ -n "$config_file" ] && [ -f "$config_file" ]; then
                echo "Setting up libcamera environment for $PI_MODEL..."
                
                # Add to system environment (for services)
                if ! grep -q "LIBCAMERA_RPI_CONFIG_FILE" /etc/environment 2>/dev/null; then
                    echo "LIBCAMERA_RPI_CONFIG_FILE=\"$config_file\"" >> /etc/environment
                    echo "Added LIBCAMERA_RPI_CONFIG_FILE to system environment"
                fi
            fi
        }
        
        setup_libcamera_environment_postinst
        
        if type -t create_pkgconfig_files &>/dev/null; then
            create_pkgconfig_files
        fi
        
        # Update library cache
        ldconfig

        # Reload systemd
        systemctl daemon-reload

        # ZeroMQ is now the primary IPC system - no broker configuration needed

        if [ -x /usr/lib/pitrac/pitrac-service-install.sh ]; then
            if [ -n "$ACTUAL_USER" ]; then
                echo "Installing PiTrac service for user: $ACTUAL_USER"
                /usr/lib/pitrac/pitrac-service-install.sh install "$ACTUAL_USER" || true
            else
                echo "Note: PiTrac service not installed. Run '/usr/lib/pitrac/pitrac-service-install.sh install <username>' to install for a specific user"
            fi
        fi


        echo ""
        echo "======================================"
        echo " PiTrac installed!"
        echo "======================================"
        echo ""
        echo "Get started:"
        echo "  pitrac setup    - Set up your directories"
        echo "  pitrac test     - Check your cameras"
        echo "  pitrac run      - Start tracking shots"
        echo ""
        echo "The web server starts automatically when you run PiTrac."
        echo ""
        echo "Need help? Try 'pitrac help' or 'pitrac status'"
        echo ""

        # Suggest reboot if Pi model detected
        if [ "$PI_MODEL" != "unknown" ]; then
            echo "Note: A reboot is recommended to apply boot configuration changes."
        fi
        ;;

    abort-upgrade|abort-remove|abort-deconfigure)
        ;;

    *)
        echo "postinst called with unknown argument: $1" >&2
        exit 1
        ;;
esac

exit 0