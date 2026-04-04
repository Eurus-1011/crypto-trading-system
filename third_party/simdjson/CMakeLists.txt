add_library(simdjson STATIC simdjson.cpp)
target_include_directories(simdjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(simdjson PRIVATE -O3 -march=native)
