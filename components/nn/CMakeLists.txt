cmake_minimum_required(VERSION 3.24)
project(nn)

set(src
    src/nn/allocator.c
    src/nn/endian.c
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
target_include_directories(nn PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_sources(nn PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
        include/nn/allocator.h
        include/nn/endian.h
        include/nn/ip_address.h
        include/nn/llist.h
        include/nn/log.h
        include/nn/set.h
        include/nn/string.h
        include/nn/string_int_map.h
        include/nn/string_map.h
        include/nn/string_set.h
        include/nn/vector.h
)

install(TARGETS nn
        EXPORT nn
        FILE_SET public_headers)
install(EXPORT nn
        FILE nnTargets.cmake
        DESTINATION lib/cmake/
        NAMESPACE NabtoCommon)
