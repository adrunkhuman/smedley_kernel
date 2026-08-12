add_library(interest_bug_fix_core STATIC src/interest_allocation.cpp src/interest_batch.cpp src/interest_mutation_status.cpp src/interest_reconciliation.cpp)
target_include_directories(interest_bug_fix_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(interest_bug_fix_core PUBLIC smedley_kernel)

add_library(interest_bug_fix MODULE src/interest_bug_fix.cpp src/telemetry_bridge.cpp)
target_link_libraries(interest_bug_fix PRIVATE
  interest_bug_fix_core
  smedley_kernel
  psapi)
target_include_directories(interest_bug_fix PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(BUILD_TESTING)
  add_executable(smedley_interest_bug_fix_tests tests/interest_bug_fix_test.cpp)
  target_link_libraries(smedley_interest_bug_fix_tests PRIVATE interest_bug_fix_core GTest::gtest_main)
  add_custom_command(TARGET smedley_interest_bug_fix_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:smedley_kernel> $<TARGET_FILE_DIR:smedley_interest_bug_fix_tests>)
  include(GoogleTest)
  gtest_discover_tests(smedley_interest_bug_fix_tests DISCOVERY_TIMEOUT 30)
endif()

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS interest_bug_fix DESTINATION ${PLUGINS_DIR})
  install(FILES interest_bug_fix.toml DESTINATION ${PLUGINS_DIR})
endif()
