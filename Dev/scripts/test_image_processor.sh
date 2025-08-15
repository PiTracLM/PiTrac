#!/usr/bin/env bash
set -euo pipefail

# PiTrac Image Processing Test Runner
# Runs image processing pipeline on test images without camera hardware

SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Load defaults from YAML config
load_defaults "test-processor" "$@"

# Set up paths using detection or environment
if [ -z "${PITRAC_ROOT:-}" ]; then
    PITRAC_ROOT="$(detect_pitrac_root)"
fi
# For test images, go to the base PiTrac directory
PITRAC_BASE="$(dirname $(dirname "$PITRAC_ROOT"))"
TEST_DIR="${PITRAC_BASE}/${test_base_dir:-TestImages}"
RESULTS_DIR="${TEST_DIR}/results"
CONFIG_FILE="${PITRAC_ROOT}/ImageProcessing/golf_sim_config.json"
PITRAC_BINARY="${PITRAC_ROOT}/ImageProcessing/build/pitrac_lm"

# Test mode from command line or default
TEST_MODE="${1:-quick}"
TEED_IMAGE="${2:-}"
STROBED_IMAGE="${3:-}"

# Create necessary directories
setup_directories() {
    log_info "Setting up test directories..."
    mkdir -p "$TEST_DIR"/{default,custom,results}
    mkdir -p "$RESULTS_DIR"/{images,logs,data}
}

is_test_processor_installed() {
    local packages=("python3" "python3-yaml" "jq")
    for pkg in "${packages[@]}"; do
        if ! dpkg -s "$pkg" >/dev/null 2>&1; then
            return 1
        fi
    done
    
    [ -d "${TEST_DIR}/default" ] && [ -d "${TEST_DIR}/results" ]
}

check_binary() {
    if [ ! -f "$PITRAC_BINARY" ]; then
        log_error "PiTrac binary not found at: $PITRAC_BINARY"
        log_info "Please build PiTrac first using the main menu option 3"
        return 1
    fi
    log_success "Found PiTrac binary"
}

check_test_images() {
    local teed="${1:-${TEST_DIR}/default/${default_teed_image:-test_teed_ball.png}}"
    local strobed="${2:-${TEST_DIR}/default/${default_strobed_image:-test_strobed_ball.png}}"
    
    if [ ! -f "$teed" ]; then
        log_error "Teed ball image not found: $teed"
        if is_non_interactive; then
            return 1
        else
            read -p "Continue anyway? (y/N): " -n 1 -r
            echo
            [[ $REPLY =~ ^[Yy]$ ]] || return 1
        fi
    fi
    
    if [ ! -f "$strobed" ]; then
        log_error "Strobed image not found: $strobed"
        if is_non_interactive; then
            return 1
        else
            read -p "Continue anyway? (y/N): " -n 1 -r
            echo
            [[ $REPLY =~ ^[Yy]$ ]] || return 1
        fi
    fi
    
    log_success "Test images found"
    return 0
}

update_config() {
    local teed_image="$1"
    local strobed_image="$2"
    local temp_config="${CONFIG_FILE}.test"
    
    log_info "Updating configuration with test image paths..."
    
    # Use jq if available, otherwise use sed
    if command -v jq >/dev/null 2>&1; then
        jq --arg teed "$teed_image" --arg strobed "$strobed_image" \
            '.gs_config.testing.kTwoImageTestTeedBallImage = $teed | 
             .gs_config.testing.kTwoImageTestStrobedImage = $strobed' \
            "$CONFIG_FILE" > "$temp_config"
    else
        # Fallback to sed
        cp "$CONFIG_FILE" "$temp_config"
        sed -i "s|\"kTwoImageTestTeedBallImage\":.*|\"kTwoImageTestTeedBallImage\": \"$teed_image\",|" "$temp_config"
        sed -i "s|\"kTwoImageTestStrobedImage\":.*|\"kTwoImageTestStrobedImage\": \"$strobed_image\",|" "$temp_config"
    fi
    
    # Backup original and use test config
    cp "$CONFIG_FILE" "${CONFIG_FILE}.backup"
    mv "$temp_config" "$CONFIG_FILE"
}

restore_config() {
    if [ -f "${CONFIG_FILE}.backup" ]; then
        mv "${CONFIG_FILE}.backup" "$CONFIG_FILE"
        log_info "Restored original configuration"
    fi
}

