add_library(smedley_game_state STATIC src/readers.cpp)
target_include_directories(smedley_game_state PUBLIC include)

add_library(smedley_game_runtime STATIC src/runtime.cpp)
target_include_directories(smedley_game_runtime PUBLIC include)
target_link_libraries(smedley_game_runtime
  PUBLIC smedley_game_state
  PRIVATE smedley_kernel)

if(BUILD_TESTING)
  add_executable(smedley_game_state_tests tests/readers_test.cpp)
  target_link_libraries(smedley_game_state_tests PRIVATE smedley_game_state GTest::gtest_main)
  add_executable(smedley_game_runtime_tests tests/runtime_test.cpp)
  target_link_libraries(smedley_game_runtime_tests PRIVATE smedley_game_runtime smedley_kernel GTest::gtest_main)
  add_custom_command(TARGET smedley_game_runtime_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:smedley_kernel> $<TARGET_FILE_DIR:smedley_game_runtime_tests>)
  include(GoogleTest)
  gtest_discover_tests(smedley_game_state_tests DISCOVERY_TIMEOUT 30)
  gtest_discover_tests(smedley_game_runtime_tests DISCOVERY_TIMEOUT 30)
endif()
