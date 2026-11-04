vcpkg_from_sourceforge(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO qwt/qwt
    REF 6.3.0
    FILENAME "qwt-6.3.0.zip"
    SHA512 4008c3e4dace0f18e572b473a51a293bb896abbd62b9c5f0a92734b2121923d2e2cbf67c997b84570a13bf4fdd7669b56497c82fbae35049ed856b2f0a65e475
    PATCHES
        fix-dynamic-static.patch
        inlined-templates.patch
)

# The designer plugin would require qt5-tools.
file(READ "${SOURCE_PATH}/qwtconfig.pri" QWT_CONFIG_PRI)
string(REGEX REPLACE "\nQWT_CONFIG[ \t]*\\+=[ \t]*QwtDesigner" "\n#QWT_CONFIG += QwtDesigner" QWT_CONFIG_PRI_NEW "${QWT_CONFIG_PRI}")
if (QWT_CONFIG_PRI_NEW STREQUAL QWT_CONFIG_PRI)
    message(FATAL_ERROR "Could not disable QwtDesigner in qwtconfig.pri")
endif()
file(WRITE "${SOURCE_PATH}/qwtconfig.pri" "${QWT_CONFIG_PRI_NEW}")

vcpkg_configure_qmake(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        CONFIG+=${VCPKG_LIBRARY_LINKAGE}
)

if (VCPKG_TARGET_IS_WINDOWS)
    vcpkg_install_qmake(
        RELEASE_TARGETS sub-src-release_ordered
        DEBUG_TARGETS sub-src-debug_ordered
    )
else ()
    vcpkg_install_qmake(
        RELEASE_TARGETS sub-src-all-ordered
        DEBUG_TARGETS sub-src-all-ordered
    )
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
        file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()

#Install the header files
file(GLOB HEADER_FILES "${SOURCE_PATH}/src/*.h" "${SOURCE_PATH}/classincludes/*")
file(INSTALL ${HEADER_FILES} DESTINATION "${CURRENT_PACKAGES_DIR}/include/${PORT}")

# Handle copyright
file(INSTALL "${SOURCE_PATH}/COPYING" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
