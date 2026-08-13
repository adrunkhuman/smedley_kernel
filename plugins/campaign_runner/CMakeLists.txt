add_library(campaign_runner MODULE src/plugin.cpp src/campaign_services.cpp src/campaign_launch_arguments.cpp src/campaign_launcher.cpp
  src/campaign_launcher.hpp src/benchmark_controller.cpp src/campaign_telemetry.cpp src/campaign_save_selection.cpp)
set_target_properties(campaign_runner PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
target_link_libraries(campaign_runner shell32)
target_include_directories(campaign_runner PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")

if(BUILD_TESTING)
  add_executable(smedley_campaign_runner_tests src/campaign_launch_arguments.cpp src/benchmark_controller.cpp src/campaign_telemetry.cpp src/campaign_save_selection.cpp tests/campaign_telemetry_test.cpp)
  target_include_directories(smedley_campaign_runner_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(smedley_campaign_runner_tests PRIVATE GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(smedley_campaign_runner_tests DISCOVERY_TIMEOUT 30)
endif()

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS campaign_runner DESTINATION ${PLUGINS_DIR})
  install(FILES campaign_runner.toml DESTINATION ${PLUGINS_DIR})
endif()
