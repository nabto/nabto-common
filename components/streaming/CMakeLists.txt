set(src
  src/nabto_stream.c
  src/nabto_stream_window.c
  src/nabto_stream_packet.c
  src/nabto_stream_util.c
  src/nabto_stream_log.c
  src/nabto_stream_flow_control.c
  src/nabto_stream_congestion_control.c
  src/nabto_stream_log_helper.c
  src/nabto_stream_memory.c
  )

add_library(nabto_stream "${src}")

target_include_directories(nabto_stream PUBLIC include)
