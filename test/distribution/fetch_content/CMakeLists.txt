cmake_minimum_required(VERSION 3.25)
project(test_fetch_content)

include(FetchContent)

FetchContent_Declare(
    NabtoCommon
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../
    FIND_PACKAGE_ARGS
)
FetchContent_MakeAvailable(NabtoCommon)

find_package(NabtoCommon CONFIG REQUIRED COMPONENTS nn)

add_executable(test_nn test_nn.c)
target_link_libraries(test_nn NabtoCommon::nn)
