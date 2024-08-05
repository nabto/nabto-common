
set(src
  src/mdns_server.c
  )

add_library(nabto_mdns ${src})
add_library(NabtoCommon::nabto_mdns ALIAS nabto_mdns)
target_link_libraries(nabto_mdns NabtoCommon::nn)
target_include_directories(nabto_mdns PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_sources(nabto_mdns PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
        include/nabto_mdns/nabto_mdns_server.h
)
