cmake_minimum_required(VERSION 3.2)
project(nn)

set(src
    src/nn/vector.c
    src/nn/string_set.c
    src/nn/log.c
    src/nn/llist.c
    src/nn/string_map.c
    src/nn/string_int_map.c
    src/nn/set.c
    src/nn/ip_address.c
    src/nn/string.c)

add_library(nn "${src}")
target_include_directories(nn PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
