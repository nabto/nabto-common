cmake_minimum_required(VERSION 3.24)
project(nabto_stun)
option(STUN_BUILD_TESTS "build stun test program" OFF)

if (UNIX)
  add_compile_options(-Wall -Wextra -pedantic)
endif()

set(src
  src/nabto_stun_client.c
  src/nabto_stun_message.c
  src/nabto_stun_util.c
  )

add_library(nabto_stun_client "${src}")
add_library(NabtoCommon::nabto_stun_client ALIAS nabto_stun_client)
target_link_libraries(nabto_stun_client NabtoCommon::nn)

target_include_directories(nabto_stun_client PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_sources(nabto_stun_client PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
        include/stun/nabto_stun_client.h
        include/stun/nabto_stun_defines.h
        include/stun/nabto_stun_message.h
        include/stun/nabto_stun_types.h
)
