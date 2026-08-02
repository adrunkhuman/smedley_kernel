add_library(smedley_game_state STATIC readers.cpp)
target_include_directories(smedley_game_state PUBLIC ".")

if(BUILD_TESTING)
  add_executable(smedley_game_state_tests readers_test.cpp)
  target_link_libraries(smedley_game_state_tests PRIVATE smedley_game_state GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(smedley_game_state_tests DISCOVERY_TIMEOUT 30)
endif()
