cmake_minimum_required(VERSION 3.24)

project(test)

find_package(NabtoCommon CONFIG COMPONENTS nn)

add_executable(test test.c)
target_link_libraries(test NabtoCommon::nn)

install(TARGETS test)
