set(src
  src/nabto_stun_client.c
  src/nabto_stun_message.c
  src/nabto_stun_log.c
  )

add_library(nabto_stun_client "${src}")

target_include_directories(nabto_stun_client PUBLIC include)
