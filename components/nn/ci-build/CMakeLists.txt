cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0167 NEW)
project(nn)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. nn)
set(test_dir ${CMAKE_CURRENT_SOURCE_DIR}/../test)

find_package(
  Boost
  REQUIRED
  COMPONENTS unit_test_framework)

if (NOT Boost_FOUND)
  message(FATAL_ERROR "boost not found")
endif()

set(test_src
    ${test_dir}/nn/endian_test.cpp
    ${test_dir}/nn/vector_test.cpp
    ${test_dir}/nn/string_set_test.cpp
    ${test_dir}/nn/log_test.cpp
    ${test_dir}/nn/llist_test.cpp
    ${test_dir}/nn/string_int_map_test.cpp
    ${test_dir}/nn/string_map_test.cpp
    ${test_dir}/nn/set_test.cpp
    ${test_dir}/nn/ip_address_test.cpp
    ${test_dir}/unit_test.cpp)

add_executable(nn_test "${test_src}")
target_link_libraries(nn_test PRIVATE Boost::unit_test_framework nn)
