add_library(gfm_smedley_policy STATIC src/province_reconciliation.cpp)
target_include_directories(gfm_smedley_policy PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(gfm-smedley MODULE src/gfm_smedley.cpp)
set_target_properties(gfm-smedley PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
target_link_libraries(gfm-smedley PRIVATE gfm_smedley_policy bcrypt shell32)
target_include_directories(gfm-smedley PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")

if(BUILD_TESTING)
  add_executable(smedley_gfm_smedley_tests tests/province_reconciliation_test.cpp)
  target_link_libraries(smedley_gfm_smedley_tests PRIVATE gfm_smedley_policy GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(smedley_gfm_smedley_tests DISCOVERY_TIMEOUT 30)
endif()

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS gfm-smedley DESTINATION ${PLUGINS_DIR})
  install(FILES gfm-smedley.toml DESTINATION ${PLUGINS_DIR})
endif()