run_test() {
    local teed_image="$1"
    local strobed_image="$2"
    local test_name="${3:-test}"
    local timestamp=$(date +"%Y%m%d_%H%M%S")
    local log_file="${RESULTS_DIR}/logs/test_${test_name}_${timestamp}.log"
    local timing_file="${RESULTS_DIR}/data/timing_${test_name}_${timestamp}.txt"
    
    log_info "Running test: $test_name"
    log_info "Teed ball image: $(basename "$teed_image")"
    log_info "Strobed image: $(basename "$strobed_image")"
    
    update_config "$teed_image" "$strobed_image"
    
    log_info "Starting image processing pipeline..."
    
    start_time=$(date +%s.%N)
    
    if timeout "${test_timeout_seconds:-30}" "$PITRAC_BINARY" \
        --system_mode="${test_system_mode:-kTest}" \
        --logging_level="${test_log_level:-info}" \
        --artifact_save_level="${test_artifact_level:-all}" \
        --base_image_logging_dir="$RESULTS_DIR/images" \
        > "$log_file" 2>&1; then
        
        end_time=$(date +%s.%N)
        
        # Calculate duration (using awk instead of bc which may not be installed)
        duration=$(awk "BEGIN {printf \"%.2f\", $end_time - $start_time}")
        
        log_success "Test completed in ${duration} seconds"
        
        echo "Test: $test_name" > "$timing_file"
        echo "Timestamp: $timestamp" >> "$timing_file"
        echo "Duration: ${duration} seconds" >> "$timing_file"
        echo "Teed Image: $teed_image" >> "$timing_file"
        echo "Strobed Image: $strobed_image" >> "$timing_file"
        
        extract_results "$log_file" "$timing_file"
        
        echo ""
        echo "============================================"
        cat "$timing_file"
        echo "============================================"
        
    else
        log_error "Test failed or timed out"
        restore_config
        return 1
    fi
    
    restore_config
    
    return 0
}

extract_results() {
    local log_file="$1"
    local output_file="$2"
    
    echo "" >> "$output_file"
    echo "=== Results ===" >> "$output_file"
    
    grep -i "ball.*speed\|launch.*angle\|spin\|rotation" "$log_file" >> "$output_file" 2>/dev/null || true
    
    if grep -q "ERROR" "$log_file"; then
        echo "" >> "$output_file"
        echo "=== Errors ===" >> "$output_file"
        grep "ERROR" "$log_file" >> "$output_file"
    fi
}

list_test_images() {
    log_info "Available test images:"
    echo ""
    
    if [ -d "$TEST_DIR/default" ]; then
        echo "Default images ($TEST_DIR/default/):"
        ls -la "$TEST_DIR/default/"*.png 2>/dev/null || echo "  No images found"
    fi
    
    echo ""
    
    if [ -d "$TEST_DIR/custom" ]; then
        echo "Custom images ($TEST_DIR/custom/):"
        ls -la "$TEST_DIR/custom/"*.png 2>/dev/null || echo "  No images found"
    fi
}

view_latest_results() {
    if [ -d "$RESULTS_DIR/data" ]; then
        local latest_result
        latest_result=$(ls -t "$RESULTS_DIR/data/timing_"*.txt 2>/dev/null | head -1)
        
        if [ -n "$latest_result" ] && [ -f "$latest_result" ]; then
            log_success "Latest results from: $(basename "$latest_result")"
            echo "============================================"
            cat "$latest_result"
            echo "============================================"
        else
            log_warn "No test results found"
            return 1
        fi
    else
        log_warn "Results directory not found"
        return 1
    fi
}

# Main function
main() {
    # Setup directories first
    setup_directories
    
    case "$TEST_MODE" in
        quick|custom)
            run_preflight_checks "test-processor" || return 1
            ;;
    esac
    
    case "$TEST_MODE" in
        quick)
            check_binary || return 1
            
            local teed="${TEST_DIR}/default/${default_teed_image:-test_teed_ball.png}"
            local strobed="${TEST_DIR}/default/${default_strobed_image:-test_strobed_ball.png}"
            
            if check_test_images "$teed" "$strobed"; then
                run_test "$teed" "$strobed" "quick"
            else
                return 1
            fi
            ;;
            
        custom)
            check_binary || return 1
            
            if [ -z "$TEED_IMAGE" ] || [ -z "$STROBED_IMAGE" ]; then
                log_error "Usage: $0 custom <teed_image> <strobed_image>"
                return 1
            fi
            
            if check_test_images "$TEED_IMAGE" "$STROBED_IMAGE"; then
                run_test "$TEED_IMAGE" "$STROBED_IMAGE" "custom"
            else
                return 1
            fi
            ;;
            
        list)
            list_test_images
            ;;
            
        results)
            view_latest_results
            ;;
            
        *)
            log_error "Unknown test mode: $TEST_MODE"
            echo "Usage: $0 [quick|custom <teed> <strobed>|list|results]"
            return 1
            ;;
    esac
}

main "$@"