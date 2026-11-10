# Deploys the runtime dependencies of MSVC/vcpkg builds into the install tree:
# - DLLs required by targets installed with ${MVME_RUNTIME_DEPS}
# - Qt plugins and a qt.conf pointing to them
# - the MSVC runtime
#
# With -DX_VCPKG_APPLOCAL_DEPS_INSTALL=ON vcpkg's toolchain hooks every
# install(TARGETS) call and copies DLLs and Qt plugins itself. In this case
# only the MSVC runtime is handled here.

if (POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

if (X_VCPKG_APPLOCAL_DEPS_INSTALL)
    message(STATUS "mvme: runtime dependencies are deployed by vcpkg (X_VCPKG_APPLOCAL_DEPS_INSTALL)")
else()
    # Shared libraries built in external/ and the submodules. Their own install
    # rules do not use the dependency set, and dependencies of project DLLs
    # outside the set are not scanned. Adding them here installs them a second
    # time to the same location which is harmless.
    foreach (_lib jcon qgvcore mesytec-mvlc)
        if (TARGET ${_lib})
            install(TARGETS ${_lib} ${MVME_RUNTIME_DEPS} RUNTIME DESTINATION bin)
        endif()
    endforeach()

    # Qt plugins are loaded at runtime and thus invisible to dependency
    # scanning. Install them explicitly and add them to the set so that their
    # own DLLs are deployed too.
    find_package(Qt5 COMPONENTS Gui PrintSupport Widgets REQUIRED)

    set(_mvme_qt_plugins
        Qt5::QWindowsIntegrationPlugin
        Qt5::QWindowsVistaStylePlugin
        Qt5::QWindowsPrinterSupportPlugin
        Qt5::QSvgIconPlugin
        Qt5::QSvgPlugin
        Qt5::QGifPlugin
        Qt5::QICOPlugin
        Qt5::QJpegPlugin
    )

    foreach (_plugin IN LISTS _mvme_qt_plugins)
        if (NOT TARGET ${_plugin})
            message(WARNING "MvmeDeployWindows: Qt plugin ${_plugin} not found, skipping")
            continue()
        endif()
        get_target_property(_plugin_type ${_plugin} QT_PLUGIN_TYPE)
        install(IMPORTED_RUNTIME_ARTIFACTS ${_plugin}
            ${MVME_RUNTIME_DEPS}
            LIBRARY DESTINATION bin/plugins/${_plugin_type})
    endforeach()

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qt.conf.install" "[Paths]\nPlugins = plugins\n")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qt.conf.install" DESTINATION bin RENAME qt.conf)

    # Only search the build directory: vcpkg copies all required DLLs there and
    # executables always find these copies first. Also searching the vcpkg bin
    # dir would resolve some DLLs to two different paths which is an error.
    # System DLLs and the MSVC runtime are excluded, the latter is handled below.
    install(RUNTIME_DEPENDENCY_SET mvme_runtime_deps
        PRE_EXCLUDE_REGEXES
            "^[Aa][Pp][Ii]-[Mm][Ss]-" "^[Ee][Xx][Tt]-[Mm][Ss]-"
            "^[Vv][Cc][Rr][Uu][Nn][Tt][Ii][Mm][Ee]" "^[Mm][Ss][Vv][Cc][Pp]"
            "^[Cc][Oo][Nn][Cc][Rr][Tt]" "^[Vv][Cc][Cc][Oo][Rr][Ll][Ii][Bb]"
        POST_EXCLUDE_REGEXES ".*[/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
        DIRECTORIES "${CMAKE_BINARY_DIR}"
        RUNTIME DESTINATION bin)
endif()

set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
include(InstallRequiredSystemLibraries)
