cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(test_proto_parser
    test_proto_parser.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/proto_parser.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/cmd_dispatcher.c
)

target_include_directories(test_proto_parser PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../include
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

target_compile_options(test_proto_parser PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Werror
)

add_test(NAME proto_parser COMMAND test_proto_parser)

add_executable(test_impact_detect
    test_impact_detect.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/impact_detect.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/ring_buffer.c
)

target_include_directories(test_impact_detect PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../include
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

target_compile_options(test_impact_detect PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Werror
)

add_test(NAME impact_detect COMMAND test_impact_detect)

add_executable(test_strobe_compile
    test_strobe_compile.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/strobe_compile.c
)

target_include_directories(test_strobe_compile PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../include
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

target_compile_options(test_strobe_compile PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Werror
)

add_test(NAME strobe_compile COMMAND test_strobe_compile)

add_executable(test_arm_gate
    test_arm_gate.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/arm_gate.c
)

target_include_directories(test_arm_gate PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../include
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

target_compile_options(test_arm_gate PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Werror
)

add_test(NAME arm_gate COMMAND test_arm_gate)
