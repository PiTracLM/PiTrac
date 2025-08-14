#!/usr/bin/env bash
set -euo pipefail

# MessagePack C++ Installation Script
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Load defaults from config file
load_defaults "msgpack" "$@"

MSGPACK_VERSION="${MSGPACK_VERSION:-6.1.1}"
INSTALL_METHOD="${INSTALL_METHOD:-apt}"
BUILD_DIR="${BUILD_DIR:-/tmp/msgpack_build}"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
FORCE="${FORCE:-0}"


is_msgpack_installed() {
  # header-only lib usually installs these headers
  [ -f /usr/local/include/msgpack.hpp ] || [ -f /usr/include/msgpack.hpp ] && return 0
  # some distros lay out headers under msgpack/… only
  [ -f /usr/local/include/msgpack/msgpack.hpp ] || [ -f /usr/include/msgpack/msgpack.hpp ] && return 0
  return 1
}

verify_msgpack() {
  # Compile a tiny test that prints: [1,true,"example"]
  local tmpdir; tmpdir="$(create_temp_dir "msgpack.verify")"
  cat > "${tmpdir}/test.cpp" <<'CPP'
#include <iostream>
#include <vector>
#include <string>
#include <msgpack.hpp>
int main() {
  std::vector<msgpack::type::variant> v;
  v.emplace_back(1);
  v.emplace_back(true);
  v.emplace_back(std::string("example"));
  msgpack::sbuffer sbuf;
  msgpack::pack(sbuf, v);
  msgpack::object_handle oh = msgpack::unpack(sbuf.data(), sbuf.size());
  std::cout << oh.get() << std::endl; // prints as JSON-ish
  return 0;
}
CPP
  g++ -std=c++20 -O2 -Wall -Wextra "${tmpdir}/test.cpp" -o "${tmpdir}/test"
  local out; out="$("${tmpdir}/test")"
  rm -rf "$tmpdir"
  [ "$out" = '[1,true,"example"]' ]
}

# Install MessagePack C++
install_msgpack() {
  # Run pre-flight checks
  run_preflight_checks "msgpack" || return 1

  # Check if already installed and skip if FORCE not set
  if [ "$FORCE" != "1" ] && have_msgpack; then
    echo "msgpack headers already present. Verifying..."
    if verify_msgpack; then
      echo "msgpack already installed and working — skipping."
      return 0
    else
      echo "msgpack headers found but verification failed; will rebuild."
    fi
  fi

  echo "Ensuring prerequisite build tools..."
  apt_ensure build-essential cmake unzip wget ca-certificates g++

  # Build from source
  local WORK
  WORK="$(create_temp_dir "msgpack")"
  cd "$WORK"

  echo "Fetching msgpack-c (cpp_master)..."
  download_with_progress "https://github.com/msgpack/msgpack-c/archive/refs/heads/cpp_master.zip" "cpp_master.zip"

  echo "Extracting source..."
  unzip -q cpp_master.zip
  local SRC_DIR="${WORK}/msgpack-c-cpp_master"
  
  if [ ! -d "$SRC_DIR" ]; then
    log_error "expected source dir not found."
    return 1
  fi

  echo "Configuring with CMake..."
  cmake -S "$SRC_DIR" -B "$SRC_DIR/build" -DMSGPACK_CXX20=ON

  echo "Building..."
  cmake --build "$SRC_DIR/build" -j"$(nproc)"

  log_info "Installing to /usr/local..."
  $SUDO cmake --install "$SRC_DIR/build"
  $SUDO ldconfig 2>/dev/null || true

  echo "Verifying installation..."
  if verify_msgpack; then
    echo "MessagePack C++ successfully installed and verified!"
  else
    log_error "MessagePack C++ install/verify failed."
    return 1
  fi
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  install_msgpack
fi
