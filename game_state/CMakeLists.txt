# The runtime object library is linked into the kernel and host tests without
# making kernel-local C++ symbols part of the DLL ABI.
target_sources(smedley_kernel_runtime PRIVATE
  src/foreign_memory.cpp
  src/readers.cpp
  src/runtime.cpp
  src/campaign_control_abi.cpp
  src/services/campaign_runtime.cpp
  src/services/campaign_automation.cpp
  src/services/interest_pool.cpp
  src/services/telemetry.cpp
  src/artisan_consumption_hook.cpp
  src/factory_consumption_hook.cpp
  src/factory_sales_hook.cpp
  src/pop_cash_flow_hook.cpp)
target_include_directories(smedley_kernel_runtime PUBLIC include)
target_link_libraries(smedley_kernel PRIVATE psapi)

if(BUILD_TESTING)
  add_executable(smedley_game_state_tests tests/readers_test.cpp $<TARGET_OBJECTS:smedley_kernel_runtime>)
  target_link_libraries(smedley_game_state_tests PRIVATE GTest::gtest_main bcrypt shell32 minhook psapi)
  target_include_directories(smedley_game_state_tests PRIVATE "${PROJECT_SOURCE_DIR}/include" include "${PROJECT_SOURCE_DIR}/smedley_kernel/include")
  add_executable(smedley_game_runtime_tests tests/runtime_test.cpp $<TARGET_OBJECTS:smedley_kernel_runtime>)
  target_link_libraries(smedley_game_runtime_tests PRIVATE GTest::gtest_main bcrypt shell32 minhook psapi)
  target_include_directories(smedley_game_runtime_tests PRIVATE "${PROJECT_SOURCE_DIR}/include" include "${PROJECT_SOURCE_DIR}/smedley_kernel/include")
  include(GoogleTest)
  gtest_discover_tests(smedley_game_state_tests DISCOVERY_TIMEOUT 30)
  gtest_discover_tests(smedley_game_runtime_tests DISCOVERY_TIMEOUT 30)
endif()
