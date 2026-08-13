cmake_minimum_required(VERSION 3.20)

FILE(GLOB_RECURSE kernel_sources src/*.cpp)
list(REMOVE_ITEM kernel_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/entry.cpp")

add_library(smedley_kernel_runtime OBJECT ${kernel_sources})
target_compile_definitions(smedley_kernel_runtime PRIVATE SMEDLEY_KERNEL SMEDLEY_EVENT_BUILD SMEDLEY_EVENT_SERVICES_BUILD
  SMEDLEY_CAMPAIGN_CONTROL_BUILD SMEDLEY_CAMPAIGN_RUNTIME_BUILD SMEDLEY_CAMPAIGN_AUTOMATION_BUILD SMEDLEY_INTEREST_POOL_BUILD
  SMEDLEY_TELEMETRY_GAME_BUILD SMEDLEY_TELEMETRY_OBSERVATION_BUILD SMEDLEY_LOGGING_BUILD)
target_include_directories(smedley_kernel_runtime PUBLIC
  "${PROJECT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/smedley"
  "${PROJECT_SOURCE_DIR}/third_party/toml/include")
target_link_libraries(smedley_kernel_runtime PRIVATE minhook)

add_library(smedley_kernel SHARED src/entry.cpp src/smedley_kernel.def $<TARGET_OBJECTS:smedley_kernel_runtime>)
set_target_properties(smedley_kernel PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
target_link_libraries(smedley_kernel PRIVATE bcrypt shell32 minhook)
target_include_directories(smedley_kernel PUBLIC
  "${PROJECT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/smedley"
  "${PROJECT_SOURCE_DIR}/third_party/toml/include")

if(BUILD_TESTING)
  FILE(GLOB_RECURSE test_sources CONFIGURE_DEPENDS tests/*_test.cpp)

  add_executable(smedley_kernel_tests ${test_sources} $<TARGET_OBJECTS:smedley_kernel_runtime>)
  target_link_libraries(smedley_kernel_tests GTest::gtest_main bcrypt shell32 minhook)
  target_include_directories(smedley_kernel_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/smedley"
    "${PROJECT_SOURCE_DIR}/game_state/include"
    "${PROJECT_SOURCE_DIR}/third_party/toml/include")

  add_library(smedley_plugin_abi_c_compile_test OBJECT tests/plugin_abi_header_c_test.c)
  target_include_directories(smedley_plugin_abi_c_compile_test PRIVATE "${CMAKE_SOURCE_DIR}/include")

  add_library(smedley_event_api_c_compile_test OBJECT tests/event_api_header_c_test.c)
  target_include_directories(smedley_event_api_c_compile_test PRIVATE "${CMAKE_SOURCE_DIR}/include")

  add_library(smedley_event_services_api_c_compile_test OBJECT tests/event_services_api_header_c_test.c)
  target_include_directories(smedley_event_services_api_c_compile_test PRIVATE "${CMAKE_SOURCE_DIR}/include")

  add_library(smedley_campaign_control_api_c_compile_test OBJECT tests/campaign_control_api_header_c_test.c)
  target_include_directories(smedley_campaign_control_api_c_compile_test PRIVATE "${CMAKE_SOURCE_DIR}/include")

  add_library(smedley_game_services_api_c_compile_test OBJECT tests/game_services_api_header_c_test.c)
  target_include_directories(smedley_game_services_api_c_compile_test PRIVATE "${CMAKE_SOURCE_DIR}/include")

  add_library(smedley_logging_api_c_compile_test OBJECT tests/logging_api_header_c_test.c)
  target_include_directories(smedley_logging_api_c_compile_test PRIVATE "${CMAKE_SOURCE_DIR}/include")

  add_library(smedley_telemetry_observation_api_c_compile_test OBJECT tests/telemetry_observation_api_header_c_test.c)
  target_include_directories(smedley_telemetry_observation_api_c_compile_test PRIVATE "${CMAKE_SOURCE_DIR}/include")

  add_library(smedley_plugin_abi_test_plugin MODULE tests/test_plugins/plugin_abi_test_plugin.c)
  set_target_properties(smedley_plugin_abi_test_plugin PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
  target_include_directories(smedley_plugin_abi_test_plugin PRIVATE "${CMAKE_SOURCE_DIR}/include")
  add_dependencies(smedley_kernel_tests smedley_plugin_abi_test_plugin)

  add_library(smedley_legacy_plugin_test_plugin MODULE tests/test_plugins/legacy_plugin_test_plugin.c)
  set_target_properties(smedley_legacy_plugin_test_plugin PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)

  include(GoogleTest)
  gtest_discover_tests(smedley_kernel_tests DISCOVERY_TIMEOUT 30)
endif()

if(EXISTS ${V2_GAME_DIR})
	install(TARGETS smedley_kernel DESTINATION ${V2_GAME_DIR})
endif()


# Build Doxygen documentation only when Doxygen is available and BUILD_DOC is enabled.
option(BUILD_DOC "Build documentation" OFF)
find_package(Doxygen)
if (DOXYGEN_FOUND)
  if (BUILD_DOC)
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    message("Doxygen build started")

    # Include documentation generation in the default build.
    add_custom_target( doc_doxygen ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM )
  endif(BUILD_DOC)
else (DOXYGEN_FOUND)
  message("Doxygen need to be installed to generate the doxygen documentation")
endif (DOXYGEN_FOUND)
