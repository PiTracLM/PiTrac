#!/bin/bash
#
# PiTrac Camera Detection Tool
# Purpose: Detect and validate Raspberry Pi cameras for PiTrac configuration
# 
# Detects:
#   - Camera model and sensor type
#   - PiTrac type ID (4 or 5 for supported cameras)
#   - Physical CSI port (CAM0 or CAM1)
#
# Usage:
#   ./detect_pi_cameras.sh [options]
#
# Options:
#   -v, --verbose : Show detailed detection methods
#   -q, --quiet   : Show minimal summary only
#   --no-color    : Disable ANSI color output
#
set -euo pipefail

# ============================================================================
# Constants and Configuration
# ============================================================================

# Color codes (disabled if NO_COLOR is set or not a terminal)
setup_colors() {
    if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
        RED='\033[0;31m'
        GREEN='\033[0;32m'
        YELLOW='\033[1;33m'
        BLUE='\033[0;34m'
        BOLD='\033[1m'
        NC='\033[0m'
    else
        RED="" GREEN="" YELLOW="" BLUE="" BOLD="" NC=""
    fi
}

# Camera sensor to model mapping
declare -A CAMERA_MODELS=(
    ["ov5647"]="Pi Camera v1.3"
    ["imx219"]="Pi Camera v2"
    ["imx477"]="Pi HQ Camera"
    ["imx296"]="Global Shutter Camera"
    ["imx708"]="Pi Camera v3"
)

# PiTrac type mappings (from camera_hardware.h)
declare -A PITRAC_TYPES=(
    ["ov5647"]=1    # Deprecated - not recommended
    ["imx219"]=2    # Deprecated - not recommended
    ["imx477"]=3    # Deprecated - not recommended
    ["imx296"]=4    # Default for IMX296 (Pi GS)
    ["imx708"]=0    # Unsupported
)

# Camera support status
declare -A CAMERA_STATUS=(
    ["ov5647"]="DEPRECATED"
    ["imx219"]="DEPRECATED"
    ["imx477"]="DEPRECATED"
    ["imx296"]="SUPPORTED"
    ["imx708"]="UNSUPPORTED"
)

# IMX296 specific types
PITRAC_TYPE_PI_GS=4           # Raspberry Pi Global Shutter (color)
PITRAC_TYPE_INNOMAKER=5       # InnoMaker IMX296 (mono)

# Device tree root paths
DT_ROOT="/sys/firmware/devicetree/base"
DT_ROOT_ALT="/proc/device-tree"

# InnoMaker trigger tool path
INNOMAKER_TRIGGER="/usr/lib/pitrac/ImageProcessing/CameraTools/imx296_trigger"

# ============================================================================
# Command Line Parsing
# ============================================================================

VERBOSE=0
QUIET=0

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -v|--verbose)
                VERBOSE=1
                ;;
            -q|--quiet)
                QUIET=1
                ;;
            --no-color)
                export NO_COLOR=1
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
        shift
    done
}

show_help() {
    cat << EOF
PiTrac Camera Detection Tool

Usage: $0 [options]

Options:
  -v, --verbose  Show detailed detection methods and diagnostics
  -q, --quiet    Show minimal summary only
  --no-color     Disable colored output
  -h, --help     Show this help message

This tool detects Raspberry Pi cameras and provides PiTrac configuration values.

Supported cameras (PiTrac primary):
  - Raspberry Pi Global Shutter Camera (IMX296) - Type 4
  - InnoMaker IMX296 Mono Camera - Type 5

Legacy cameras (deprecated, may not work):
  - Pi Camera v1.3 (OV5647) - Type 1
  - Pi Camera v2 (IMX219) - Type 2
  - Pi HQ Camera (IMX477) - Type 3

Unsupported:
  - Pi Camera v3 (IMX708) - Type 0
EOF
}

# ============================================================================
# Utility Functions
# ============================================================================

# Detect Raspberry Pi model
detect_pi_model() {
    local model=""
    
    if [ -r "$DT_ROOT/model" ]; then
        model=$(tr -d '\0' < "$DT_ROOT/model")
    elif [ -r "$DT_ROOT_ALT/model" ]; then
        model=$(tr -d '\0' < "$DT_ROOT_ALT/model")
    elif grep -q "Raspberry Pi" /proc/cpuinfo 2>/dev/null; then
        model=$(grep -m1 "Model" /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ *//')
    else
        model="Unknown"
    fi
    
    case "$model" in
        *"Raspberry Pi 5"*|*"Raspberry Pi Compute Module 5"*) echo "pi5" ;;
        *"Raspberry Pi 4"*|*"Raspberry Pi Compute Module 4"*) echo "pi4" ;;
        *"Raspberry Pi 3"*|*"Raspberry Pi Compute Module 3"*) echo "pi3" ;;
        *"Raspberry Pi 2"*) echo "pi2" ;;
        *"Raspberry Pi"*)   echo "pi_other" ;;
        *)                  echo "unknown" ;;
    esac
}

