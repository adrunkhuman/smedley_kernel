add_library(campaign_runner MODULE src/plugin.cpp src/campaign_launch_arguments.cpp src/campaign_launcher.cpp
  src/campaign_launcher.hpp src/benchmark_controller.cpp src/campaign_telemetry.cpp src/campaign_save_selection.cpp)
target_link_libraries(campaign_runner smedley_game_runtime smedley_kernel shell32 psapi)
target_include_directories(campaign_runner PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(BUILD_TESTING)
  add_executable(smedley_campaign_runner_tests src/campaign_launch_arguments.cpp src/benchmark_controller.cpp src/campaign_telemetry.cpp src/campaign_save_selection.cpp tests/campaign_telemetry_test.cpp)
  target_include_directories(smedley_campaign_runner_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(smedley_campaign_runner_tests PRIVATE GTest::gtest_main smedley_kernel)
  include(GoogleTest)
  gtest_discover_tests(smedley_campaign_runner_tests DISCOVERY_TIMEOUT 30)
endif()

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS campaign_runner DESTINATION ${PLUGINS_DIR})
  install(FILES campaign_runner.toml DESTINATION ${PLUGINS_DIR})
endif()
