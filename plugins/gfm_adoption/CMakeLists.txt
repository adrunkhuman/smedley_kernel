add_library(gfm_adoption_policy STATIC src/province_reconciliation.cpp)
target_include_directories(gfm_adoption_policy PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(gfm_adoption MODULE src/gfm_adoption.cpp)
set_target_properties(gfm_adoption PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
target_link_libraries(gfm_adoption PRIVATE gfm_adoption_policy bcrypt shell32)
target_include_directories(gfm_adoption PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")

if(BUILD_TESTING)
  add_executable(smedley_gfm_adoption_tests tests/province_reconciliation_test.cpp)
  target_link_libraries(smedley_gfm_adoption_tests PRIVATE gfm_adoption_policy GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(smedley_gfm_adoption_tests DISCOVERY_TIMEOUT 30)
endif()

if(EXISTS ${PLUGINS_DIR})
  install(TARGETS gfm_adoption DESTINATION ${PLUGINS_DIR})
  install(FILES gfm_adoption.toml DESTINATION ${PLUGINS_DIR})
endif()
