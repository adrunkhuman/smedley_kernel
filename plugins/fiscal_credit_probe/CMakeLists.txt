add_library(fiscal_credit_probe MODULE src/fiscal_credit_probe.cpp)
target_link_libraries(fiscal_credit_probe PRIVATE
  smedley_game_runtime
  smedley_kernel
  psapi)

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS fiscal_credit_probe DESTINATION ${PLUGINS_DIR})
  install(FILES fiscal_credit_probe.toml DESTINATION ${PLUGINS_DIR})
endif()
