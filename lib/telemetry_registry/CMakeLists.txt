add_library(smedley_telemetry_registry STATIC src/telemetry_registry.cpp)
target_include_directories(smedley_telemetry_registry PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
