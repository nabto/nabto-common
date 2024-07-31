set(src
  src/nabto_coap_server_impl.c
  src/nabto_coap_server_impl_incoming.c
  src/nabto_coap.c
  src/nabto_coap_client_impl.c
  src/nabto_coap_client_test.c
  )

add_library(coap ${src})
target_link_libraries(coap NabtoCommon::nn)

target_include_directories(coap PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_sources(coap PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
        include/coap/nabto_coap_client_test.h
        include/coap/nabto_coap_client.h
        include/coap/nabto_coap_server.h
        include/coap/nabto_coap.h
)
