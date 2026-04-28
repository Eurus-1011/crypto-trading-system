add_library(sdk SHARED
    src/strategy.cpp
    src/strategy_registry.cpp
    src/instrument_registry.cpp
    src/position_manager.cpp
    src/log.cpp
)

target_include_directories(sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(sdk PUBLIC cxx_std_23)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS sdk
    EXPORT sdkTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT sdkTargets
    NAMESPACE sdk::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sdk
)

configure_package_config_file(cmake/sdkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/sdkConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sdk
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sdkConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sdk
)
