cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(NOT V2_GAME_DIR)
	set(V2_GAME_DIR "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Victoria 2")
endif()

set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)

project("smedley")

include(CTest)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

function(smedley_allow_raw_plugin_sources target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Unknown raw plugin adapter target: ${target}")
  endif()
  get_target_property(target_sources ${target} SOURCES)
  set(absolute_target_sources)
  foreach(source IN LISTS target_sources)
    get_filename_component(absolute_source "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND absolute_target_sources "${absolute_source}")
  endforeach()
  foreach(source IN LISTS ARGN)
    get_filename_component(absolute_source "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    list(FIND absolute_target_sources "${absolute_source}" source_index)
    if(source_index EQUAL -1)
      message(FATAL_ERROR "Raw plugin adapter is not owned by ${target}: ${source}")
    endif()
    file(RELATIVE_PATH relative_source "${PROJECT_SOURCE_DIR}" "${absolute_source}")
    set_property(GLOBAL APPEND PROPERTY SMEDLEY_RAW_PLUGIN_SOURCES "${relative_source}")
  endforeach()
endfunction()

if(NOT MSVC)
    message(FATAL_ERROR "Smedley currently requires the MSVC x86 toolchain")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Smedley must be configured for Win32 because Victoria II is an x86 process")
endif()

include(FetchContent)

if(BUILD_TESTING)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )

  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

add_subdirectory ("smedley_kernel")
add_subdirectory ("game_state")
add_subdirectory ("plugins")
add_subdirectory ("tools/launcher/core")
add_subdirectory ("tools/launcher/cli")
add_subdirectory ("tools/launcher/gui")
add_subdirectory ("tools/trace")

if(BUILD_TESTING)
  get_property(raw_plugin_sources GLOBAL PROPERTY SMEDLEY_RAW_PLUGIN_SOURCES)
  set(raw_plugin_arguments)
  foreach(source IN LISTS raw_plugin_sources)
    list(APPEND raw_plugin_arguments --allow-raw "${source}")
  endforeach()
  add_test(
    NAME smedley_game_layering_audit
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_game_layering.py
      --root ${PROJECT_SOURCE_DIR} ${raw_plugin_arguments})
  add_test(
    NAME smedley_game_layering_tool_tests
    COMMAND ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/tools/tests/check_game_layering_test.py)
endif()
