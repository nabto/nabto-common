cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0167 NEW)
project(nabto_streaming_ci_build)

option(NABTO_STREAMING_SANITIZE "Build the library and tests with AddressSanitizer and UndefinedBehaviorSanitizer" ON)
if(NABTO_STREAMING_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../nn nn)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. nabto_stream)

find_package(
  Boost
  COMPONENTS unit_test_framework
  REQUIRED)

find_package(Threads REQUIRED)

set(test_dir ${CMAKE_CURRENT_SOURCE_DIR}/../test)

set(CMAKE_CXX_STANDARD 14)
set(test_src
${test_dir}/packet_test.cpp
${test_dir}/unit_test.cpp)

add_executable(streaming_test "${test_src}")
target_link_libraries(streaming_test Boost::unit_test_framework
                      nabto_stream
                      Threads::Threads)

install(TARGETS streaming_test RUNTIME DESTINATION bin)
