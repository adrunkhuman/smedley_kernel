target_sources(smedley_kernel PRIVATE
  src/readers.cpp
  src/runtime.cpp
  src/campaign_control_abi.cpp
  src/artisan_consumption_hook.cpp
  src/factory_consumption_hook.cpp
  src/factory_sales_hook.cpp
  src/pop_cash_flow_hook.cpp)
target_include_directories(smedley_kernel PUBLIC include)
target_link_libraries(smedley_kernel PRIVATE psapi)

if(BUILD_TESTING)
  add_executable(smedley_game_state_tests tests/readers_test.cpp)
  target_link_libraries(smedley_game_state_tests PRIVATE smedley_kernel GTest::gtest_main)
  add_executable(smedley_game_runtime_tests tests/runtime_test.cpp)
  target_link_libraries(smedley_game_runtime_tests PRIVATE smedley_kernel GTest::gtest_main)
  add_custom_command(TARGET smedley_game_state_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:smedley_kernel> $<TARGET_FILE_DIR:smedley_game_state_tests>)
  add_custom_command(TARGET smedley_game_runtime_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:smedley_kernel> $<TARGET_FILE_DIR:smedley_game_runtime_tests>)
  include(GoogleTest)
  gtest_discover_tests(smedley_game_state_tests DISCOVERY_TIMEOUT 30)
  gtest_discover_tests(smedley_game_runtime_tests DISCOVERY_TIMEOUT 30)
endif()
