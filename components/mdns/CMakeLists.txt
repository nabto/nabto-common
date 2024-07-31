
set(src
  src/mdns_server.c
  )

add_library(mdns_server ${src})
add_library(NabtoCommon::mdns_server ALIAS mdns_server)
target_link_libraries(mdns_server NabtoCommon::nn)
target_include_directories(mdns_server PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_sources(mdns_server PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
        include/mdns/mdns_server.h
)
