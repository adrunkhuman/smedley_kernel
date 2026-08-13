cmake_minimum_required(VERSION 3.20)

FILE(GLOB_RECURSE kernel_sources src/*.cpp)

add_library (smedley_kernel SHARED ${kernel_sources})
# Transitional C++ plugin imports require kernel auto-export until issue #41 completes.
set_target_properties(smedley_kernel PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(smedley_kernel PRIVATE SMEDLEY_KERNEL SMEDLEY_EVENT_BUILD SMEDLEY_EVENT_SERVICES_BUILD
  SMEDLEY_CAMPAIGN_CONTROL_BUILD SMEDLEY_CAMPAIGN_RUNTIME_BUILD SMEDLEY_INTEREST_POOL_BUILD
  SMEDLEY_TELEMETRY_GAME_BUILD SMEDLEY_LOGGING_BUILD LUA_BUILD_AS_DLL LUA_LIB)
target_link_libraries(smedley_kernel PRIVATE bcrypt shell32 minhook)
target_include_directories(smedley_kernel PUBLIC
  "${PROJECT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/smedley"
  "${PROJECT_SOURCE_DIR}/third_party/toml/include"
  PRIVATE "${PROJECT_SOURCE_DIR}/third_party/victoria2-lua/include")

if(BUILD_TESTING)
  FILE(GLOB_RECURSE test_sources CONFIGURE_DEPENDS tests/*_test.cpp)

  add_executable(smedley_kernel_tests ${test_sources})
  target_link_libraries(smedley_kernel_tests GTest::gtest_main smedley_kernel)

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

  add_library(smedley_plugin_abi_test_plugin MODULE tests/test_plugins/plugin_abi_test_plugin.c)
  set_target_properties(smedley_plugin_abi_test_plugin PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
  target_include_directories(smedley_plugin_abi_test_plugin PRIVATE "${CMAKE_SOURCE_DIR}/include")
  add_dependencies(smedley_kernel_tests smedley_plugin_abi_test_plugin)

  add_library(smedley_event_mutation_test_plugin MODULE tests/test_plugins/event_mutation_test_plugin.cpp)
  set_target_properties(smedley_event_mutation_test_plugin PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
  target_link_libraries(smedley_event_mutation_test_plugin PRIVATE smedley_kernel)
  add_dependencies(smedley_kernel_tests smedley_event_mutation_test_plugin)

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

option(SMEDLEY_REGENERATE_BINDINGS "Regenerate checked-in game bindings during the build" OFF)

if(SMEDLEY_REGENERATE_BINDINGS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import cog, toml, pydantic; assert pydantic.VERSION.startswith('1.')"
    RESULT_VARIABLE SMEDLEY_CODEGEN_DEPENDENCIES_MISSING
    OUTPUT_QUIET
    ERROR_QUIET)
  if(SMEDLEY_CODEGEN_DEPENDENCIES_MISSING)
    message(FATAL_ERROR "Binding regeneration requires cogapp, toml, and Pydantic 1.x")
  endif()
  add_custom_target(smedley_regenerate_bindings
    COMMAND ${Python3_EXECUTABLE} -m cogapp -r -I codegen @codegen/generated_outputs.txt
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Regenerating checked-in game bindings"
    VERBATIM
  )
  add_dependencies(smedley_kernel smedley_regenerate_bindings)
endif()
