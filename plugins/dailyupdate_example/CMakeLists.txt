cmake_minimum_required (VERSION 3.8)

include_directories("../../smedley_kernel/include")

add_library (dailyupdate_example MODULE "plugin.cpp")

target_link_libraries(dailyupdate_example smedley_kernel)

if(EXISTS ${PLUGINS_DIR})
	install(TARGETS dailyupdate_example DESTINATION ${PLUGINS_DIR})
	install(FILES dailyupdate_example.toml DESTINATION ${PLUGINS_DIR})
endif()
