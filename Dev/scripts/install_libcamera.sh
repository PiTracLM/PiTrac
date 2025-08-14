#!/usr/bin/env bash
set -euo pipefail

# Libcamera and RpiCam Apps Installation Script
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Load defaults from config file
load_defaults "libcamera" "$@"

REQUIRED_RPICAM_APPS_VERSION="${REQUIRED_RPICAM_APPS_VERSION:-1.5.3}"
BUILD_FROM_SOURCE="${BUILD_FROM_SOURCE:-1}"
BUILD_DIR="${BUILD_DIR:-/tmp/rpicam_build}"
CPU_CORES="${CPU_CORES:-$(get_cpu_cores)}"


# Check if libcamera is installed
is_libcamera_installed() {
  # Try common pkg-config names
  if pkg-config --exists libcamera 2>/dev/null; then
    return 0
  fi
  if pkg-config --exists libcamera-base 2>/dev/null; then
    return 0
  fi
  # Fallback: look for headers as a last resort
  [ -e /usr/include/libcamera/libcamera.h ] || [ -e /usr/local/include/libcamera/libcamera.h ]
}

rpicam_apps_meet_requirement() {
  if ! need_cmd rpicam-still; then
    return 1
  fi
  local installed ver
  installed="$(rpicam-still --version 2>/dev/null | awk '/rpicam-apps build/ {print $3}')"
  ver="${installed#v}"
  [ -n "$ver" ] && version_ge "$ver" "$REQUIRED_RPICAM_APPS_VERSION"
}

# Installation functions
install_libraries() {
  # Run pre-flight checks
  run_preflight_checks "libcamera" || return 1

  echo "Ensuring prerequisite libraries..."
  apt_ensure \
    git \
    python3 \
    python3-pip \
    python3-graphviz \
    python3-sphinx \
    python3-yaml \
    python3-ply \
    python3-jinja2 \
    doxygen \
    libevent-dev \
    pybind11-dev \
    libavdevice-dev \
    qtbase5-dev libqt5core5a libqt5gui5 libqt5widgets5 \
    meson \
    cmake \
    ninja-build \
    pkg-config \
    build-essential \
    libglib2.0-dev \
    libgstreamer-plugins-base1.0-dev \
    unzip \
    wget \
    ca-certificates
}

install_libcamera() {
  # Run pre-flight checks
  run_preflight_checks "libcamera" || return 1

  if have_libcamera; then
    echo "libcamera already present (pkg-config found it). Skipping build."
    return 0
  fi

  echo "Downloading and building libcamera..."
  local WORK
  WORK="$(create_temp_dir "rpi-build")"
  cd "$WORK"

  git clone https://github.com/raspberrypi/libcamera.git
  cd libcamera

  export PKEXEC_UID="${PKEXEC_UID:-99999}"

  echo "Meson setup..."
  meson setup build \
    --buildtype=release \
    -Dpipelines=rpi/vc4,rpi/pisp \
    -Dipas=rpi/vc4,rpi/pisp \
    -Dv4l2=enabled \
    -Dgstreamer=enabled \
    -Dtest=false \
    -Dlc-compliance=disabled \
    -Dcam=disabled \
    -Dqcam=disabled \
    -Ddocumentation=disabled \
    -Dpycamera=enabled

  echo "Ninja build..."
  ninja -C build

  log_info "Installing libcamera..."
  $SUDO ninja -C build install
  echo "libcamera installed."
}

install_rpicam_apps() {
  # Run pre-flight checks
  run_preflight_checks "libcamera" || return 1

  if rpicam_apps_meet_requirement; then
    echo "rpicam-apps already meets requirement (>= ${REQUIRED_RPICAM_APPS_VERSION}). Skipping build."
    return 0
  fi

  log_info "Installing rpicam pre-required libraries..."
  apt_ensure libboost-program-options-dev libdrm-dev libexif-dev

  echo "Downloading and building rpicam-apps..."
  local WORK
  WORK="$(create_temp_dir "rpi-build")"
  cd "$WORK"

  git clone https://github.com/raspberrypi/rpicam-apps.git
  cd rpicam-apps

  echo "Meson setup for rpicam-apps..."
  meson setup build \
    -Denable_libav=enabled \
    -Denable_drm=enabled \
    -Denable_egl=enabled \
    -Denable_qt=enabled \
    -Denable_opencv=enabled \
    -Denable_tflite=disabled \
    -Denable_hailo=disabled

  echo "Compiling rpicam-apps..."
  meson compile -C build

  log_info "Installing rpicam-apps..."
  $SUDO meson install -C build
  $SUDO ldconfig 2>/dev/null || true

  echo "rpicam-apps installed."
}

verify_installation() {
  log_info "Verifying rpicam-apps installation..."
  if ! need_cmd rpicam-still; then
    log_error "rpicam-still not found on PATH."
    return 2
  fi

  local installed ver
  installed="$(rpicam-still --version 2>/dev/null | awk '/rpicam-apps build/ {print $3}')"
  ver="${installed#v}"

  if [ -z "$ver" ]; then
    log_warn "Could not determine rpicam-apps version."
    return 0
  fi

  if version_ge "$ver" "$REQUIRED_RPICAM_APPS_VERSION"; then
    log_success "OK: rpicam-apps $ver >= required $REQUIRED_RPICAM_APPS_VERSION"
    return 0
  else
    log_error "rpicam-apps $ver < required $REQUIRED_RPICAM_APPS_VERSION"
    return 1
  fi
}

# Main installation
install_libcamera_full() {
  # Run pre-flight checks
  run_preflight_checks "libcamera" || return 1

  install_libraries
  install_libcamera
  install_rpicam_apps
  verify_installation
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  install_libcamera_full
fi
