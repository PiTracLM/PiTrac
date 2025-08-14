#!/usr/bin/env bash
set -euo pipefail

# PiTrac Launch Monitor Run Script
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Load defaults from config file
load_defaults "run-pitrac" "$@"

# Get build directory from pitrac-build configuration
BUILD_DIR="${BUILD_DIR:-$HOME/Dev}"
BUILD_DIR="${BUILD_DIR/#\~/$HOME}"
PITRAC_DIR="${BUILD_DIR}/PiTrac"

# Set PITRAC_ROOT from built location
export PITRAC_ROOT="${PITRAC_DIR}/Software/LMSourceCode"
PITRAC_BINARY="${PITRAC_ROOT}/ImageProcessing/build/pitrac_lm"

# Runtime configuration from defaults
PI_MODE="${pi_mode:-single}"
LOGGING_LEVEL="${logging_level:-info}"
SYSTEM_MODE="${system_mode:-kNormal}"
ARTIFACT_SAVE_LEVEL="${artifact_save_level:-minimal}"
TEST_TIMEOUT="${test_timeout:-30}"
CAMERA1_INDEX="${camera1_index:-1}"
CAMERA2_INDEX="${camera2_index:-2}"
RUN_SCRIPTS_DIR="${run_scripts_dir:-RunScripts}"
ENABLE_WEB_SERVER="${enable_web_server:-1}"
WEB_SERVER_PORT="${web_server_port:-8080}"
ENABLE_MSG_BROKER="${enable_msg_broker:-1}"
ENABLE_E6="${enable_e6:-0}"
ENABLE_GSPRO="${enable_gspro:-0}"
STROBE_TEST_DURATION="${strobe_test_duration:-10}"
STROBE_TEST_FREQUENCY="${strobe_test_frequency:-50}"
TRIGGER_TEST_MODE="${trigger_test_mode:-external}"
AUTO_RESTART="${auto_restart:-0}"
MAX_RESTARTS="${max_restarts:-3}"
RESTART_DELAY="${restart_delay:-5}"

# Check if PiTrac is built
is_pitrac_built() {
    [ -f "$PITRAC_BINARY" ] && [ -x "$PITRAC_BINARY" ]
}

# Check if PiTrac is installed (following naming convention)
is_run_pitrac_installed() {
    is_pitrac_built
}

# Load environment variables from shell RC files
load_environment() {
    local loaded=false
    
    if [ -f "$HOME/.bashrc" ] && grep -q "PITRAC_ROOT" "$HOME/.bashrc"; then
        source "$HOME/.bashrc"
        loaded=true
    elif [ -f "$HOME/.zshrc" ] && grep -q "PITRAC_ROOT" "$HOME/.zshrc"; then
        source "$HOME/.zshrc"
        loaded=true
    fi
    
    if [ "$loaded" = false ]; then
        log_warn "PiTrac environment variables not found in shell RC files"
        log_info "Using defaults from build configuration"
        
        # Set critical environment variables if not set
        export PITRAC_BASE_IMAGE_LOGGING_DIR="${PITRAC_BASE_IMAGE_LOGGING_DIR:-$HOME/LM_Shares/Images/}"
        export PITRAC_WEBSERVER_SHARE_DIR="${PITRAC_WEBSERVER_SHARE_DIR:-$HOME/LM_Shares/WebShare/}"
        export PITRAC_MSG_BROKER_FULL_ADDRESS="${PITRAC_MSG_BROKER_FULL_ADDRESS:-tcp://localhost:61616}"
    fi
}

