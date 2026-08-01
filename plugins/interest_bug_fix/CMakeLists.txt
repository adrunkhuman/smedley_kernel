add_library(interest_bug_fix_core STATIC economic_state.cpp interest_allocation.cpp interest_batch.cpp)
target_include_directories(interest_bug_fix_core PUBLIC ".")

add_library(pop_money_fixture MODULE pop_money_fixture.cpp)
target_link_libraries(pop_money_fixture interest_bug_fix_core smedley_kernel)
target_include_directories(pop_money_fixture PRIVATE "../../smedley_kernel/include")

add_library(interest_bug_fix MODULE interest_bug_fix.cpp telemetry_bridge.cpp)
target_link_libraries(interest_bug_fix interest_bug_fix_core smedley_kernel psapi)
target_include_directories(interest_bug_fix PRIVATE "../../smedley_kernel/include" "${CMAKE_SOURCE_DIR}/include")

add_executable(interest_bug_fix_test interest_bug_fix_test.cpp)
target_link_libraries(interest_bug_fix_test interest_bug_fix_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(interest_bug_fix_test DISCOVERY_TIMEOUT 30)

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS interest_bug_fix DESTINATION ${PLUGINS_DIR})
  install(FILES interest_bug_fix.toml DESTINATION ${PLUGINS_DIR})
endif()