# Get the appropriate camera detection command
get_camera_cmd() {
    # Try various camera detection commands in order of preference
    local commands=(
        "rpicam-hello"
        "libcamera-hello"
        "rpicam-still"
        "libcamera-still"
        "raspistill"
    )
    
    for cmd in "${commands[@]}"; do
        if command -v "$cmd" >/dev/null 2>&1; then
            echo "$cmd"
            return 0
        fi
        
        # Also check common installation paths
        for path in /usr/bin /usr/local/bin /opt/vc/bin; do
            if [ -x "$path/$cmd" ]; then
                echo "$path/$cmd"
                return 0
            fi
        done
    done
    
    return 1
}

# Check for required tools
check_dependencies() {
    local camera_cmd
    camera_cmd=$(get_camera_cmd || true)
    
    if [ -z "$camera_cmd" ]; then
        echo -e "${RED}Error: No camera detection tool found${NC}"
        echo
        echo "This script requires one of the following tools:"
        echo "  - rpicam-hello or rpicam-still (Raspberry Pi OS Bookworm and later)"
        echo "  - libcamera-hello or libcamera-still (Raspberry Pi OS Bullseye)"
        echo "  - raspistill (Legacy camera stack)"
        echo
        echo "Install with:"
        echo "  For Bookworm (latest):"
        echo "    sudo apt update && sudo apt install rpicam-apps"
        echo
        echo "  For Bullseye (older):"
        echo "    sudo apt update && sudo apt install libcamera-apps"
        echo
        echo "  For legacy stack:"
        echo "    sudo raspi-config (enable legacy camera)"
        exit 1
    fi
}

# Read a uint32 from device tree property
dt_read_u32() {
    od -An -tu4 -N4 "$1" 2>/dev/null | awk '{print $1}'
}

# Find device tree node by phandle
dt_find_node_by_phandle() {
    local target="$1"
    local base
    
    for base in "$DT_ROOT" "$DT_ROOT_ALT"; do
        [ -d "$base" ] || continue
        
        while IFS= read -r -d '' phandle_file; do
            local val
            val=$(dt_read_u32 "$phandle_file")
            
            if [ -n "${val:-}" ] && [ "$val" = "$target" ]; then
                dirname "$phandle_file"
                return 0
            fi
        done < <(find "$base" -type f -name phandle -print0 2>/dev/null || true)
    done
    
    return 1
}

# Determine physical CSI port from device tree
dt_sensor_to_cam_port() {
    local sensor_node="$1"
    [ -d "$sensor_node" ] || { echo "UNKNOWN"; return; }
    
    # Look for port or ports directory
    local ports_dir="$sensor_node/port"
    if [ -d "$sensor_node/ports" ]; then
        ports_dir="$sensor_node/ports"
    elif [ ! -d "$ports_dir" ]; then
        ports_dir="$sensor_node"
    fi
    
    # Find endpoint and trace to CSI port
    while IFS= read -r -d '' endpoint; do
        local remote_endpoint="$endpoint/remote-endpoint"
        [ -f "$remote_endpoint" ] || continue
        
        local phandle
        phandle=$(dt_read_u32 "$remote_endpoint")
        [ -n "${phandle:-}" ] || continue
        
        local remote_node
        remote_node=$(dt_find_node_by_phandle "$phandle" || true)
        [ -n "${remote_node:-}" ] || continue
        
        local parent_port
        parent_port=$(dirname "$remote_node")
        local port_name
        port_name=$(basename "$parent_port")
        
        if [[ "$port_name" =~ port@([0-9]+) ]]; then
            local port_idx="${BASH_REMATCH[1]}"
            case "$port_idx" in
                0) echo "CAM0"; return ;;
                1) echo "CAM1"; return ;;
                *) echo "CSI$port_idx"; return ;;
            esac
        fi
    done < <(find "$ports_dir" -maxdepth 2 -type d -name 'endpoint@*' -print0 2>/dev/null || true)
    
    echo "UNKNOWN"
}

