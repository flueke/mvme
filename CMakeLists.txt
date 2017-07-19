cmake_minimum_required(VERSION 3.0.0)
project(mvme)

## Create binaries in the root of the build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Add the local cmake directory module path.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Pull information from git:
# GIT_SHA1 contains the complete git hash
#
# GIT_VERSION contains the name of the latest git tag, the number of commits
# since that tag was created and the start of the git hash if there have been
# changes since the tag was created.
# Example GIT_VERSION: 0.7-371-g12d9678

# GIT_VERSION_SHORT contains the same as GIT_VERSION but without the start of
# the last commits hash.
# Example GIT_VERSION_SHORT: 0.7-371

# GIT_VERSION_TAG contains only the tag part of the version
# Example GIT_VERSION_TAG: 0.7

include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)
git_describe(GIT_VERSION "--always")
string(REGEX MATCH "^[0-9.]+-[0-9]+" GIT_VERSION_SHORT ${GIT_VERSION})
string(REGEX MATCH "^[0-9.]+" GIT_VERSION_TAG ${GIT_VERSION})

# Subdirectories containing build targets
add_subdirectory(src)
add_subdirectory(extras)

option(BUILD_DOCS "Build the documentation (if Sphinx is found)" ON)

if (BUILD_DOCS)
    add_subdirectory(doc)
endif()

# Handle the template directory at installation and build time
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates" DESTINATION .)
file(COPY templates DESTINATION .)

# CPack configuration used for 'make package'

# CPACK_PROJECT_CONFIG_FILE
# File included at cpack time, once per generator after setting CPACK_GENERATOR
# to the actual generator being used; allows per-generator setting of CPACK_*
# variables at cpack time.
# Default: ${PROJECT_BINARY_DIR}/CPackOptions.cmake

if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    set(PACKAGE_ARCH_STRING "x64")
else()
    set(PACKAGE_ARCH_STRING "x32")
endif()

# Note: To produce stand-alone packages additional files need to be deployed to
# the target system: shared objects, Qt plugins, Qt configuration, shell
# scripts, etc.
#
# For WIN32 adding those deploy files is handled in the NSIS input file found
# in cmake/NSIS.template.in (variable DEPLOY_DIR).
# FIXME: Using NSIS to handle DEPLOY_DIR is bad as ZIP packages generated here
# are missing those files...
#
# For other systems an external deploy directory is added via the install()
# command.

if (WIN32)
    install(DIRECTORY "c:/src/deploy/mvme-${PACKAGE_ARCH_STRING}-libusb-0.1/"
        DESTINATION .
        USE_SOURCE_PERMISSIONS)

    set(CPACK_GENERATOR "NSIS") # "NSIS;ZIP"
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON")
    set(CPACK_NSIS_PACKAGE_NAME "mvme")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "mvme.exe")
    set(CPACK_NSIS_URL_INFO_ABOUT "http://www.mesytec.com")
    set(CPACK_NSIS_CONTACT "info@mesytec.com")

    #set(CPACK_NSIS_EXECUTABLES_DIRECTORY "")
    # List of (<link> <link name>) pairs
    set(CPACK_NSIS_MENU_LINKS
        "doc/mvme.pdf"                          "mvme Documentation (PDF)"
        "doc/html/index.html"                   "mvme Documentation (HTML)"
        "extras/vm-usb/Manual_VM-USB_A00.pdf"   "VM-USB Manual"
        )

    set(CPACK_SOURCE_GENERATOR "ZIP")
else()
    # Install additional deployment files: shared objects, qt plugins, etc.
    install(DIRECTORY "$ENV{HOME}/src/deploy/mvme-${PACKAGE_ARCH_STRING}-libusb-0.1/"
        DESTINATION .
        USE_SOURCE_PERMISSIONS)

    set(CPACK_GENERATOR "TBZ2")
    set(CPACK_SOURCE_GENERATOR "TBZ2")
endif()

configure_file("CPackOptions.cmake.in" "${PROJECT_BINARY_DIR}/CPackOptions.cmake" @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${PROJECT_BINARY_DIR}/CPackOptions.cmake")

include(CPack)

# vim:tw=0
