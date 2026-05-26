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