# Extract device tree path from libcamera info
extract_dt_path_from_info() {
    local info="$1"
    local suffix
    
    if [[ "$info" =~ \((/base/.*)\) ]]; then
        suffix="${BASH_REMATCH[1]}"
        suffix="${suffix#/base}"
        
        # Try both DT roots
        for root in "$DT_ROOT" "$DT_ROOT_ALT"; do
            local candidate="${root}${suffix}"
            if [ -d "$candidate" ]; then
                echo "$candidate"
                return
            fi
        done
    fi
    
    echo ""
}

# Heuristic port detection for Pi 5 RP1 chip
heuristic_port_from_path() {
    local info="$1"
    
    # Pi 5 RP1 I2C addresses
    if echo "$info" | grep -q "i2c@88000"; then
        echo "CAM0"
    elif echo "$info" | grep -q "i2c@80000"; then
        echo "CAM1"
    else
        echo "UNKNOWN"
    fi
}

# Determine if a CFA pattern indicates color sensor
is_color_sensor() {
    echo "$1" | grep -Eq 'RGGB|BGGR|GRBG|GBRG'
}

# ============================================================================
# Camera Detection
# ============================================================================

# Data structures for detected cameras
declare -a CAMERA_INDEXES=()
declare -A CAMERA_INFO=()
declare -A CAMERA_SENSOR=()
declare -A CAMERA_MODEL_NAME=()
declare -A CAMERA_CFA=()
declare -A CAMERA_PITRAC_TYPE=()
declare -A CAMERA_DESCRIPTION=()
declare -A CAMERA_DT_PATH=()
declare -A CAMERA_PORT=()
declare -A CAMERA_STATUS=()

detect_cameras() {
    local camera_cmd
    camera_cmd=$(get_camera_cmd)
    
    if [ -z "$camera_cmd" ]; then
        echo -e "${RED}Error: Camera detection command not found${NC}"
        return 1
    fi
    
    local libcamera_output
    
    # Different commands need different arguments
    case "$(basename "$camera_cmd")" in
        rpicam-hello|libcamera-hello)
            libcamera_output=$($camera_cmd --list-cameras 2>&1 || true)
            ;;
        rpicam-still|libcamera-still)
            libcamera_output=$($camera_cmd --list-cameras 2>&1 || true)
            ;;
        raspistill)
            # raspistill doesn't have --list-cameras, use vcgencmd
            if command -v vcgencmd >/dev/null 2>&1; then
                local supported
                supported=$(vcgencmd get_camera 2>&1 || true)
                if echo "$supported" | grep -q "supported=1"; then
                    # Try to detect camera model through other means
                    libcamera_output="0 : Legacy camera detected (check with libcamera tools for details)"
                else
                    libcamera_output="No cameras available"
                fi
            else
                libcamera_output="No cameras available"
            fi
            ;;
        *)
            libcamera_output=$($camera_cmd --list-cameras 2>&1 || true)
            ;;
    esac
    
    # Parse libcamera output
    while IFS= read -r line; do
        if [[ $line =~ ^[[:space:]]*([0-9]+)[[:space:]]*:[[:space:]]*(.*) ]]; then
            local idx="${BASH_REMATCH[1]}"
            local info="${BASH_REMATCH[2]}"
            
            CAMERA_INDEXES+=("$idx")
            CAMERA_INFO["$idx"]="$info"
            
            # Detect sensor type
            local sensor=""
            for model in "${!CAMERA_MODELS[@]}"; do
                if echo "$info" | grep -qi "$model"; then
                    sensor="$model"
                    break
                fi
            done
            
            CAMERA_SENSOR["$idx"]="$sensor"
            CAMERA_MODEL_NAME["$idx"]="${CAMERA_MODELS[$sensor]:-Unknown}"
            CAMERA_STATUS["$idx"]="${CAMERA_STATUS[$sensor]:-UNKNOWN}"
            
            # Detect CFA (Color Filter Array)
            local cfa=""
            if echo "$info" | grep -qi "MONO"; then
                cfa="MONO"
            elif is_color_sensor "$info"; then
                cfa="COLOR"
            fi
            CAMERA_CFA["$idx"]="$cfa"
            
            # Determine PiTrac type
            local pitrac_type="${PITRAC_TYPES[$sensor]:-0}"
            local description="${CAMERA_MODELS[$sensor]:-Unknown}"
            
            # Special handling for IMX296
            if [[ "$sensor" == "imx296" ]]; then
                if [[ "$cfa" == "COLOR" ]]; then
                    pitrac_type=$PITRAC_TYPE_PI_GS
                    description="Raspberry Pi Global Shutter (Color)"
                elif [[ "$cfa" == "MONO" ]]; then
                    if [ -x "$INNOMAKER_TRIGGER" ]; then
                        pitrac_type=$PITRAC_TYPE_INNOMAKER
                        description="InnoMaker IMX296 (Mono)"
                    else
                        pitrac_type=$PITRAC_TYPE_INNOMAKER
                        description="IMX296 Mono (InnoMaker-compatible)"
                    fi
                else
                    # Unknown CFA, default to Pi GS
                    pitrac_type=$PITRAC_TYPE_PI_GS
                    description="IMX296 (Unknown variant)"
                fi
            fi
            
            CAMERA_PITRAC_TYPE["$idx"]="$pitrac_type"
            CAMERA_DESCRIPTION["$idx"]="$description"
            
            # Detect physical port
            local dt_path
            dt_path=$(extract_dt_path_from_info "$info")
            CAMERA_DT_PATH["$idx"]="$dt_path"
            
            local port="UNKNOWN"
            if [ -n "$dt_path" ] && [ -d "$dt_path" ]; then
                port=$(dt_sensor_to_cam_port "$dt_path")
            fi
            
            # Fallback to heuristic detection for Pi 5
            if [ "$port" = "UNKNOWN" ]; then
                port=$(heuristic_port_from_path "$info")
            fi
            
            CAMERA_PORT["$idx"]="$port"
        fi
    done <<< "$libcamera_output"
    
    return 0
}

