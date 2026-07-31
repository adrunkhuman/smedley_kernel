add_library(interest_probe_core STATIC probe_core.cpp interest_allocation.cpp economic_telemetry_core.cpp)
target_include_directories(interest_probe_core PUBLIC ".")

add_library(interest_probe MODULE plugin.cpp)
target_link_libraries(interest_probe interest_probe_core smedley_kernel)
target_include_directories(interest_probe PRIVATE "../../smedley_kernel/include")

add_library(pop_money_fixture MODULE pop_money_fixture.cpp)
target_link_libraries(pop_money_fixture interest_probe_core smedley_kernel)
target_include_directories(pop_money_fixture PRIVATE "../../smedley_kernel/include")

add_library(interest_fix MODULE interest_fix.cpp telemetry_bridge.cpp)
target_link_libraries(interest_fix interest_probe_core smedley_kernel psapi)
target_include_directories(interest_fix PRIVATE "../../smedley_kernel/include" "${CMAKE_SOURCE_DIR}/include")

add_library(economic_telemetry MODULE economic_telemetry.cpp telemetry_bridge.cpp)
target_link_libraries(economic_telemetry interest_probe_core smedley_kernel psapi shell32)
target_include_directories(economic_telemetry PRIVATE "../../smedley_kernel/include" "${CMAKE_SOURCE_DIR}/include")

add_executable(interest_probe_test probe_core_test.cpp)
target_link_libraries(interest_probe_test interest_probe_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(interest_probe_test DISCOVERY_TIMEOUT 30)

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS interest_probe DESTINATION ${PLUGINS_DIR})
  install(FILES interest_probe.toml DESTINATION ${PLUGINS_DIR})
  install(TARGETS interest_fix DESTINATION ${PLUGINS_DIR})
  install(FILES interest_fix.toml DESTINATION ${PLUGINS_DIR})
  install(TARGETS economic_telemetry DESTINATION ${PLUGINS_DIR})
  install(FILES economic_telemetry.toml DESTINATION ${PLUGINS_DIR})
endif()
