cmake_minimum_required(VERSION 3.12)

project(mvme
    DESCRIPTION "mesytec VME Data Acquisition")

message("CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug")
endif()

message("CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#set(WARNING_FLAGS "-Wall -Wno-unused-variable -Wno-unused-function")
#set(WARNING_FLAGS "-Wall -Wextra")

#if (WIN32)
#    set(WARNING_FLAGS "${WARNING_FLAGS} -Wno-format")
#endif(WIN32)

#set(CMAKE_CXX_FLAGS "-pipe ${WARNING_FLAGS}")

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mbmi2")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")

#set(MARCH_FLAG "nocona")
#set(MTUNE_FLAG "core2")
#set(MARCH_FLAG "haswell")
#set(MTUNE_FLAG "haswell")
#set(MARCH_FLAG "skylake")
#set(MTUNE_FLAG "skylake")

#set(CMAKE_CXX_FLAGS_DEBUG   "-ggdb -Og")
#set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3 -march=${MARCH_FLAG} -mtune=${MTUNE_FLAG}")
#set(CMAKE_CXX_FLAGS_PROFILE "-DNDEBUG -O3 -march=${MARCH_FLAG} -mtune=${MTUNE_FLAG} -pg -fno-omit-frame-pointer")

# Explicitly set the required c++ standard (CMake >= 3.1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
string(REGEX MATCH "^[0-9.]+(-beta[0-9]*|-rc[0-9]*)?(-[0-9]+)?" GIT_VERSION_SHORT ${GIT_VERSION})
string(REGEX MATCH "^[0-9.]+" GIT_VERSION_TAG ${GIT_VERSION})

execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

message("-- GIT_VERSION=${GIT_VERSION}")
message("-- GIT_VERSION_SHORT=${GIT_VERSION_SHORT}")
message("-- GIT_VERSION_TAG=${GIT_VERSION_TAG}")
message("-- GIT_BRANCH=${GIT_BRANCH}")

# RPATH handling
# https://gitlab.kitware.com/cmake/community/wikis/doc/cmake/RPATH-handling

# use, i.e. don't skip the full RPATH for the build tree
SET(CMAKE_SKIP_BUILD_RPATH FALSE)

# Enable $ORIGIN in the rpath if supported by the target platform.
SET(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

SET(CMAKE_INSTALL_RPATH "$$ORIGIN/../lib:$$ORIGIN/")

## add the automatically determined parts of the RPATH
## which point to directories outside the build tree to the install RPATH
#SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
#
## the RPATH to be used when installing, but only if it's not a system directory
#LIST(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
#IF("${isSystemDir}" STREQUAL "-1")
#   SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
#ENDIF("${isSystemDir}" STREQUAL "-1")

#if (WIN32)
#    string(APPEND CMAKE_CXX_FLAGS " -flto")
#    string(APPEND CMAKE_C_FLAGS " -flto")
#    string(APPEND CMAKE_EXE_LINKER_FLAGS " -flto")
#endif(WIN32)

set(not-msvc $<NOT:$<CXX_COMPILER_ID:MSVC>>)

# externals
add_subdirectory("${CMAKE_SOURCE_DIR}/external")

# quazip
find_path(QUAZIP_INCLUDE_DIR
    NAMES quazip.h
    PATH_SUFFIXES quazip quazip-qt5 quazip5
    QuaZip-Qt5-1.1/quazip
    QuaZip-Qt5-1.3/quazip
    QuaZip-Qt5-1.4/quazip
    )

#include_directories(${QUAZIP_INCLUDE_DIR})

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
        #set(QWT_LIBRARY ${QWT_LIBRARY_DEBUG})
    else()
        set(QWT_LIBRARY
            optimized ${QWT_LIBRARY_RELEASE}
            debug ${QWT_LIBRARY_RELEASE}
            )
        #set(QWT_LIBRARY ${QWT_LIBRARY_RELEASE})
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

if (MVME_BUILD_TESTS)
    message("-- Building test binaries")
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
# These files are handled using an external deploy directory which is added via
# the install() command.
option(MVME_INSTALL_DEPLOY_FILES "Install additional library files from a special deploy directory." OFF)

if (WIN32)
    if (MVME_INSTALL_DEPLOY_FILES)
        # C:\src\deploy\mvme-x64-libusb-0.1-Debug
        # C:\src\deploy\mvme-x64-libusb-0.1-Release
        #set(DEPLOY_DIR "c:/src/mvme2-deploy/windows/mvme-${PACKAGE_ARCH_STRING}-libusb-0.1-${CMAKE_BUILD_TYPE}/")
        set(DEPLOY_DIR "c:/src/mvme2-deploy/windows/mvme-${PACKAGE_ARCH_STRING}-libusb-0.1-Release/")

        install(DIRECTORY "${DEPLOY_DIR}" DESTINATION . USE_SOURCE_PERMISSIONS)
    endif()

    set(CPACK_GENERATOR "NSIS") # "NSIS;ZIP"
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON")
    set(CPACK_NSIS_PACKAGE_NAME "mvme")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "bin\\\\mvme.exe")
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
    if (MVME_INSTALL_DEPLOY_FILES)
        if (EXISTS "$ENV{HOME}/src/mvme2-deploy/linux/mvme-${PACKAGE_ARCH_STRING}-libusb-0.1/")
            # Install additional deployment files: shared objects, qt plugins, etc.
            install(DIRECTORY "$ENV{HOME}/src/mvme2-deploy/linux/mvme-${PACKAGE_ARCH_STRING}-libusb-0.1/"
                DESTINATION .
                USE_SOURCE_PERMISSIONS)
        endif()
    endif()

    set(CPACK_GENERATOR "TBZ2")
    set(CPACK_SOURCE_GENERATOR "TBZ2")
endif()

configure_file("CPackOptions.cmake.in" "${PROJECT_BINARY_DIR}/CPackOptions.cmake" @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${PROJECT_BINARY_DIR}/CPackOptions.cmake")

include(CPack)

# vim:tw=0
