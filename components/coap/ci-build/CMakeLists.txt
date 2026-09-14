cmake_minimum_required(VERSION 3.30)
project(nabto_coap_ci_build)

option(NABTO_COAP_SANITIZE "Build the library and tests with AddressSanitizer and UndefinedBehaviorSanitizer" ON)
if(NABTO_COAP_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../nn nn)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. nabto_coap)

find_package(
  Boost
  COMPONENTS unit_test_framework
  REQUIRED)

find_package(Threads REQUIRED)

set(test_dir ${CMAKE_CURRENT_SOURCE_DIR}/../test)

set(CMAKE_CXX_STANDARD 14)
set(test_src
${test_dir}/coap_server_test.cpp
${test_dir}/unit_test.cpp)

add_executable(coap_test "${test_src}")
target_link_libraries(coap_test Boost::unit_test_framework
                      nabto_coap
                      Threads::Threads)

install(TARGETS coap_test RUNTIME DESTINATION bin)
