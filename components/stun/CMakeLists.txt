cmake_minimum_required(VERSION 3.10)
project(nabto_stun)
option(STUN_BUILD_TESTS "build stun test program" OFF)

add_compile_options(-Wall -Wextra -pedantic)

set(src
  src/nabto_stun_client.c
  src/nabto_stun_message.c
  )

add_library(nabto_stun_client "${src}")
target_link_libraries(nabto_stun_client nn)

target_include_directories(nabto_stun_client PUBLIC include)
