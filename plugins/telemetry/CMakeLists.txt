add_library(smedley_telemetry_core STATIC telemetry_core.cpp country_economy_core.cpp producer_sales_core.cpp
  pop_cash_flow_core.cpp pop_identity_core.cpp)
target_compile_definitions(smedley_telemetry_core PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(smedley_telemetry_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_SOURCE_DIR}/include")

add_library(telemetry MODULE plugin.cpp economic_capture.cpp economic_capture_core.cpp
  artisan_consumption_hook.cpp factory_consumption_hook.cpp factory_sales_hook.cpp
  pop_cash_flow_hook.cpp)
target_link_libraries(telemetry PRIVATE smedley_kernel smedley_telemetry_core smedley_game_state shell32)
target_include_directories(telemetry PRIVATE "../../smedley_kernel/include")
target_compile_definitions(telemetry PRIVATE SMEDLEY_TELEMETRY_BUILD)

if(BUILD_TESTING)
  add_executable(smedley_telemetry_core_tests telemetry_core_test.cpp country_economy_core_test.cpp
    producer_sales_core_test.cpp pop_cash_flow_core_test.cpp economic_capture_core.cpp
    economic_capture_core_test.cpp pop_identity_core_test.cpp)
  target_link_libraries(smedley_telemetry_core_tests PRIVATE smedley_telemetry_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(smedley_telemetry_core_tests DISCOVERY_TIMEOUT 30)
  add_library(smedley_telemetry_header_c OBJECT telemetry_header_c_test.c)
  target_include_directories(smedley_telemetry_header_c PRIVATE "${CMAKE_SOURCE_DIR}/include")
endif()

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS telemetry DESTINATION ${PLUGINS_DIR})
  install(FILES telemetry.toml DESTINATION ${PLUGINS_DIR})
endif()
