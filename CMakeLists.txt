cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT V2_GAME_DIR)
	set(V2_GAME_DIR "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Victoria 2")
endif()

set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)

project("smedley")

include(CTest)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

if(NOT MSVC)
    message(FATAL_ERROR "Smedley currently requires the MSVC x86 toolchain")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Smedley must be configured for Win32 because Victoria II is an x86 process")
endif()

include(FetchContent)

FetchContent_Declare(
  minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
)
FetchContent_MakeAvailable(minhook)

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

add_subdirectory ("lib/telemetry_registry")
add_subdirectory ("kernel")
add_subdirectory ("plugins")
add_subdirectory ("apps/launcher/core")
add_subdirectory ("apps/launcher/cli")
add_subdirectory ("apps/launcher/gui")
add_subdirectory ("apps/trace")

if(EXISTS "${V2_GAME_DIR}")
  set(smedley_license_dir "${V2_GAME_DIR}/smedley-licenses")
  install(FILES LICENSE.md LICENSE.txt THIRD_PARTY_NOTICES.md
    DESTINATION "${smedley_license_dir}")
  install(FILES LICENSES/MIT.txt LICENSES/GPL-3.0.txt
    DESTINATION "${smedley_license_dir}/LICENSES")
  install(FILES third_party/minhook-LICENSE.txt
    DESTINATION "${smedley_license_dir}/third_party")
endif()

if(BUILD_TESTING)
  add_test(
    NAME smedley_game_layering_audit
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_game_layering.py
      --root ${PROJECT_SOURCE_DIR})
  add_test(
    NAME smedley_game_layering_tool_tests
    COMMAND ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/tools/tests/check_game_layering_test.py)
  add_test(
    NAME smedley_engine_ownership_audit
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_engine_ownership.py
      --root ${PROJECT_SOURCE_DIR})
  add_test(
    NAME smedley_engine_ownership_tool_tests
    COMMAND ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/tools/tests/check_engine_ownership_test.py)
  add_test(
    NAME smedley_clausewitz_workload_inventory_tool_tests
    COMMAND ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/tools/tests/clausewitz_workload_inventory_test.py)
  add_test(
    NAME smedley_dll_boundary_audit
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/audit_dll_boundaries.py
      --linker ${CMAKE_LINKER}
      --kernel $<TARGET_FILE:smedley_kernel>
      --campaign-runner $<TARGET_FILE:campaign_runner>
      --interest-bug-fix $<TARGET_FILE:interest_bug_fix>
      --scripting $<TARGET_FILE:scripting>
      --telemetry $<TARGET_FILE:telemetry>)
endif()
