FetchContent_Declare(
  lua51
  URL https://www.lua.org/ftp/lua-5.1.5.tar.gz
  URL_HASH SHA256=2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lua51)

set(lua51_sources
  ${lua51_SOURCE_DIR}/src/lapi.c
  ${lua51_SOURCE_DIR}/src/lcode.c
  ${lua51_SOURCE_DIR}/src/ldebug.c
  ${lua51_SOURCE_DIR}/src/ldo.c
  ${lua51_SOURCE_DIR}/src/ldump.c
  ${lua51_SOURCE_DIR}/src/lfunc.c
  ${lua51_SOURCE_DIR}/src/lgc.c
  ${lua51_SOURCE_DIR}/src/llex.c
  ${lua51_SOURCE_DIR}/src/lmem.c
  ${lua51_SOURCE_DIR}/src/lobject.c
  ${lua51_SOURCE_DIR}/src/lopcodes.c
  ${lua51_SOURCE_DIR}/src/lparser.c
  ${lua51_SOURCE_DIR}/src/lstate.c
  ${lua51_SOURCE_DIR}/src/lstring.c
  ${lua51_SOURCE_DIR}/src/ltable.c
  ${lua51_SOURCE_DIR}/src/ltm.c
  ${lua51_SOURCE_DIR}/src/lundump.c
  ${lua51_SOURCE_DIR}/src/lvm.c
  ${lua51_SOURCE_DIR}/src/lzio.c
  ${lua51_SOURCE_DIR}/src/lauxlib.c
  ${lua51_SOURCE_DIR}/src/lbaselib.c
  ${lua51_SOURCE_DIR}/src/ltablib.c
  ${lua51_SOURCE_DIR}/src/lstrlib.c
  ${lua51_SOURCE_DIR}/src/lmathlib.c
)

add_library(smedley_lua51 STATIC ${lua51_sources})
target_compile_definitions(smedley_lua51 PRIVATE LUA_ANSI _CRT_SECURE_NO_WARNINGS)
target_include_directories(smedley_lua51 PUBLIC ${lua51_SOURCE_DIR}/src)

add_library(smedley_scripting_core STATIC src/scripting_runtime.cpp)
target_link_libraries(smedley_scripting_core PUBLIC smedley_lua51)
target_include_directories(smedley_scripting_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(smedley_scripting_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(scripting MODULE src/plugin.cpp)
set_target_properties(scripting PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
target_link_libraries(scripting PRIVATE smedley_kernel smedley_scripting_core shell32)
target_include_directories(scripting PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
smedley_allow_raw_plugin_sources(scripting src/plugin.cpp)

if(BUILD_TESTING)
  add_executable(smedley_scripting_tests tests/scripting_runtime_test.cpp)
  target_link_libraries(smedley_scripting_tests PRIVATE smedley_scripting_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(smedley_scripting_tests DISCOVERY_TIMEOUT 30)
endif()

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS scripting DESTINATION ${PLUGINS_DIR})
  install(FILES scripting.toml DESTINATION ${PLUGINS_DIR})
  install(FILES
    ${CMAKE_SOURCE_DIR}/examples/scripts/country_log.lua
    ${CMAKE_SOURCE_DIR}/examples/scripts/pause_after_30_days.lua
    DESTINATION ${V2_GAME_DIR}/scripts/examples)
  install(FILES ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md DESTINATION ${V2_GAME_DIR})
endif()
