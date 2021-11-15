cmake_minimum_required(VERSION 3.10)
project(nabto_stun_ci_build)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../nn nn)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. nabto_stun_client)

set(Boost_USE_STATIC_LIBS ON)

find_package(
  Boost
  COMPONENTS unit_test_framework
  REQUIRED)

set(test_dir ${CMAKE_CURRENT_SOURCE_DIR}/../test)

set(CMAKE_CXX_STANDARD 14)
set(test_src
${test_dir}/message_test.cpp
${test_dir}/client_test.cpp
${test_dir}/unit_test.cpp)

add_executable(stun_test "${test_src}")
target_link_libraries(stun_test ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
                      nabto_stun_client)
target_include_directories(stun_test PRIVATE ${Boost_INCLUDE_DIRS})
target_link_directories(stun_test PRIVATE ${Boost_LIBRARY_DIRS})

install(TARGETS stun_test RUNTIME DESTINATION bin)

add_definitions(-DBoost_USE_STATIC_LIBS=ON)
