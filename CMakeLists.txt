cmake_minimum_required(VERSION 3.16)
project(crypto-trading-system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

set(COMMON_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src/include)

# OKX client library
add_library(okx_client STATIC
    src/client/okx/okx_client.cpp
)
target_include_directories(okx_client PUBLIC
    ${COMMON_INCLUDE_DIR}
    ${JSONCPP_INCLUDE_DIRS}
)
target_link_libraries(okx_client PUBLIC
    ${JSONCPP_LIBRARIES}
    CURL::libcurl
    OpenSSL::Crypto
)

# market_engine
add_executable(market_engine src/market_engine/main.cpp)
target_include_directories(market_engine PRIVATE ${COMMON_INCLUDE_DIR} ${JSONCPP_INCLUDE_DIRS})
target_link_libraries(market_engine PRIVATE okx_client rt)
target_compile_options(market_engine PRIVATE $<$<CONFIG:Release>:-O3 -march=native -DNDEBUG>)

# strategy_engine
add_executable(strategy_engine src/strategy_engine/main.cpp)
target_include_directories(strategy_engine PRIVATE ${COMMON_INCLUDE_DIR} ${JSONCPP_INCLUDE_DIRS})
target_link_libraries(strategy_engine PRIVATE ${JSONCPP_LIBRARIES} rt)
target_compile_options(strategy_engine PRIVATE $<$<CONFIG:Release>:-O3 -march=native -DNDEBUG>)

# trading_engine
add_executable(trading_engine src/trading_engine/main.cpp)
target_include_directories(trading_engine PRIVATE ${COMMON_INCLUDE_DIR} ${JSONCPP_INCLUDE_DIRS})
target_link_libraries(trading_engine PRIVATE okx_client rt)
target_compile_options(trading_engine PRIVATE $<$<CONFIG:Release>:-O3 -march=native -DNDEBUG>)
