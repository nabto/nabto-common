set(src
  src/nabto_coap_server_impl.c
  src/nabto_coap_server_impl_incoming.c
  src/nabto_coap.c
  src/nabto_coap_client_impl.c
  src/nabto_coap_client_test.c
  )

add_library(nabto_coap ${src})
add_library(NabtoCommon::nabto_coap ALIAS nabto_coap)
target_link_libraries(nabto_coap NabtoCommon::nn)


target_include_directories(nabto_coap PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_sources(nabto_coap PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
        include/nabto_coap/nabto_coap_client_test.h
        include/nabto_coap/nabto_coap_client.h
        include/nabto_coap/nabto_coap_server.h
        include/nabto_coap/nabto_coap.h
)
