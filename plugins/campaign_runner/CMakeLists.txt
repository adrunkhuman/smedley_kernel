add_library(campaign_runner MODULE plugin.cpp campaign_launcher.cpp)
target_link_libraries(campaign_runner smedley_kernel shell32)
target_include_directories(campaign_runner PRIVATE "../../smedley_kernel/include")

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS campaign_runner DESTINATION ${PLUGINS_DIR})
  install(FILES campaign_runner.toml DESTINATION ${PLUGINS_DIR})
endif()
