add_library(smedley_telemetry_core STATIC src/telemetry_core.cpp src/country_economy_core.cpp src/producer_sales_core.cpp
  src/pop_cash_flow_core.cpp src/pop_identity_core.cpp src/economic_capture_core.cpp)
target_compile_definitions(smedley_telemetry_core PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(smedley_telemetry_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include")

add_library(telemetry MODULE src/plugin.cpp src/economic_capture.cpp
  src/economic_capture.hpp
  src/artisan_consumption_hook.cpp src/factory_consumption_hook.cpp src/factory_sales_hook.cpp
  src/pop_cash_flow_hook.cpp src/artisan_consumption_hook.hpp src/factory_consumption_hook.hpp
  src/factory_sales_hook.hpp src/pop_cash_flow_hook.hpp)
target_link_libraries(telemetry PRIVATE smedley_kernel smedley_telemetry_core smedley_game_state shell32)
target_include_directories(telemetry PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(telemetry PRIVATE SMEDLEY_TELEMETRY_BUILD)
smedley_allow_raw_plugin_sources(telemetry
  src/plugin.cpp
  src/economic_capture.cpp
  src/economic_capture.hpp
  src/artisan_consumption_hook.cpp
  src/artisan_consumption_hook.hpp
  src/factory_consumption_hook.cpp
  src/factory_consumption_hook.hpp
  src/factory_sales_hook.cpp
  src/factory_sales_hook.hpp
  src/pop_cash_flow_hook.cpp
  src/pop_cash_flow_hook.hpp)

if(BUILD_TESTING)
  add_executable(smedley_telemetry_core_tests tests/telemetry_core_test.cpp tests/country_economy_core_test.cpp
    tests/producer_sales_core_test.cpp tests/pop_cash_flow_core_test.cpp
    tests/economic_capture_core_test.cpp tests/pop_identity_core_test.cpp)
  target_link_libraries(smedley_telemetry_core_tests PRIVATE smedley_telemetry_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(smedley_telemetry_core_tests DISCOVERY_TIMEOUT 30)
  add_library(smedley_telemetry_abi_c_compile_test OBJECT tests/telemetry_header_c_test.c)
  target_include_directories(smedley_telemetry_abi_c_compile_test PRIVATE "${CMAKE_SOURCE_DIR}/include")
endif()

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS telemetry DESTINATION ${PLUGINS_DIR})
  install(FILES telemetry.toml DESTINATION ${PLUGINS_DIR})
endif()
