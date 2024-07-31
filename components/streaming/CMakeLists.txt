set(src
  src/nabto_stream.c
  src/nabto_stream_window.c
  src/nabto_stream_packet.c
  src/nabto_stream_util.c
  src/nabto_stream_flow_control.c
  src/nabto_stream_congestion_control.c
  src/nabto_stream_log_helper.c
  src/nabto_stream_memory.c
  )

add_library(nabto_stream "${src}")
add_library(NabtoCommon::nabto_stream ALIAS nabto_stream)

target_link_libraries(nabto_stream nn)

target_include_directories(nabto_stream PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_sources(nabto_stream PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
        include/streaming/nabto_stream_config.h
        include/streaming/nabto_stream_congestion_control.h
        include/streaming/nabto_stream_flow_control.h
        include/streaming/nabto_stream.h
        include/streaming/nabto_stream_interface.h
        include/streaming/nabto_stream_log_helper.h
        include/streaming/nabto_stream_memory.h
        include/streaming/nabto_stream_packet.h
        include/streaming/nabto_stream_protocol.h
        include/streaming/nabto_stream_types.h
        include/streaming/nabto_stream_util.h
        include/streaming/nabto_stream_window.h
)
