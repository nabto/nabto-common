set(src
  src/nabto_coap_server_impl.c
  src/nabto_coap_server_impl_incoming.c
  src/nabto_coap.c
  src/nabto_coap_client_impl.c
  )

add_library(coap ${src})

target_include_directories(coap PUBLIC include)
