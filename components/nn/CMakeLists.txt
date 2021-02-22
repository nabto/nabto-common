cmake_minimum_required(VERSION 3.2)
project(nn)

option(NN_BUILD_TESTS "build nn test program" OFF)



set(src
  src/nn/vector.c
  src/nn/string_set.c
  src/nn/log.c
  src/nn/llist.c
  src/nn/string_map.c
  src/nn/set.c
  )

add_library(nn "${src}")
target_include_directories(nn PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)


if (NN_BUILD_TESTS)
  set(Boost_USE_STATIC_LIBS ON)

  find_package(Boost COMPONENTS unit_test_framework REQUIRED)


  set(test_src
    test/nn/vector_test.cpp
    test/nn/string_set_test.cpp
    test/nn/log_test.cpp
    test/nn/llist_test.cpp
    test/nn/string_map_test.cpp
    test/nn/set_test.cpp
    test/unit_test.cpp

    )

  add_executable(nn_test "${test_src}")
  target_link_libraries(nn_test ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} nn)
  target_include_directories(nn_test PRIVATE ${Boost_INCLUDE_DIRS})


  target_link_directories(nn_test PRIVATE ${Boost_LIBRARY_DIRS})
  add_definitions(-DBoost_USE_STATIC_LIBS=ON)
endif()
