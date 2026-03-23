cmake_minimum_required(VERSION 3.16)
project(crypto-trading-system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
find_package(OpenSSL REQUIRED)

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)

add_library(exchange_client STATIC
    src/clients/exchange_client.cpp
    src/clients/okx/okx.cpp
)
target_include_directories(exchange_client PUBLIC
    ${SRC_DIR}
    ${JSONCPP_INCLUDE_DIRS}
)
target_link_libraries(exchange_client PUBLIC
    ${JSONCPP_LIBRARIES}
    OpenSSL::Crypto
    OpenSSL::SSL
    pthread
)

add_executable(crypto-trading-system
    src/main.cpp
    src/quotation_engine/quotation_engine.cpp
    src/strategy_engine/strategy_engine.cpp
    src/trading_engine/trading_engine.cpp
    src/trading_engine/position_manager.cpp
)
target_include_directories(crypto-trading-system PRIVATE ${SRC_DIR} ${JSONCPP_INCLUDE_DIRS})
target_link_libraries(crypto-trading-system PRIVATE exchange_client rt)
target_compile_options(crypto-trading-system PRIVATE $<$<CONFIG:Release>:-O3 -march=native -DNDEBUG>)
