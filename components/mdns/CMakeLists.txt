
set(src
  src/mdns_server.c
  )

add_library(mdns_server ${src})
target_link_libraries(mdns_server nn)
target_include_directories(mdns_server PUBLIC include)
