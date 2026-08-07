cmake_minimum_required (VERSION 3.8)

if(NOT PLUGINS_DIR)
	set(PLUGINS_DIR "${V2_GAME_DIR}\\plugins")
endif()

file(TO_CMAKE_PATH "${PLUGINS_DIR}" plugins_install_dir)
install(CODE "file(REMOVE
  \"${plugins_install_dir}/v2up.dll\" \"${plugins_install_dir}/v2up.toml\"
  \"${plugins_install_dir}/dailyupdate_example.dll\" \"${plugins_install_dir}/dailyupdate_example.toml\"
  \"${plugins_install_dir}/economy_trace.dll\" \"${plugins_install_dir}/economy_trace.toml\"
  \"${plugins_install_dir}/interest_probe.dll\" \"${plugins_install_dir}/interest_probe.toml\"
  \"${plugins_install_dir}/interest_fix.dll\" \"${plugins_install_dir}/interest_fix.toml\"
  \"${plugins_install_dir}/economic_telemetry.dll\" \"${plugins_install_dir}/economic_telemetry.toml\")")

add_subdirectory("campaign_runner")
add_subdirectory("interest_bug_fix")
add_subdirectory("fiscal_credit_probe")
add_subdirectory("telemetry")
add_subdirectory("scripting")
