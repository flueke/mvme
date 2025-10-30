cmake_minimum_required(VERSION 3.15)

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
# the last commits hash. Also the trailing commit number is added as another
# version number component, separated by a dot.
# Example GIT_VERSION_SHORT: 0.7.371

include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)
git_describe(GIT_VERSION "--always")
#string(REGEX REPLACE "^v" "" GIT_VERSION ${GIT_VERSION}) # strip leavind 'v' from tags

if(GIT_VERSION MATCHES v[^-]*-)
    string(REGEX REPLACE "v([^-]*)-([0-9]+)-.*" "\\1.\\2"  GIT_VERSION_SHORT ${GIT_VERSION})
else()
    string(REGEX REPLACE "v(.*)" "\\1" GIT_VERSION_SHORT ${GIT_VERSION})
endif()

execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

message("-- GIT_VERSION=${GIT_VERSION}")
message("-- GIT_VERSION_SHORT=${GIT_VERSION_SHORT}")
message("-- GIT_BRANCH=${GIT_BRANCH}")

project(mvme DESCRIPTION "mesytec VME Data Acquisition" VERSION ${GIT_VERSION_SHORT})

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug")
endif()

message("-- CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} (mvme)")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Explicitly set the required c++ standard (CMake >= 3.1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create binaries in the root of the build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# RPATH handling
# https://gitlab.kitware.com/cmake/community/wikis/doc/cmake/RPATH-handling

# use, i.e. don't skip the full RPATH for the build tree
SET(CMAKE_SKIP_BUILD_RPATH FALSE)

# Enable $ORIGIN in the rpath if supported by the target platform.
SET(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

SET(CMAKE_INSTALL_RPATH "$ORIGIN/../lib:$ORIGIN/")

set(not-msvc $<NOT:$<CXX_COMPILER_ID:MSVC>>)

# externals
add_subdirectory("${CMAKE_SOURCE_DIR}/external")

# quazip
find_path(QUAZIP_INCLUDE_DIR
    NAMES quazip.h
    PATH_SUFFIXES quazip quazip-qt5 quazip5 quazip1-qt5
    QuaZip-Qt5-1.1/quazip
    QuaZip-Qt5-1.3/quazip
    QuaZip-Qt5-1.4/quazip
    QuaZip-Qt5-1.5/quazip
)

find_library(QUAZIP_QT5_RELEASE NAMES quazip quazip-qt5 quazip5 quazip1-qt5)
find_library(QUAZIP_QT5_DEBUG NAMES quazipd quazip-qt5d quazip5d)

if (WIN32 OR QUAZIP_QT5_DEBUG)
    if (QUAZIP_QT5_DEBUG)
        set(QUAZIP_QT5 optimized ${QUAZIP_QT5_RELEASE} debug ${QUAZIP_QT5_DEBUG})
    else()
        set(QUAZIP_QT5 optimized ${QUAZIP_QT5_RELEASE} debug ${QUAZIP_QT5_RELEASE})
    endif()
else()
    set(QUAZIP_QT5 ${QUAZIP_QT5_RELEASE})
endif()

message("-- Using QUAZIP_INCLUDE_DIR=${QUAZIP_INCLUDE_DIR}")
message("-- Using QUAZIP_QT5=${QUAZIP_QT5}")

find_path(QWT_INCLUDE_DIR
    NAMES qwt_plot.h
    PATH_SUFFIXES qwt qwt-qt5 qwt-6.2.0 qwt-6.1.3 qt5/qwt6
    PATHS /usr/local/lib/qwt.framework/Headers)

include_directories(${QWT_INCLUDE_DIR})

find_library(QWT_LIBRARY_RELEASE NAMES qwt qwt-qt5 qwt6)
find_library(QWT_LIBRARY_DEBUG   NAMES qwtd qwt-qt5d qwt6d)

if (WIN32 OR QWT_LIBRARY_DEBUG)
    if (QWT_LIBRARY_DEBUG)
        set(QWT_LIBRARY
            optimized ${QWT_LIBRARY_RELEASE}
            debug ${QWT_LIBRARY_DEBUG}
            )
    else()
        set(QWT_LIBRARY
            optimized ${QWT_LIBRARY_RELEASE}
            debug ${QWT_LIBRARY_RELEASE}
            )
    endif()
else()
    set(QWT_LIBRARY ${QWT_LIBRARY_RELEASE})
endif()

message("-- Using QWT_INCLUDE_DIR=${QWT_INCLUDE_DIR}")
message("-- Using QWT_LIBRARY=${QWT_LIBRARY}")


# To get a 'make test' target. If this is not in the top level CMakeLists.txt
# no test target is generated. Do this before any add_subdirectory() commands
# as otherwise ctest won't pick up any tests in subprojects.
option(MVME_BUILD_TESTS "Build test binaries" OFF)
option(MVME_BUILD_BENCHMARKS "Build benchmark binaries" OFF)

if (MVME_BUILD_TESTS)
    message("-- mvme: Building tests")
endif()

if (MVME_BUILD_BENCHMARKS)
    message("-- mvme: Building benchmarks")
    if (NOT TARGET benchmark)
        message(">>> mvme tests enabled. using local copy of google benchmark")
        option(BENCHMARK_ENABLE_TESTING "Enable testing of the benchmark library." OFF)
        add_subdirectory(external/benchmark)
    endif()
endif()

if (MVME_BUILD_TESTS OR MVME_BUILD_BENCHMARKS)
    include(CTest)
    enable_testing()
    add_subdirectory(test)
endif()

# Subdirectories containing build targets
add_subdirectory(src)
add_subdirectory(extras)

option(MVME_BUILD_DOCS "Build the mvme documentation (requires Sphinx)" OFF)

if (MVME_BUILD_DOCS)
    add_subdirectory(doc)
endif()

# Log build type and flags. Keep this below the commands that might change the
# flags.
message("-- CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message("-- CMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}")
if (NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
    string(TOUPPER ${CMAKE_BUILD_TYPE} build_type_upper)
    message("-- CMAKE_CXX_FLAGS_${build_type_upper}=${CMAKE_CXX_FLAGS_${build_type_upper}}")
endif()

# Handle the template directory at installation and build time
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates" DESTINATION .)
file(COPY templates DESTINATION .)

# CPack configuration used for 'make package'

if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    set(PACKAGE_ARCH_STRING "x64")
else()
    set(PACKAGE_ARCH_STRING "x32")
endif()

# Note: To produce stand-alone packages additional files need to be deployed to
# the target system: shared objects, Qt plugins, Qt configuration, shell
# scripts, etc.
#
# Under windows These files are handled using an external deploy directory
# which is added via the install() command.
#
# Under linux the `linuxdeployqt` tool and a custom cmake script for the cpack
# `External` generator are used.

set(CPACK_PACKAGE_VERSION ${VERSION})
set(CPACK_PACKAGE_VENDOR "mesytec")
set(CPACK_PACKAGE_EXECUTABLES mvme;mvme)
set(CPACK_THREADS 0)

if (WIN32)
    # Note: CPack NSIS does automatically use the cmake/NSIS.template.in file!
    # This is important because currently not all standard settings are used in
    # the custom script, e.g. CPACK_NSIS_MUI_FINISHPAGE_RUN

    option(MVME_CPACK_INSTALL_DEPLOY_FILES
        "Install additional library files from a special deploy directory." OFF)

    set(MVME_CPACK_DEPLOY_DIR "c:/src/mvme2-deploy/windows/mvme-${PACKAGE_ARCH_STRING}-libusb-0.1-Release"
        CACHE PATH "Directory containing additional files to be deployed."
    )

    if (MVME_CPACK_INSTALL_DEPLOY_FILES)
        install(DIRECTORY "${MVME_CPACK_DEPLOY_DIR}/" DESTINATION . USE_SOURCE_PERMISSIONS)
    endif()

    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_SOURCE_GENERATOR "ZIP")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON")
    set(CPACK_NSIS_PACKAGE_NAME "mvme")
    set(CPACK_NSIS_URL_INFO_ABOUT "http://www.mesytec.com")
    set(CPACK_NSIS_CONTACT "info@mesytec.com")

    #set(CPACK_NSIS_EXECUTABLES_DIRECTORY "")
    # List of (<link> <link name>) pairs
    set(CPACK_NSIS_MENU_LINKS
        "doc/mvme.pdf"                          "mvme Documentation (PDF)"
        "doc/html/index.html"                   "mvme Documentation (HTML)"
        "extras/vm-usb/Manual_VM-USB_A00.pdf"   "VM-USB Manual"
    )
else()
    option(MVME_CPACK_USE_LINUXDEPLOYQT
        "Use 'linuxdeployqt' to create universal binary packages." OFF)

    set(CPACK_SOURCE_GENERATOR "TBZ2")

    if (MVME_CPACK_USE_LINUXDEPLOYQT)
        # Find linuxdeployqt here to error out early in case it's missing.
        find_program(LINUXDEPLOYQT_EXECUTABLE linuxdeployqt REQUIRED)
        set(CPACK_GENERATOR "External")
        set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/CPackExternal_linuxdeployqt.cmake")
        set(CPACK_EXTERNAL_ENABLE_STAGING ON)
    else()
        set(CPACK_GENERATOR "TBZ2")
    endif()
endif()

message("-- PROJECT_NAME=${PROJECT_NAME}, PROJECT_BINARY_DIR=${PROJECT_BINARY_DIR}, PROJECT_IS_TOP_LEVEL=${PROJECT_IS_TOP_LEVEL}")

# CPACK_PROJECT_CONFIG_FILE
# File included at cpack time, once per generator after setting CPACK_GENERATOR
# to the actual generator being used; allows per-generator setting of CPACK_*
# variables at cpack time.
# Default: ${PROJECT_BINARY_DIR}/CPackOptions.cmake
configure_file("CPackOptions.cmake.in" "${PROJECT_BINARY_DIR}/CPackOptions.cmake" @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${PROJECT_BINARY_DIR}/CPackOptions.cmake")

include(CPack)

# vim:tw=0
