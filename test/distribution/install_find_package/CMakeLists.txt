cmake_minimum_required(VERSION 3.24)

project(example_superbuild)

project(cmake_find_package)

include(ExternalProject)



ExternalProject_Add(
  NabtoCommon
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
)

ExternalProject_Add(
    example
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/example
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
    DEPENDS NabtoCommon
)
