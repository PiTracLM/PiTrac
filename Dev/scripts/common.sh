#!/usr/bin/env bash
# Common utilities for PiTrac installation scripts

# Handle sudo for both Docker and Pi
SUDO=""
if command -v sudo >/dev/null 2>&1 && [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
fi
export DEBIAN_FRONTEND=noninteractive

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Logging functions
log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $*"; }
log_progress() { echo -e "${CYAN}[PROGRESS]${NC} $*"; }

# Check if command exists
need_cmd() { 
    command -v "$1" >/dev/null 2>&1
}

# Package management helper
apt_ensure() {
    local need=()
    for p in "$@"; do 
        if ! dpkg -s "$p" >/dev/null 2>&1; then
            need+=("$p")
        fi
    done
    
    if [ "${#need[@]}" -gt 0 ]; then
        log_info "Installing system packages: ${need[*]}"
        $SUDO apt-get update
        $SUDO apt-get install -y --no-install-recommends "${need[@]}"
    fi
}

# Version comparison helper with fallback
version_ge() {
    # Try dpkg first (most accurate)
    if command -v dpkg >/dev/null 2>&1; then
        dpkg --compare-versions "$1" ge "$2" 2>/dev/null && return 0
    fi
    
    # Fallback to simple version comparison
    version_compare_fallback "$1" "$2"
}

# Simple version comparison fallback
version_compare_fallback() {
    local version1="$1"
    local version2="$2"
    
    # Handle empty versions
    [ -z "$version1" ] && return 1
    [ -z "$version2" ] && return 0
    
    # Same version
    [ "$version1" = "$version2" ] && return 0
    
    # Compare using sort -V if available
    if command -v sort >/dev/null 2>&1; then
        [ "$(printf '%s\n' "$version2" "$version1" | sort -V | head -n1)" = "$version2" ] && return 0
    fi
    
    # Basic numeric comparison for simple versions
    local IFS=.
    local i ver1=($version1) ver2=($version2)
    
    # Fill empty fields with zeros
    for ((i=${#ver1[@]}; i<${#ver2[@]}; i++)); do
        ver1[i]=0
    done
    
    for ((i=0; i<${#ver1[@]}; i++)); do
        # Handle missing parts in ver2
        if [ -z "${ver2[i]:-}" ]; then
            ver2[i]=0
        fi
        
        # Compare numeric parts only
        local num1="${ver1[i]//[^0-9]/}"
        local num2="${ver2[i]//[^0-9]/}"
        
        [ -z "$num1" ] && num1=0
        [ -z "$num2" ] && num2=0
        
        if ((10#$num1 > 10#$num2)); then
            return 0
        elif ((10#$num1 < 10#$num2)); then
            return 1
        fi
    done
    
    return 0
}

# Extract version from various formats
extract_version() {
    local input="$1"
    local pattern="${2:-[0-9]+(\.[0-9]+)+}"
    
    # Try to extract version number
    echo "$input" | grep -oE "$pattern" | head -1
}

# Get version with multiple fallback methods
get_package_version() {
    local package="$1"
    local version=""
    
    case "$package" in
        opencv)
            # Try pkg-config first
            if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists opencv4 2>/dev/null; then
                version=$(pkg-config --modversion opencv4 2>/dev/null)
            # Try Python cv2
            elif command -v python3 >/dev/null 2>&1; then
                version=$(python3 -c "import cv2; print(cv2.__version__)" 2>/dev/null || true)
            # Try opencv_version command
            elif command -v opencv_version >/dev/null 2>&1; then
                version=$(opencv_version 2>/dev/null | head -1)
            fi
            ;;
            
        java)
            if command -v java >/dev/null 2>&1; then
                # Try different Java version formats
                version=$(java -version 2>&1 | grep -E "version|openjdk" | head -1 | grep -oE '[0-9]+(\.[0-9]+)+' | head -1)
            fi
            ;;
            
        activemq-broker)
            if [ -x "/opt/apache-activemq/bin/activemq" ]; then
                version=$(/opt/apache-activemq/bin/activemq --version 2>/dev/null | grep -oE '[0-9]+(\.[0-9]+)+' | head -1)
            fi
            ;;
            
        tomee)
            if [ -f "/opt/tomee/.tomee-version" ]; then
                version=$(cat "/opt/tomee/.tomee-version")
            elif [ -d "/opt/tomee/lib" ]; then
                version=$(ls /opt/tomee/lib/tomee-*.jar 2>/dev/null | head -1 | grep -oE '[0-9]+(\.[0-9]+)+' | head -1)
            fi
            ;;
            
        *)
            # Generic version detection
            if command -v "$package" >/dev/null 2>&1; then
                # Try common version flags
                for flag in --version -version -v -V version; do
                    version=$("$package" $flag 2>&1 | grep -oE '[0-9]+(\.[0-9]+)+' | head -1)
                    [ -n "$version" ] && break
                done
            fi
            ;;
    esac
    
    echo "${version:-unknown}"
}

# Global array to track temp directories for cleanup
declare -a TEMP_DIRS_TO_CLEAN=()

# Cleanup function for all temp directories
cleanup_all_temp_dirs() {
    local dir
    for dir in "${TEMP_DIRS_TO_CLEAN[@]}"; do
        if [ -n "$dir" ] && [ -d "$dir" ]; then
            log_info "Cleaning up temporary directory: $dir"
            rm -rf "$dir"
        fi
    done
    TEMP_DIRS_TO_CLEAN=()
}

# Set up cleanup trap once
if [ -z "${CLEANUP_TRAP_SET:-}" ]; then
    trap cleanup_all_temp_dirs EXIT INT TERM
    export CLEANUP_TRAP_SET=1
fi

# Create temporary directory with automatic cleanup
create_temp_dir() {
    local prefix="${1:-pitrac}"
    local temp_dir
    temp_dir=$(mktemp -d "/tmp/${prefix}.XXXXXX")
    
    if [ -z "$temp_dir" ] || [ ! -d "$temp_dir" ]; then
        log_error "Failed to create temporary directory"
        return 1
    fi
    
    # Add to cleanup list
    TEMP_DIRS_TO_CLEAN+=("$temp_dir")
    
    echo "$temp_dir"
    return 0
}

# Create build directory with cleanup
create_build_dir() {
    local build_dir="${1:-/tmp/build}"
    local clean_existing="${2:-1}"
    
    # Clean existing directory if requested
    if [ -d "$build_dir" ] && [ "$clean_existing" = "1" ]; then
        log_info "Cleaning existing build directory: $build_dir"
        rm -rf "$build_dir"
    fi
    
    # Create directory
    mkdir -p "$build_dir"
    
    # Add to cleanup list if it's in /tmp
    if [[ "$build_dir" == /tmp/* ]]; then
        TEMP_DIRS_TO_CLEAN+=("$build_dir")
    fi
    
    echo "$build_dir"
    return 0
}

# Run comprehensive pre-flight checks
run_preflight_checks() {
    local package="${1:-}"
    local skip_checks="${SKIP_PREFLIGHT:-0}"
    
    if [ "$skip_checks" = "1" ]; then
        log_warn "Skipping pre-flight checks (SKIP_PREFLIGHT=1)"
        return 0
    fi
    
    log_info "Running pre-flight checks..."
    local checks_passed=true
    
    # Check internet connectivity
    if ! check_internet; then
        log_error "Internet connectivity check failed"
        checks_passed=false
    else
        log_success "Internet connectivity: OK"
    fi
    
    # Check disk space based on package
    local required_space=100
    case "$package" in
        opencv) required_space=4096 ;;
        libcamera) required_space=2048 ;;
        activemq-broker) required_space=512 ;;
        tomee) required_space=256 ;;
        activemq-cpp) required_space=1024 ;;
    esac
    
    if ! check_disk_space "$required_space"; then
        checks_passed=false
    else
        log_success "Disk space: OK (${required_space}MB required)"
    fi
    
    # Check system requirements
    if ! check_system_requirements "$package"; then
        checks_passed=false
    fi
    
    # Check for required commands
    if ! check_required_commands "$package"; then
        checks_passed=false
    fi
    
    if [ "$checks_passed" = false ]; then
        log_error "Pre-flight checks failed"
        if is_non_interactive; then
            return 1
        fi
        read -p "Continue anyway? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            return 1
        fi
    else
        log_success "All pre-flight checks passed"
    fi
    
    return 0
}

# Check disk space (in MB)
check_disk_space() {
    local required_mb="${1:-100}"
    local path="${2:-.}"
    
    local available_mb
    available_mb=$(df -BM "$path" | awk 'NR==2 {print $4}' | sed 's/M//')
    
    if [ "$available_mb" -lt "$required_mb" ]; then
        log_error "Insufficient disk space: ${available_mb}MB available, ${required_mb}MB required"
        return 1
    fi
    
    return 0
}

# Check internet connectivity
check_internet() {
    local test_hosts=("8.8.8.8" "1.1.1.1")
    
    for host in "${test_hosts[@]}"; do
        if ping -c 1 -W 2 "$host" >/dev/null 2>&1; then
            return 0
        fi
    done
    
    # Try HTTP as fallback
    if command -v curl >/dev/null 2>&1; then
        if curl -fsS --max-time 5 "http://www.google.com/generate_204" >/dev/null 2>&1; then
            return 0
        fi
    elif command -v wget >/dev/null 2>&1; then
        if wget -q --timeout=5 -O /dev/null "http://www.google.com/generate_204" 2>/dev/null; then
            return 0
        fi
    fi
    
    return 1
}

# Check system requirements
check_system_requirements() {
    local package="${1:-}"
    
    # Check if running on Raspberry Pi for Pi-specific packages
    if [[ "$package" =~ ^(system-config|camera-config|pitrac-environment)$ ]]; then
        if ! grep -q "Raspberry Pi" /proc/cpuinfo 2>/dev/null; then
            log_warn "Package $package is designed for Raspberry Pi"
            if is_non_interactive; then
                return 1
            fi
        fi
    fi
    
    # Check CPU cores for compilation
    if [[ "$package" =~ ^(opencv|libcamera|activemq-cpp)$ ]]; then
        local cpu_cores
        cpu_cores=$(nproc 2>/dev/null || echo 1)
        if [ "$cpu_cores" -lt 2 ]; then
            log_warn "Only $cpu_cores CPU core(s) detected. Compilation will be slow."
        else
            log_info "CPU cores: $cpu_cores"
        fi
    fi
    
    # Check memory
    local total_mem
    if command -v free >/dev/null 2>&1; then
        total_mem=$(free -m | awk 'NR==2 {print $2}')
    elif [ -r /proc/meminfo ]; then
        # Fallback to /proc/meminfo if free is not available
        total_mem=$(awk '/MemTotal/ {print int($2/1024)}' /proc/meminfo)
    else
        # If we can't determine memory, skip the check
        log_info "Memory check skipped (unable to determine)"
        total_mem=999999  # Set a high value to pass checks
    fi
    
    if [ -n "$total_mem" ] && [ "$total_mem" -lt 512 ]; then
        log_warn "Low memory detected: ${total_mem}MB. Some installations may fail."
        if [[ "$package" =~ ^(opencv|libcamera)$ ]] && [ "$total_mem" -lt 1024 ]; then
            log_error "Insufficient memory for $package compilation (need at least 1GB)"
            return 1
        fi
    elif [ -n "$total_mem" ]; then
        log_info "Memory: ${total_mem}MB"
    fi
    
    return 0
}

# Check for required commands
check_required_commands() {
    local package="${1:-}"
    local missing_cmds=()
    
    # Basic required commands for all packages
    local basic_cmds=("wget" "curl" "tar" "unzip")
    
    # Package-specific requirements
    case "$package" in
        opencv|libcamera|activemq-cpp)
            basic_cmds+=("git" "cmake" "make" "gcc" "g++")
            ;;
        java|tomee|activemq-broker)
            basic_cmds+=("java")
            ;;
    esac
    
    for cmd in "${basic_cmds[@]}"; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            missing_cmds+=("$cmd")
        fi
    done
    
    if [ ${#missing_cmds[@]} -gt 0 ]; then
        log_warn "Missing required commands: ${missing_cmds[*]}"
        log_info "Installing missing commands..."
        apt_ensure "${missing_cmds[@]}"
    fi
    
    return 0
}

# Progress bar for long operations
show_progress() {
    local current="$1"
    local total="$2"
    local label="${3:-Progress}"
    local width=50
    
    local percentage=$((current * 100 / total))
    local filled=$((percentage * width / 100))
    
    printf "\r%s: [" "$label"
    printf "%${filled}s" | tr ' ' '='
    printf "%$((width - filled))s" | tr ' ' '-'
    printf "] %d%% (%d/%d)" "$percentage" "$current" "$total"
    
    if [ "$current" -eq "$total" ]; then
        echo  # New line when complete
    fi
}

# Spinner for indeterminate progress
show_spinner() {
    local pid=$1
    local label="${2:-Working}"
    local spin='⣾⣽⣻⢿⡿⣟⣯⣷'
    local i=0
    
    while kill -0 $pid 2>/dev/null; do
        i=$(( (i+1) % ${#spin} ))
        printf "\r%s %s " "$label" "${spin:$i:1}"
        sleep 0.1
    done
    
    printf "\r%s Done!    \n" "$label"
}

# Monitor compilation with progress
monitor_compilation() {
    local build_dir="${1:-}"
    local label="${2:-Compiling}"
    local total_files="${3:-0}"
    
    if [ "$total_files" -eq 0 ] && [ -n "$build_dir" ] && [ -d "$build_dir" ]; then
        # Try to estimate total files
        total_files=$(find "$build_dir" -name "*.cpp" -o -name "*.c" -o -name "*.cc" 2>/dev/null | wc -l)
        [ "$total_files" -eq 0 ] && total_files=100  # Default estimate
    fi
    
    local compiled=0
    local start_time=$(date +%s)
    
    while IFS= read -r line; do
        # Check for compilation progress indicators
        if [[ "$line" =~ \[([0-9]+)/([0-9]+)\] ]]; then
            # Format: [X/Y]
            compiled="${BASH_REMATCH[1]}"
            total_files="${BASH_REMATCH[2]}"
            show_progress "$compiled" "$total_files" "$label"
        elif [[ "$line" =~ \[\ *([0-9]+)%\] ]]; then
            # Format: [X%]
            local percent="${BASH_REMATCH[1]}"
            if [ "$total_files" -gt 0 ]; then
                compiled=$((total_files * percent / 100))
            else
                compiled="$percent"
                total_files="100"
            fi
            
            show_progress "$compiled" "$total_files" "$label"
        elif [[ "$line" =~ Building|Compiling|Linking ]]; then
            ((compiled++))
            show_progress "$compiled" "$total_files" "$label"
        elif [[ "$line" =~ error:|Error:|ERROR: ]]; then
            # Show errors in red
            echo -e "\n${RED}$line${NC}"
        elif [[ "$line" =~ warning:|Warning:|WARNING: ]]; then
            # Show warnings in yellow (optional)
            [ "${SHOW_WARNINGS:-0}" = "1" ] && echo -e "\n${YELLOW}$line${NC}"
        fi
        
        # Save to log if specified
        [ -n "${BUILD_LOG:-}" ] && echo "$line" >> "$BUILD_LOG"
    done
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    echo  # New line after progress
    log_success "$label completed in $(format_duration $duration)"
}

# Format duration in human-readable form
format_duration() {
    local seconds=$1
    local hours=$((seconds / 3600))
    local minutes=$(((seconds % 3600) / 60))
    local secs=$((seconds % 60))
    
    if [ $hours -gt 0 ]; then
        printf "%dh %dm %ds" $hours $minutes $secs
    elif [ $minutes -gt 0 ]; then
        printf "%dm %ds" $minutes $secs
    else
        printf "%ds" $secs
    fi
}

# Run command with progress monitoring
run_with_progress() {
    local cmd="$1"
    local label="${2:-Running}"
    local log_file="${3:-}"
    
    log_info "$label: $cmd"
    
    if [ -n "$log_file" ]; then
        # Run with logging
        ( $cmd 2>&1 | tee "$log_file" | monitor_compilation "" "$label" ) &
    else
        # Run without logging
        ( $cmd 2>&1 | monitor_compilation "" "$label" ) &
    fi
    
    local cmd_pid=$!
    
    # Wait for command to complete
    wait $cmd_pid
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        log_success "$label completed successfully"
    else
        log_error "$label failed with exit code $exit_code"
        [ -n "$log_file" ] && log_error "Check log file: $log_file"
    fi
    
    return $exit_code
}

# Download with progress
download_with_progress() {
    local url="$1"
    local output="${2:-}"
    local label="${3:-Downloading}"
    
    if [ -z "$output" ]; then
        output=$(basename "$url")
    fi
    
    log_info "$label from: $url"
    
    if command -v wget >/dev/null 2>&1; then
        {
            wget --progress=bar:force -O "$output" "$url" 2>&1 | \
                grep --line-buffered "%" | \
                sed -u 's/.*\([0-9]\+\)%.*/\1/' | \
                while read percent; do
                    show_progress "$percent" "100" "$label"
                done
        } || true
    elif command -v curl >/dev/null 2>&1; then
        {
            curl -# -L -o "$output" "$url" 2>&1 | \
                tr '\r' '\n' | \
                sed -n 's/.*\([0-9]\+\.[0-9]\+\)%.*/\1/p' | \
                while read percent; do
                    percent=${percent%.*}
                    show_progress "$percent" "100" "$label"
                done
        } || true
    else
        log_error "Neither wget nor curl found"
        return 1
    fi
    
    echo  # New line after progress
    
    if [ -f "$output" ]; then
        log_success "$label completed: $output"
        return 0
    else
        log_error "$label failed"
        return 1
    fi
}

# detect_directory "relative/path" "fallback/path" "base1" "base2" ...
detect_directory() {
  local target_path="$1"
  local fallback="$2"
  shift 2
  local search_bases=("$@")
  
  local current_dir="${DETECT_FROM_DIR:-$PWD}"
  local check_dir="$current_dir"
  
  # Walk up directory tree
  while [ "$check_dir" != "/" ]; do
    if [ -d "$check_dir/$target_path" ]; then
      echo "$check_dir/$target_path"
      return 0
    fi
    check_dir="$(dirname "$check_dir")"
  done
  
  # Check base directories
  for base in "${search_bases[@]}"; do
    if [ -d "$base/$target_path" ]; then
      echo "$base/$target_path"
      return 0
    fi
  done
  
  echo "$fallback"
}

detect_pitrac_root() {
  detect_directory \
    "Software/LMSourceCode" \
    "/home/$(whoami)/Dev/PiTrac/Software/LMSourceCode" \
    "/work" \
    "$HOME/dev/personal/PiTrac" \
    "$HOME/Dev/PiTrac" \
    "/home/pi/Dev/PiTrac"
}

detect_lm_shares_dir() {
  local subdir="${1:-Images}"
  
  # Check relative to PiTrac
  local pitrac_source="$(detect_pitrac_root)"
  if [ -n "$pitrac_source" ] && [ -d "$pitrac_source" ]; then
    local pitrac_base="$(dirname "$(dirname "$pitrac_source")")"
    local parent="$(dirname "$pitrac_base")"
    
    if [ -d "$parent/LM_Shares/$subdir" ]; then
      echo "$parent/LM_Shares/$subdir"
      return 0
    fi
  fi
  
  detect_directory \
    "LM_Shares/$subdir" \
    "/home/$(whoami)/LM_Shares/$subdir" \
    "/work" \
    "$HOME" \
    "/home/pi"
}

# Detect Raspberry Pi model
detect_pi_model() {
    if grep -q "Raspberry Pi 5" /proc/cpuinfo 2>/dev/null; then
        echo "5"
    elif grep -q "Raspberry Pi 4" /proc/cpuinfo 2>/dev/null; then
        echo "4"
    else
        echo "unknown"
    fi
}

# Get CPU cores for compilation
get_cpu_cores() {
    local cores
    cores=$(nproc 2>/dev/null || echo 1)
    # Limit to avoid memory issues on Pi
    if [ "$cores" -gt 4 ]; then
        cores=4
    fi
    echo "$cores"
}

# Load defaults from YAML file
load_defaults() {
    local script_name="$1"
    local defaults_file="${SCRIPT_DIR}/defaults/${script_name}.yaml"
    
    # Always load defaults (for both interactive and non-interactive)
    if [ -f "$defaults_file" ]; then
        # Check if running in non-interactive mode
        if [[ " $* " =~ " --non-interactive " ]] || [ "${NON_INTERACTIVE:-0}" = "1" ]; then
            export NON_INTERACTIVE=1
            log_info "Running in non-interactive mode, using defaults from $defaults_file"
        fi
        
        # Parse simple YAML (key: value format)
        while IFS=': ' read -r key value || [ -n "$key" ]; do
            # Skip comments and empty lines
            [[ "$key" =~ ^#.*$ ]] && continue
            [ -z "$key" ] && continue
            
            # Clean up the value (remove quotes and leading/trailing spaces)
            value="${value#\"}"
            value="${value%\"}"
            value="${value#\'}"
            value="${value%\'}"
            value="$(echo "$value" | xargs)"
            
            # Convert key to uppercase and replace - with _
            var_name="${key^^}"
            var_name="${var_name//-/_}"
            
            # Export as environment variable if not already set
            # This provides defaults for both interactive and non-interactive modes
            if [ -z "${!var_name:-}" ]; then
                export "$var_name=$value"
                [ "${NON_INTERACTIVE:-0}" = "1" ] && log_info "Using default: $var_name=$value"
            fi
        done < "$defaults_file" || true
    else
        if [ "${NON_INTERACTIVE:-0}" = "1" ]; then
            log_error "Non-interactive mode requires defaults file: $defaults_file"
            return 1
        fi
    fi
}

# Check if running non-interactively
is_non_interactive() {
    [ "${NON_INTERACTIVE:-0}" = "1" ]
}

# Prompt with fallback for non-interactive mode
prompt_or_default() {
    local prompt="$1"
    local default="$2"
    local var_name="$3"
    
    if is_non_interactive; then
        # In non-interactive mode, use the default
        echo "${!var_name:-$default}"
    else
        # Interactive mode - prompt user
        read -p "$prompt [$default]: " result
        echo "${result:-$default}"
    fi
}