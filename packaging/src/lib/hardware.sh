#!/usr/bin/env bash
# lib/hardware.sh - Hardware detection and management functions

detect_pi_model() {
  if grep -q "Raspberry Pi.*5" /proc/cpuinfo 2>/dev/null; then
    echo "pi5"
  elif grep -q "Raspberry Pi.*4" /proc/cpuinfo 2>/dev/null; then
    echo "pi4"
  elif grep -q "Raspberry Pi" /proc/cpuinfo 2>/dev/null; then
    echo "pi_other"
  else
    echo "unknown"
  fi
}

get_gpio_chip() {
  local model="${1:-$(detect_pi_model)}"
  
  case "$model" in
    pi5)
      echo "4"
      ;;
    pi4|pi_other)
      echo "0"
      ;;
    *)
      echo "0"
      ;;
  esac
}

get_boot_config_path() {
  local model="${1:-$(detect_pi_model)}"
  
  if [[ -f "/boot/firmware/config.txt" ]]; then
    echo "/boot/firmware/config.txt"
  elif [[ -f "/boot/config.txt" ]]; then
    echo "/boot/config.txt"
  else
    echo "/boot/firmware/config.txt"
  fi
}

get_libcamera_config_path() {
  local model="${1:-$(detect_pi_model)}"
  
  case "$model" in
    pi5)
      echo "/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml"
      ;;
    pi4|pi_other)
      echo "/usr/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml"
      ;;
    *)
      if [[ -f "/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml" ]]; then
        echo "/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml"
      else
        echo "/usr/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml"
      fi
      ;;
  esac
}

is_gpio_available() {
  local chip=$(get_gpio_chip)
  
  if [[ -e "/dev/gpiochip${chip}" ]]; then
    return 0
  else
    return 1
  fi
}

get_camera_command() {
  local model="${1:-$(detect_pi_model)}"
  local base_command="${2:-hello}"
  
  case "$model" in
    pi5)
      echo "rpicam-${base_command}"
      ;;
    *)
      echo "libcamera-${base_command}"
      ;;
  esac
}

is_raspberry_pi() {
  if [[ -f /proc/cpuinfo ]] && grep -q "Raspberry Pi" /proc/cpuinfo; then
    return 0
  else
    return 1
  fi
}

get_system_memory_mb() {
  local mem_kb
  mem_kb=$(grep MemTotal /proc/meminfo | awk '{print $2}')
  echo $((mem_kb / 1024))
}

needs_gpu_memory_setting() {
  local model="${1:-$(detect_pi_model)}"
  
  case "$model" in
    pi4)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

get_recommended_gpu_memory() {
  local model="${1:-$(detect_pi_model)}"
  
  case "$model" in
    pi4)
      echo "256"
      ;;
    *)
      echo "0"
      ;;
  esac
}

check_camera_availability() {
  local camera_cmd=$(get_camera_command)
  
  if command -v "$camera_cmd" >/dev/null 2>&1; then
    if $camera_cmd --list-cameras 2>/dev/null | grep -q "Available cameras"; then
      return 0
    fi
  fi
  
  return 1
}

get_camera_slots() {
  local camera_cmd=$(get_camera_command)
  
  if command -v "$camera_cmd" >/dev/null 2>&1; then
    $camera_cmd --list-cameras 2>/dev/null | grep -E "^\s*[0-9]+ :" | while read -r line; do
      echo "$line"
    done
  fi
}


# Determines if running on a single pi by determining how many cameras are attached
# Returns 0 if this is a single-pi setup (with 2 cameras on the same pi) or 1 if not
is_single_pi() {

    LINE_COUNT=$(rpicam-hello --list 2>/dev/null | grep -E "^\s*[0-9]+ :" | wc -l )

    if [[ $LINE_COUNT == "2" ]]; then
        return 0
    else
        return 1
    fi
}
