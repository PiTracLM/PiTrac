# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2022-2026, Verdant Consultants, LLC.
#
# Standalone Boost.Test build for unit tests that exercise pure pieces of
# the ImageProcessing pipeline (no OpenCV, no rpicam, no lgpio).
#
# Build:
#   cd Software/LMSourceCode/ImageProcessing/tests
#   cmake -B build
#   cmake --build build
#   ctest --test-dir build --output-on-failure

cmake_minimum_required(VERSION 3.16)
project(ImageProcessingTests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Boost REQUIRED COMPONENTS unit_test_framework)

enable_testing()

# ---- test_pico_strobe_client ----
add_executable(test_pico_strobe_client
    test_pico_strobe_client.cpp
    ../pico_strobe_client.cpp
)

target_include_directories(test_pico_strobe_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(TARGET Boost::unit_test_framework)
    target_link_libraries(test_pico_strobe_client PRIVATE Boost::unit_test_framework)
    target_compile_definitions(test_pico_strobe_client PRIVATE BOOST_TEST_DYN_LINK)
else()
    target_link_libraries(test_pico_strobe_client PRIVATE ${Boost_LIBRARIES})
    target_include_directories(test_pico_strobe_client PRIVATE ${Boost_INCLUDE_DIRS})
    target_compile_definitions(test_pico_strobe_client PRIVATE BOOST_TEST_DYN_LINK)
endif()

target_compile_options(test_pico_strobe_client PRIVATE -Wall -Wextra -Wpedantic)

# Link lgpio when present so the BCM 26 fast path compiles on the Pi. The
# header guard inside pico_strobe_client.cpp handles dev machines that do not
# have lgpio.
find_library(LGPIO_LIB lgpio)
if(LGPIO_LIB)
    target_link_libraries(test_pico_strobe_client PRIVATE ${LGPIO_LIB})
endif()

add_test(NAME PicoStrobeClientTests COMMAND test_pico_strobe_client)
set_tests_properties(PicoStrobeClientTests PROPERTIES
    TIMEOUT 30
    LABELS "unit;pico"
)
