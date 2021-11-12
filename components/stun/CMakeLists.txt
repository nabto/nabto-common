cmake_minimum_required(VERSION 3.10)
project(nabto_stun)
option(STUN_BUILD_TESTS "build stun test program" OFF)

set(src
  src/nabto_stun_client.c
  src/nabto_stun_message.c
  )

add_library(nabto_stun_client "${src}")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../nn nn)
target_link_libraries(nabto_stun_client nn)

target_include_directories(nabto_stun_client PUBLIC include)

if (STUN_BUILD_TESTS)

  set(Boost_USE_STATIC_LIBS ON)

  find_package(Boost COMPONENTS unit_test_framework REQUIRED)


  set(CMAKE_CXX_STANDARD 14)
  set(test_src
    test/message_test.cpp
    test/client_test.cpp
    test/unit_test.cpp

    )

  add_executable(stun_test "${test_src}")
  target_link_libraries(stun_test ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} nabto_stun_client)
  target_include_directories(stun_test PRIVATE ${Boost_INCLUDE_DIRS})
  target_link_directories(stun_test PRIVATE ${Boost_LIBRARY_DIRS})

  install(TARGETS stun_test
    RUNTIME DESTINATION bin
    )

  add_definitions(-DBoost_USE_STATIC_LIBS=ON)
endif()
