add_executable(smedley_cli main.cpp)
target_compile_definitions(smedley_cli PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(smedley_cli PRIVATE smedley_launcher_core)

if(EXISTS ${V2_GAME_DIR})
  install(TARGETS smedley_cli DESTINATION ${V2_GAME_DIR})
endif()
