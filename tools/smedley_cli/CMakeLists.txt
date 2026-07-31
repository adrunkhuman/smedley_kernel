add_executable(smedley_cli main.cpp)
target_compile_definitions(smedley_cli PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(smedley_cli PRIVATE "../../include/toml")
target_link_libraries(smedley_cli PRIVATE bcrypt)

if(EXISTS ${V2_GAME_DIR})
  install(TARGETS smedley_cli DESTINATION ${V2_GAME_DIR})
endif()
