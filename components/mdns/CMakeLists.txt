
set(src
  src/mdns_server.c
  )

add_library(mdns_server ${src})
target_include_directories(mdns_server PUBLIC include)
