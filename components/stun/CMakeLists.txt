cmake_minimum_required(VERSION 3.2)
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
target_link_libraries(nabto_stun_client nn)

target_include_directories(nabto_stun_client PUBLIC include)
