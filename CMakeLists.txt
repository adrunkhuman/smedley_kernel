# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(NOT V2_GAME_DIR)
	set(V2_GAME_DIR "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Victoria 2")
endif()

set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)
#set(BUILD_DOC ON)

project("smedley")

include(CTest)

if(NOT MSVC)
    message(FATAL_ERROR "Smedley currently requires the MSVC x86 toolchain")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Smedley must be configured for Win32 because Victoria II is an x86 process")
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include_directories("include/lua" "include/toml")

# Include sub-projects.
add_subdirectory ("smedley_kernel")
add_subdirectory ("plugins")
add_subdirectory ("tools/launcher_core")
add_subdirectory ("tools/smedley_cli")
