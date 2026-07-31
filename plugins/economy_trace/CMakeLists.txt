add_library(economy_trace MODULE plugin.cpp)
target_link_libraries(economy_trace smedley_kernel)
target_include_directories(economy_trace PRIVATE "../../smedley_kernel/include")

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS economy_trace DESTINATION ${PLUGINS_DIR})
  install(FILES economy_trace.toml DESTINATION ${PLUGINS_DIR})
endif()
