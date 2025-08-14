#!/usr/bin/env bash
set -euo pipefail

# ActiveMQ C++ CMS Installation Script
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Load defaults from config file
load_defaults "activemq-cpp" "$@"

GIT_REPO="${GIT_REPO:-https://gitbox.apache.org/repos/asf/activemq-cpp.git}"
BUILD_DIR="${BUILD_DIR:-/tmp/activemq-cpp-build}"
CPU_CORES="${CPU_CORES:-$(get_cpu_cores)}"
RUN_TESTS="${RUN_TESTS:-0}"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
FORCE="${FORCE:-0}"


# Check if activemq cpp is installed
is_activemq_cpp_installed() {
  # Check if libactivemq-cpp is available via ldconfig
  if ldconfig -p 2>/dev/null | grep -q "libactivemq-cpp"; then
    return 0
  fi

  # Fallback: check if the header exists
  if [ -f "/usr/local/include/activemq/library/ActiveMQCPP.h" ] || \
     [ -f "/usr/include/activemq/library/ActiveMQCPP.h" ]; then
    return 0
  fi

  return 1
}

# Install ActiveMQ C++ CMS
install_activemq_cpp() {
  # Run pre-flight checks
  run_preflight_checks "activemq_cpp_cms" || return 1

  if is_activemq_cpp_installed; then
    echo "ActiveMQ-CPP already installed. Skipping build."
    return 0
  fi

  log_info "Installing prerequisites..."
  apt_ensure \
    autoconf \
    automake \
    libtool \
    pkg-config \
    build-essential \
    libssl-dev \
    libapr1-dev \
    libaprutil1-dev \
    libcppunit-dev \
    uuid-dev \
    doxygen \
    git

  # Build ActiveMQ-CPP
  local WORK_DIR
  WORK_DIR="$(create_temp_dir "activemq-cpp")"
  cd "$WORK_DIR"

  echo "Cloning ActiveMQ-CPP..."
  git clone https://gitbox.apache.org/repos/asf/activemq-cpp.git
  cd activemq-cpp/activemq-cpp

  echo "Configuring..."
  ./autogen.sh
  ./configure

  log_info "Building..."
  run_with_progress "make -j$(get_cpu_cores)" "Building activemq-cpp" "/tmp/activemq_cpp_build.log"

  log_info "Installing..."
  $SUDO make install
  $SUDO ldconfig 2>/dev/null || true

  log_info "Generating Doxygen docs..."
  make doxygen-run || echo "Doxygen documentation generation skipped."

  log_info "Running unit tests..."
  make check || echo "Unit tests skipped or failed."

  echo "ActiveMQ-CPP installation complete."
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  install_activemq_cpp
fi
