option(STUN_BUILD_TESTS "build stun test program" OFF)

set(src
  src/nabto_stun_client.c
  src/nabto_stun_message.c
  )

add_library(nabto_stun_client "${src}")
target_link_libraries(nabto_stun_client nn)

target_include_directories(nabto_stun_client PUBLIC include)

if (STUN_BUILD_TESTS)
  set(CMAKE_CXX_STANDARD 14)
  set(test_src
    test/message_test.cpp
    test/client_test.cpp
    test/unit_test.cpp

    )

  add_executable(stun_test "${test_src}")
  target_link_libraries(stun_test 3rdparty_boost_test nabto_stun_client)

  install(TARGETS stun_test
    RUNTIME DESTINATION bin
    )
endif()