# Check dependencies (non-blocking)
check_runtime_dependencies() {
    local warnings=""
    local critical=false
    
    # Check if ActiveMQ is running
    if [ "$ENABLE_MSG_BROKER" = "1" ]; then
        if ! systemctl is-active --quiet activemq 2>/dev/null && \
           ! pgrep -f activemq >/dev/null 2>&1; then
            warnings="${warnings}  - ActiveMQ Broker not running\n"
        fi
    fi
    
    # Check if TomEE is running for web interface
    if [ "$ENABLE_WEB_SERVER" = "1" ]; then
        if ! systemctl is-active --quiet tomee 2>/dev/null && \
           ! pgrep -f tomee >/dev/null 2>&1; then
            warnings="${warnings}  - TomEE web server not running\n"
        fi
    fi
    
    # Check for camera devices
    if ! ls /dev/video* >/dev/null 2>&1; then
        warnings="${warnings}  - No camera devices found\n"
        critical=true
    fi
    
    # Check required directories exist
    if [ ! -d "$PITRAC_BASE_IMAGE_LOGGING_DIR" ]; then
        mkdir -p "$PITRAC_BASE_IMAGE_LOGGING_DIR" || {
            warnings="${warnings}  - Cannot create image logging directory\n"
        }
    fi
    
    if [ ! -d "$PITRAC_WEBSERVER_SHARE_DIR" ]; then
        mkdir -p "$PITRAC_WEBSERVER_SHARE_DIR" || {
            warnings="${warnings}  - Cannot create web share directory\n"
        }
    fi
    
    if [ -n "$warnings" ]; then
        log_warn "Runtime dependency issues detected:"
        echo -e "$warnings"
        
        if [ "$critical" = true ]; then
            log_error "Critical issues found"
            if ! is_non_interactive; then
                read -p "Continue anyway? (y/N): " -n 1 -r
                echo
                [[ ! $REPLY =~ ^[Yy]$ ]] && return 1
            else
                return 1
            fi
        else
            log_info "Non-critical issues - PiTrac may still run"
        fi
    else
        log_success "All runtime dependencies OK"
    fi
    
    return 0
}

# Run single-Pi setup
run_single_pi() {
    log_info "Starting PiTrac in Single-Pi mode"
    log_info "Logging level: $LOGGING_LEVEL"
    log_info "System mode: $SYSTEM_MODE"
    
    cd "$PITRAC_ROOT/ImageProcessing"
    
    # Check for specific run script
    local run_script="$RUN_SCRIPTS_DIR/runSinglePi.sh"
    if [ -f "$run_script" ] && [ -x "$run_script" ]; then
        log_info "Using run script: $run_script"
        exec "$run_script"
    else
        log_info "Running with command-line parameters"
        exec "$PITRAC_BINARY" \
            --logging_level="$LOGGING_LEVEL" \
            --system_mode="$SYSTEM_MODE" \
            --artifact_save_level="$ARTIFACT_SAVE_LEVEL"
    fi
}

# Run Camera 1 (primary Pi in dual setup)
run_camera1() {
    log_info "Starting Camera 1 (Primary Pi)"
    log_warn "Make sure Camera 2 is running on the secondary Pi first!"
    
    cd "$PITRAC_ROOT/ImageProcessing"
    
    local run_script="$RUN_SCRIPTS_DIR/runCam1.sh"
    if [ -f "$run_script" ] && [ -x "$run_script" ]; then
        log_info "Using run script: $run_script"
        exec "$run_script"
    else
        log_info "Running with command-line parameters"
        exec "$PITRAC_BINARY" \
            --logging_level="$LOGGING_LEVEL" \
            --camera_index="$CAMERA1_INDEX"
    fi
}

# Run Camera 2 (secondary Pi in dual setup)
run_camera2() {
    log_info "Starting Camera 2 (Secondary Pi)"
    log_info "This should be started BEFORE Camera 1"
    
    cd "$PITRAC_ROOT/ImageProcessing"
    
    local run_script="$RUN_SCRIPTS_DIR/runCam2.sh"
    if [ -f "$run_script" ] && [ -x "$run_script" ]; then
        log_info "Using run script: $run_script"
        exec "$run_script"
    else
        log_info "Running with command-line parameters"
        exec "$PITRAC_BINARY" \
            --logging_level="$LOGGING_LEVEL" \
            --camera_index="$CAMERA2_INDEX"
    fi
}

# Test strobe light
test_strobe() {
    log_info "Testing strobe light"
    log_warn "You should see dark-reddish pulses in the LED lens"
    log_warn "Duration: ${STROBE_TEST_DURATION} seconds"
    
    cd "$PITRAC_ROOT/ImageProcessing"
    
    local pulse_script="$RUN_SCRIPTS_DIR/runPulseTest.sh"
    if [ -f "$pulse_script" ] && [ -x "$pulse_script" ]; then
        log_info "Using pulse test script"
        timeout "$STROBE_TEST_DURATION" "$pulse_script" || true
    else
        log_error "Pulse test script not found: $pulse_script"
        return 1
    fi
    
    log_success "Strobe test completed"
}

