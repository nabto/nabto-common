set(src
  nabto_stream.c
  nabto_stream_window.c
  nabto_stream_packet.c
  nabto_stream_util.c
  nabto_stream_log.c
  nabto_stream_flow_control.c
  nabto_stream_congestion_control.c
  nabto_stream_log_helper.c
  nabto_stream_memory.c
  )

add_library(nabto_stream "${src}")
