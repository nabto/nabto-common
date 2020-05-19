find_package( Threads )

set(src_dir ${CMAKE_CURRENT_SOURCE_DIR}/generated)

include_directories(${src_dir})

set(boost_test_src
  ${src_dir}/libs/test/src/compiler_log_formatter.cpp
  ${src_dir}/libs/test/src/debug.cpp
  ${src_dir}/libs/test/src/decorator.cpp
  ${src_dir}/libs/test/src/execution_monitor.cpp
  ${src_dir}/libs/test/src/framework.cpp
  ${src_dir}/libs/test/src/plain_report_formatter.cpp
  ${src_dir}/libs/test/src/progress_monitor.cpp
  ${src_dir}/libs/test/src/results_collector.cpp
  ${src_dir}/libs/test/src/results_reporter.cpp
  ${src_dir}/libs/test/src/test_tools.cpp
  ${src_dir}/libs/test/src/test_tree.cpp
  ${src_dir}/libs/test/src/unit_test_log.cpp
  ${src_dir}/libs/test/src/unit_test_main.cpp
  ${src_dir}/libs/test/src/unit_test_monitor.cpp
  ${src_dir}/libs/test/src/unit_test_parameters.cpp
  ${src_dir}/libs/test/src/junit_log_formatter.cpp
  ${src_dir}/libs/test/src/xml_log_formatter.cpp
  ${src_dir}/libs/test/src/xml_report_formatter.cpp
  ${src_dir}/libs/test/src/test_framework_init_observer.cpp
  )

set(boost_timer_src
  ${src_dir}/libs/timer/src/auto_timers_construction.cpp
  ${src_dir}/libs/timer/src/cpu_timer.cpp
  )

add_library(3rdparty_boost_system INTERFACE)
target_include_directories(3rdparty_boost_system INTERFACE ${src_dir})

add_library(3rdparty_boost_test STATIC "${boost_test_src}")
target_include_directories(3rdparty_boost_system INTERFACE ${src_dir})

add_library(3rdparty_boost_timer "${boost_timer_src}")
target_compile_definitions(3rdparty_boost_timer PUBLIC -DBOOST_CHRONO_HEADER_ONLY)


target_link_libraries(3rdparty_boost_test 3rdparty_boost_timer)
target_link_libraries(3rdparty_boost_timer 3rdparty_boost_system)


add_library(3rdparty_boost_asio INTERFACE)
target_include_directories(3rdparty_boost_asio INTERFACE ${src_dir})
target_link_libraries(3rdparty_boost_asio INTERFACE 3rdparty_boost_system ${CMAKE_THREAD_LIBS_INIT})
target_compile_definitions(3rdparty_boost_asio INTERFACE -DBOOST_ASIO_DISABLE_BOOST_REGEX=1)


add_library(3rdparty_boost_beast INTERFACE)
target_include_directories(3rdparty_boost_beast INTERFACE ${src_dir})
target_link_libraries(3rdparty_boost_beast INTERFACE 3rdparty_boost_asio 3rdparty_boost_system ${CMAKE_THREAD_LIBS_INIT})