# ============================================================================
# Output Functions
# ============================================================================

print_header() {
    echo -e "${BOLD}======================================${NC}"
    echo -e "${BOLD}      PiTrac Camera Detection Tool     ${NC}"
    echo -e "${BOLD}======================================${NC}"
    echo
}

print_system_info() {
    local pi_model
    pi_model=$(detect_pi_model)
    
    echo -e "${BLUE}System Information:${NC}"
    echo "  Raspberry Pi Model: $pi_model"
    
    local camera_cmd
    camera_cmd=$(get_camera_cmd)
    echo "  Detection Tool: $camera_cmd"
    echo
}

print_summary() {
    if [ ${#CAMERA_INDEXES[@]} -eq 0 ]; then
        echo -e "${RED}No cameras detected!${NC}"
        echo
        echo "Troubleshooting:"
        echo "  1. Check ribbon cable connections and orientation"
        echo "  2. Power cycle the Raspberry Pi"
        echo "  3. Verify camera is enabled in raspi-config"
        echo "  4. Check dmesg for camera probe errors"
        return
    fi
    
    echo -e "${BOLD}Detected Cameras:${NC}"
    echo
    
    for idx in "${CAMERA_INDEXES[@]}"; do
        local sensor="${CAMERA_SENSOR[$idx]}"
        local model="${CAMERA_MODEL_NAME[$idx]}"
        local desc="${CAMERA_DESCRIPTION[$idx]}"
        local type="${CAMERA_PITRAC_TYPE[$idx]}"
        local port="${CAMERA_PORT[$idx]}"
        local cfa="${CAMERA_CFA[$idx]}"
        local status="${CAMERA_STATUS[$idx]}"
        
        # Status color
        local status_color="$YELLOW"
        case "$status" in
            SUPPORTED)   status_color="$GREEN" ;;
            DEPRECATED)  status_color="$YELLOW" ;;
            UNSUPPORTED) status_color="$RED" ;;
        esac
        
        echo -e "  ${BOLD}Camera $idx:${NC}"
        echo -e "    Model:        $model"
        echo -e "    Sensor:       $sensor"
        echo -e "    Description:  $desc"
        echo -e "    Port:         $port"
        echo -e "    CFA:          ${cfa:-Unknown}"
        echo -e "    PiTrac Type:  $type"
        echo -e "    Status:       ${status_color}$status${NC}"
        
        if [[ "$status" == "DEPRECATED" ]]; then
            echo -e "    ${YELLOW}⚠ Warning: This camera is deprecated and may not work properly${NC}"
        elif [[ "$status" == "UNSUPPORTED" ]]; then
            echo -e "    ${RED}✗ Error: This camera is not supported by PiTrac${NC}"
        fi
        echo
    done
}