# Test camera trigger
test_camera_trigger() {
    log_info "Camera Trigger Test Setup"
    echo ""
    echo "For dual-Pi setup:"
    echo "1. On Pi 2, set external trigger mode:"
    echo "   sudo $PITRAC_ROOT/ImageProcessing/CameraTools/setCameraTriggerExternal.sh"
    echo ""
    echo "2. On Pi 2, run camera test:"
    echo "   rpicam-hello"
    echo ""
    echo "3. On Pi 1, run pulse test:"
    echo "   $PITRAC_ROOT/ImageProcessing/RunScripts/runPulseTest.sh"
    echo ""
    echo "Camera 2 should capture when Camera 1 triggers"
    echo ""
    echo "4. After testing, restore internal trigger on Pi 2:"
    echo "   sudo $PITRAC_ROOT/ImageProcessing/CameraTools/setCameraTriggerInternal.sh"
}

# Run with restart capability
run_with_restart() {
    local mode="$1"
    local restart_count=0
    
    while [ "$restart_count" -lt "$MAX_RESTARTS" ]; do
        log_info "Starting PiTrac (attempt $((restart_count + 1))/$MAX_RESTARTS)"
        
        case "$mode" in
            single) run_single_pi ;;
            cam1) run_camera1 ;;
            cam2) run_camera2 ;;
            *)
                log_error "Invalid mode: $mode"
                return 1
                ;;
        esac
        
        local exit_code=$?
        
        if [ "$exit_code" -eq 0 ]; then
            log_success "PiTrac exited normally"
            break
        elif [ "$AUTO_RESTART" = "1" ]; then
            restart_count=$((restart_count + 1))
            if [ "$restart_count" -lt "$MAX_RESTARTS" ]; then
                log_warn "PiTrac exited with code $exit_code, restarting in $RESTART_DELAY seconds..."
                sleep "$RESTART_DELAY"
            else
                log_error "Maximum restart attempts reached"
                return 1
            fi
        else
            log_error "PiTrac exited with code $exit_code"
            return $exit_code
        fi
    done
}

# Main function
main() {
    local action="${1:-run}"
    
    # Check if PiTrac is built
    if ! is_pitrac_built; then
        log_error "PiTrac is not built!"
        log_info "Binary not found: $PITRAC_BINARY"
        log_info "Please build PiTrac first using the build script"
        return 1
    fi
    
    # Load environment
    load_environment
    
    # Check dependencies (non-blocking)
    check_runtime_dependencies
    
    case "$action" in
        run)
            # Default run based on PI_MODE
            if [ "$PI_MODE" = "single" ]; then
                run_single_pi
            else
                log_info "Dual-Pi mode - please specify cam1 or cam2"
                echo "Usage: $0 [run|cam1|cam2|test-strobe|test-trigger|help]"
                return 1
            fi
            ;;
        cam1)
            run_camera1
            ;;
        cam2)
            run_camera2
            ;;
        test-strobe)
            test_strobe
            ;;
        test-trigger)
            test_camera_trigger
            ;;
        restart-single)
            AUTO_RESTART=1
            run_with_restart single
            ;;
        restart-cam1)
            AUTO_RESTART=1
            run_with_restart cam1
            ;;
        restart-cam2)
            AUTO_RESTART=1
            run_with_restart cam2
            ;;
        help)
            echo "PiTrac Launch Monitor Runtime"
            echo ""
            echo "Usage: $0 [command]"
            echo ""
            echo "Commands:"
            echo "  run           - Run based on configured mode (default)"
            echo "  cam1          - Run Camera 1 (dual-Pi primary)"
            echo "  cam2          - Run Camera 2 (dual-Pi secondary)"
            echo "  test-strobe   - Test strobe light"
            echo "  test-trigger  - Show camera trigger test instructions"
            echo "  restart-single - Run single-Pi with auto-restart"
            echo "  restart-cam1  - Run Camera 1 with auto-restart"
            echo "  restart-cam2  - Run Camera 2 with auto-restart"
            echo "  help          - Show this help message"
            echo ""
            echo "Configuration:"
            echo "  PI_MODE=$PI_MODE"
            echo "  LOGGING_LEVEL=$LOGGING_LEVEL"
            echo "  PITRAC_ROOT=$PITRAC_ROOT"
            ;;
        *)
            log_error "Unknown command: $action"
            echo "Use '$0 help' for usage information"
            return 1
            ;;
    esac
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi