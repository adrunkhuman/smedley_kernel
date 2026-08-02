add_library(interest_bug_fix_core STATIC interest_allocation.cpp interest_batch.cpp interest_reconciliation.cpp pop_money_write.cpp)
target_include_directories(interest_bug_fix_core PUBLIC ".")

add_library(interest_bug_fix MODULE interest_bug_fix.cpp telemetry_bridge.cpp)
target_link_libraries(interest_bug_fix interest_bug_fix_core smedley_game_state smedley_kernel psapi)
target_include_directories(interest_bug_fix PRIVATE "../../smedley_kernel/include" "${CMAKE_SOURCE_DIR}/include")

if(BUILD_TESTING)
  add_executable(interest_bug_fix_test interest_bug_fix_test.cpp)
  target_link_libraries(interest_bug_fix_test interest_bug_fix_core smedley_game_state GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(interest_bug_fix_test DISCOVERY_TIMEOUT 30)
endif()

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS interest_bug_fix DESTINATION ${PLUGINS_DIR})
  install(FILES interest_bug_fix.toml DESTINATION ${PLUGINS_DIR})
endif()
