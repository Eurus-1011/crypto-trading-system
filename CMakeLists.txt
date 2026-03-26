cmake_minimum_required(VERSION 3.16)

set(TARGET_NAME crypto-trading-system)

project(${TARGET_NAME} LANGUAGES CXX)

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

add_executable(${TARGET_NAME}
    src/main.cpp
    src/quotation_engine/quotation_engine.cpp
    src/strategy_engine/strategy.cpp
    src/strategy_engine/strategy_engine.cpp
    src/strategy_engine/mesh.cpp
    src/trading_engine/trading_engine.cpp
    src/trading_engine/position_manager.cpp
)
target_include_directories(${TARGET_NAME} PRIVATE ${SRC_DIR} ${JSONCPP_INCLUDE_DIRS})
target_link_libraries(${TARGET_NAME} PRIVATE exchange_client rt)
target_compile_options(${TARGET_NAME} PRIVATE $<$<CONFIG:Release>:-O3 -march=native -DNDEBUG>)
