cmake_minimum_required(VERSION 3.25)
project(test_fetch_content)

include(FetchContent)

FetchContent_Declare(
    NabtoCommon
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../
    FIND_PACKAGE_ARGS
)
FetchContent_MakeAvailable(NabtoCommon)

find_package(NabtoCommon CONFIG REQUIRED COMPONENTS nn nabto_stream coap mdns_server nabto_stun_client)

add_executable(test_nn test_nn.c)
target_link_libraries(test_nn NabtoCommon::nn NabtoCommon::nabto_stream NabtoCommon::coap NabtoCommon::mdns_server NabtoCommon::nabto_stun_client)
