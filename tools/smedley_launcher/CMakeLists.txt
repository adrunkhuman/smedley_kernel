add_executable(smedley_launcher WIN32 main.cpp)
target_compile_definitions(smedley_launcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(smedley_launcher PRIVATE smedley_launcher_core comctl32 comdlg32 ole32 shell32)

if(EXISTS ${V2_GAME_DIR})
  install(TARGETS smedley_launcher DESTINATION ${V2_GAME_DIR})
endif()