print_configuration() {
    if [ ${#CAMERA_INDEXES[@]} -eq 0 ]; then
        return
    fi
    
    # Get types for slot1 and slot2
    local slot1_type="${CAMERA_PITRAC_TYPE[0]:-4}"
    local slot2_type="${CAMERA_PITRAC_TYPE[1]:-4}"
    
    # Check if cameras are supported
    local all_supported=1
    for idx in "${CAMERA_INDEXES[@]}"; do
        if [[ "${CAMERA_STATUS[$idx]}" == "UNSUPPORTED" ]]; then
            all_supported=0
            break
        fi
    done
    
    if [ $all_supported -eq 0 ]; then
        echo -e "${RED}⚠ Warning: Unsupported cameras detected!${NC}"
        echo "PiTrac requires IMX296-based Global Shutter cameras."
        echo
    fi
    
    echo -e "${BOLD}PiTrac Configuration:${NC}"
    echo
    echo "YAML Configuration (/etc/pitrac/pitrac.yaml):"
    echo "  cameras:"
    echo "    slot1_type: $slot1_type"
    echo "    slot2_type: $slot2_type"
    echo
    echo "Environment Variables:"
    echo "  export PITRAC_SLOT1_CAMERA_TYPE=$slot1_type"
    echo "  export PITRAC_SLOT2_CAMERA_TYPE=$slot2_type"
    echo
    
    # Recommendations
    if [[ "${CAMERA_STATUS[0]}" == "DEPRECATED" ]] || [[ "${CAMERA_STATUS[1]}" == "DEPRECATED" ]]; then
        echo -e "${YELLOW}Note: Deprecated cameras detected.${NC}"
        echo "For best results, upgrade to Raspberry Pi Global Shutter cameras (IMX296)."
        echo
    fi
}

print_verbose_info() {
    [ $VERBOSE -eq 0 ] && return
    
    echo -e "${BOLD}Detailed Detection Information:${NC}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    for idx in "${CAMERA_INDEXES[@]}"; do
        echo -e "\n${BOLD}Camera $idx Full Details:${NC}"
        echo "  Raw Info: ${CAMERA_INFO[$idx]}"
        echo "  DT Path: ${CAMERA_DT_PATH[$idx]:-Not found}"
        echo
    done
    
    # Check for InnoMaker trigger tool
    echo -e "${BOLD}InnoMaker Support:${NC}"
    if [ -x "$INNOMAKER_TRIGGER" ]; then
        echo -e "  ${GREEN}✓${NC} InnoMaker trigger tool found: $INNOMAKER_TRIGGER"
    else
        echo -e "  ${YELLOW}✗${NC} InnoMaker trigger tool not found"
    fi
    echo
    
    # Device tree information
    if [ -d "$DT_ROOT" ]; then
        echo -e "${BOLD}Device Tree Information:${NC}"
        echo "  DT Root: $DT_ROOT"
        
        # Look for camera-related nodes
        local cam_nodes
        cam_nodes=$(find "$DT_ROOT" -name "*cam*" -o -name "*imx*" -o -name "*ov*" 2>/dev/null | head -5 || true)
        if [ -n "$cam_nodes" ]; then
            echo "  Sample Camera Nodes:"
            echo "$cam_nodes" | sed 's/^/    /'
        fi
    fi
    echo
}

print_help_footer() {
    [ $QUIET -eq 1 ] && return
    
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo
    echo "For more information:"
    echo "  - PiTrac Documentation: https://jamespilgrim.github.io/PiTrac/"
    echo "  - Discord Community: https://discord.gg/j9YWCMFVHN"
}

# ============================================================================
# Main
# ============================================================================

main() {
    parse_args "$@"
    setup_colors
    
    if [ $QUIET -eq 0 ]; then
        print_header
    fi
    
    check_dependencies
    
    if [ $QUIET -eq 0 ]; then
        print_system_info
    fi
    
    detect_cameras
    
    print_summary
    print_configuration
    print_verbose_info
    
    if [ $QUIET -eq 0 ]; then
        print_help_footer
    fi
    
    # Exit with error if no supported cameras found
    local has_supported=0
    for idx in "${CAMERA_INDEXES[@]}"; do
        if [[ "${CAMERA_STATUS[$idx]}" == "SUPPORTED" ]]; then
            has_supported=1
            break
        fi
    done
    
    if [ ${#CAMERA_INDEXES[@]} -eq 0 ]; then
        exit 1
    elif [ $has_supported -eq 0 ]; then
        exit 2
    fi
    
    exit 0
}

# Run main function
main "$@"