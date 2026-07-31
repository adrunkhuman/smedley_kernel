add_library(smedley_telemetry_core STATIC telemetry_core.cpp)
target_compile_definitions(smedley_telemetry_core PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(smedley_telemetry_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(telemetry MODULE plugin.cpp)
target_link_libraries(telemetry PRIVATE smedley_kernel smedley_telemetry_core shell32)
target_include_directories(telemetry PRIVATE "../../smedley_kernel/include")

if(BUILD_TESTING)
  add_executable(smedley_telemetry_core_tests telemetry_core_test.cpp)
  target_link_libraries(smedley_telemetry_core_tests PRIVATE smedley_telemetry_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(smedley_telemetry_core_tests)
endif()

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS telemetry DESTINATION ${PLUGINS_DIR})
  install(FILES telemetry.toml DESTINATION ${PLUGINS_DIR})
endif()
