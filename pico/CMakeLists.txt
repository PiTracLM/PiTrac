# PiTrac Pico firmware — strobe driver + acoustic impact detection
#
# Target: original Raspberry Pi Pico (RP2040). Build against pico-sdk 2.2.0.
#
# Build (Linux, with pico-sdk installed via apt):
#   mkdir build && cd build
#   cmake -DPICO_BOARD=pico ..
#   ninja
#
# Build (from-source pico-sdk):
#   export PICO_SDK_PATH=/path/to/pico-sdk
#   mkdir build && cd build
#   cmake -DPICO_BOARD=pico -G Ninja ..
#   ninja
#
# Output:  build/pitrac_pico.uf2  — drag-drop to RPI-RP2 bootloader, or
#          picotool load build/pitrac_pico.uf2 -fx

cmake_minimum_required(VERSION 3.13)

option(PICO_TESTS "Build host-side unit tests (no pico-sdk required)" OFF)

if(PICO_TESTS)
    project(pitrac_pico_tests LANGUAGES C)
    enable_testing()
    add_subdirectory(tests)
    return()
endif()

# Default to release; this is performance-sensitive firmware (DSP + tight timing)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Board is `pico_w` for the original Pico W (RP2040 + CYW43 WiFi). On Pico W
# the onboard LED, GP23, GP24, and GP25 are wired to the CYW43 chip — not
# directly to those header pins like on a plain Pico — so we have to route
# LED control through the CYW43 SPI driver. Use `-DPICO_BOARD=pico` for the
# non-W board, or `-DPICO_BOARD=pico2` for RP2350 (NOT yet supported).
#
# This MUST be set before include(pico_sdk_import.cmake) — the import shim
# caches PICO_BOARD with its own default if we don't get there first.
set(PICO_BOARD pico_w CACHE STRING "Pico board type")

# Pull in the SDK import shim. PICO_SDK_PATH must be set in env or via cmake -D.
# On Debian/Ubuntu with `pico-sdk-source` installed, it lives at /usr/share/pico-sdk
# and the shim auto-discovers it; on a from-source clone, export PICO_SDK_PATH.
include(pico_sdk_import.cmake)

project(pitrac_pico
  LANGUAGES C CXX ASM
  VERSION 0.1.0
  DESCRIPTION "PiTrac strobe driver + acoustic impact detection firmware"
)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ------------------------------------------------------------------ pio assets
# Each .pio program is preprocessed into a header by pioasm at build time.
# pico_generate_pio_header() makes the .pio.h available on the include path.
add_executable(pitrac_pico
  src/main.c
  src/core1_usb.c
  src/proto_parser.c
  src/cmd_dispatcher.c
  src/impact_detect.c
  src/strobe.c
  src/ring_buffer.c
)

pico_generate_pio_header(pitrac_pico ${CMAKE_CURRENT_LIST_DIR}/pio/i2s_rx.pio)
pico_generate_pio_header(pitrac_pico ${CMAKE_CURRENT_LIST_DIR}/pio/ir_strobe.pio)

target_include_directories(pitrac_pico PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${CMAKE_CURRENT_LIST_DIR}/src
)

# Link everything we need from the SDK. Multicore is for the core1 USB worker;
# pio + dma drive the I2S receive and strobe output; clocks lets us pin the
# system PLL for deterministic PIO divisors.
target_link_libraries(pitrac_pico PRIVATE
  pico_stdlib
  pico_multicore
  pico_unique_id
  pico_bootrom
  hardware_pio
  hardware_dma
  hardware_irq
  hardware_clocks
  hardware_gpio
  hardware_sync
  hardware_watchdog
  hardware_adc
)

# On Pico W, the LED + GP23/24/25 are routed through the CYW43 WiFi chip; we
# need its SPI driver to even toggle the LED. The `_none` variant gives us
# GPIO control with no lwIP / WiFi stack — keeps the binary small. Linked
# conditionally so plain-Pico builds don't pull it in. Plain `if()` instead
# of a generator expression so the INTERFACE include dirs propagate at
# configure time.
if(PICO_BOARD STREQUAL "pico_w")
    target_link_libraries(pitrac_pico PRIVATE pico_cyw43_arch_none)
endif()

# stdio → USB CDC (TinyUSB ships with the SDK now — no external dep needed).
# UART stdio off; we use the UART pins for nothing here, no point printing twice.
pico_enable_stdio_usb(pitrac_pico  1)
pico_enable_stdio_uart(pitrac_pico 0)

# Non-blocking USB so the firmware boots even with no host attached. Otherwise
# `printf()` blocks for ~5 s waiting for DTR on first boot — not great if the
# Pico boots faster than the Pi 5's USB stack enumerates.
target_compile_definitions(pitrac_pico PRIVATE
  PICO_STDIO_USB_CONNECT_TIMEOUT_MS=0
  # Drop the 1-second "wait for DTR" — we want events to flow the moment the
  # host opens the CDC port.
  PICO_STDIO_USB_DEFAULT_CRLF=0
  # Activate tinyusb's RP2040 device-enumeration workaround. The fix function
  # is already linked in via pico_stdio_usb → tinyusb_device_unmarked →
  # pico_fix_rp2040_usb_device_enumeration, but the SDK's tinyusb CMakeLists
  # leaves PICO_RP2040_USB_DEVICE_ENUMERATION_FIX off by default — so the
  # bus-reset IRQ never calls the patch. Without this define, the host sees
  # `device descriptor read/64, error -110` and the Pico never enumerates as
  # ttyACM. The fix corrects a silicon timing quirk in the USB hardware's
  # response to certain hosts (notably Linux xhci on the Pi 5).
  PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
  # USB descriptor strings — what `lsusb` / pyserial.tools.list_ports report.
  # Lets the Pi-side discovery code match on "PiTrac" instead of the generic
  # "Raspberry Pi"/"Pico" defaults, which would clash with any other Pico in
  # the system (e.g. a debugprobe). VID/PID kept at the SDK defaults so the
  # kernel's cdc_acm driver claims the interface without udev quirks and
  # picotool / reset-via-vendor-interface keep working.
  USBD_MANUFACTURER="PiTrac"
  USBD_PRODUCT="PiTrac Pico Strobe"
)

# Optimisation flags. The DSP loop is hot; -O2 keeps the binary small enough to
# fit in RP2040's 264 KB SRAM without playing games. Don't use -O3 — bigger
# code blows the icache on the Cortex-M0+ and actually slows you down.
target_compile_options(pitrac_pico PRIVATE
  -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
  -O2
)

# Produce all the deliverables (.uf2, .elf, .bin, .hex, .map, .dis).
pico_add_extra_outputs(pitrac_pico)

# Friendly summary at configure time so it's obvious what we built against.
message(STATUS "PiTrac Pico firmware:")
message(STATUS "  pico-sdk path:  ${PICO_SDK_PATH}")
message(STATUS "  board:          ${PICO_BOARD}")
message(STATUS "  build type:     ${CMAKE_BUILD_TYPE}")
