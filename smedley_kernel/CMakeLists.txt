cmake_minimum_required(VERSION 3.20)

FILE(GLOB_RECURSE kernel_sources src/*.cpp)

include_directories("include/smedley")

add_library (smedley_kernel SHARED ${kernel_sources})
target_compile_definitions(smedley_kernel PRIVATE SMEDLEY_KERNEL SMEDLEY_EVENT_BUILD LUA_BUILD_AS_DLL LUA_LIB)
target_link_libraries(smedley_kernel PRIVATE shell32)
target_include_directories(smedley_kernel PUBLIC "${PROJECT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(BUILD_TESTING)
  FILE(GLOB_RECURSE test_sources tests/*.cpp)

  add_executable(smedley_run_tests ${test_sources})
  target_link_libraries(smedley_run_tests GTest::gtest_main smedley_kernel)

  add_library(smedley_plugin_abi_header_c OBJECT tests/plugin_abi_header_c_test.c)
  target_include_directories(smedley_plugin_abi_header_c PRIVATE "${CMAKE_SOURCE_DIR}/include")

  add_library(smedley_event_api_header_c OBJECT tests/event_api_header_c_test.c)
  target_include_directories(smedley_event_api_header_c PRIVATE "${CMAKE_SOURCE_DIR}/include")

  add_library(smedley_plugin_abi_fixture MODULE test_plugins/plugin_abi_fixture.c)
  set_target_properties(smedley_plugin_abi_fixture PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
  target_include_directories(smedley_plugin_abi_fixture PRIVATE "${CMAKE_SOURCE_DIR}/include")
  add_dependencies(smedley_run_tests smedley_plugin_abi_fixture)

  add_library(smedley_event_mutation_fixture MODULE test_plugins/event_mutation_fixture.cpp)
  set_target_properties(smedley_event_mutation_fixture PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
  target_link_libraries(smedley_event_mutation_fixture PRIVATE smedley_kernel)
  target_include_directories(smedley_event_mutation_fixture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
  add_dependencies(smedley_run_tests smedley_event_mutation_fixture)

  include(GoogleTest)
  gtest_discover_tests(smedley_run_tests DISCOVERY_TIMEOUT 30)
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
  add_custom_target(cog_generate
    COMMAND ${Python3_EXECUTABLE} -m cogapp -r -I codegen @generatedfiles.txt
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Regenerating checked-in game bindings"
    VERBATIM
  )
  add_dependencies(smedley_kernel cog_generate)
endif()
