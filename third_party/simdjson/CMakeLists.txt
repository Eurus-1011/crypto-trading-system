option(SIMDJSON_BUILD_FROM_SOURCE "Build simdjson from source instead of using prebuilt lib" OFF)

if(SIMDJSON_BUILD_FROM_SOURCE)
    add_library(simdjson STATIC src/simdjson.cpp)
    target_include_directories(simdjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(simdjson PRIVATE -O3 -march=native)
else()
    add_library(simdjson STATIC IMPORTED GLOBAL)
    set_target_properties(simdjson PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/libsimdjson.a
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
endif()
