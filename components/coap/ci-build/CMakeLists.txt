cmake_minimum_required(VERSION 3.10)
project(nabto_coap_ci_build)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../nn nn)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. nabto_coap)
