set(cpp_src
  mdns_server.cpp
  )

set(src
  mdns_server.c
  )

#add_executable(mdns_cpp_server ${cpp_src})

add_library(mdns_server ${src})

#target_link_libraries(mdns_cpp_server 3rdparty_boost_asio common_util)
