cmake_minimum_required(VERSION 3.2)
project(nn)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. nn)
set(test_dir ${CMAKE_CURRENT_SOURCE_DIR}/../test)

set(Boost_USE_STATIC_LIBS ON)

find_package(
  Boost
  COMPONENTS unit_test_framework
  REQUIRED)

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
target_link_libraries(nn_test ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} nn)
target_include_directories(nn_test PRIVATE ${Boost_INCLUDE_DIRS})

target_link_directories(nn_test PRIVATE ${Boost_LIBRARY_DIRS})
add_definitions(-DBoost_USE_STATIC_LIBS=ON)
