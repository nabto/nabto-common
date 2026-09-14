cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0167 NEW)
project(nabto_mdns_ci_build)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../nn nn)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. nabto_mdns)

find_package(
  Boost
  COMPONENTS unit_test_framework
  REQUIRED)

set(test_dir ${CMAKE_CURRENT_SOURCE_DIR}/../test)

set(CMAKE_CXX_STANDARD 14)
set(test_src
${test_dir}/mdns_server_test.cpp
${test_dir}/unit_test.cpp)

add_executable(mdns_test "${test_src}")
target_link_libraries(mdns_test Boost::unit_test_framework
                      nabto_mdns)

install(TARGETS mdns_test RUNTIME DESTINATION bin)
