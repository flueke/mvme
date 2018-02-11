cmake_minimum_required(VERSION 3.0.0)
project(mvme)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(WARNING_FLAGS "-Wall -Wno-unused-variable -Wno-unused-function")
#set(WARNING_FLAGS "${WARNING_FLAGS} -Wno-mismatched-tags")

set(CMAKE_CXX_FLAGS "-std=c++14 -pipe ${WARNING_FLAGS}")

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mbmi2")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")

set(MARCH_FLAG "nocona")
set(MTUNE_FLAG "core2")
#set(MARCH_FLAG "haswell")
#set(MTUNE_FLAG "haswell")
#set(MARCH_FLAG "skylake")
#set(MTUNE_FLAG "skylake")

set(CMAKE_CXX_FLAGS_DEBUG   "-g -Og")
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3 -march=${MARCH_FLAG} -mtune=${MTUNE_FLAG}")
set(CMAKE_CXX_FLAGS_PROFILE "-DNDEBUG -O3 -march=${MARCH_FLAG} -mtune=${MTUNE_FLAG} -pg -fno-omit-frame-pointer")
# Explicitly set the required c++ standard (CMake >= 3.1)
set(CMAKE_CXX_STANDARD 14)
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
string(REGEX MATCH "^[0-9.]+(-[0-9]+)?" GIT_VERSION_SHORT ${GIT_VERSION})
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

# External libraries.
# Note: This was moved up from the src/ subdirectory so that the
# include_directories() commands have effect both in the src/ and test/
# subdirectories. Otherwise the find_path() commands would have to be repeated
# inside test/.

# 3rdparty
include_directories("${CMAKE_SOURCE_DIR}/3rdparty")
include_directories("${CMAKE_SOURCE_DIR}/3rdparty/pcg-cpp-0.98/include/")
include_directories("${CMAKE_SOURCE_DIR}/3rdparty/")

# HDF5
set(HDF5_USE_STATIC_LIBRARIES Off)

find_package(HDF5 COMPONENTS CXX)
if (HDF5_FOUND)
    message("-- Found HDF5 - Enabling analysis session save/load support.")
    include_directories(${HDF5_INCLUDE_DIRS})
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${HDF5_DEFINITIONS}")
    add_definitions(-DMVME_ENABLE_HDF5)
else(HDF5_FOUND)
    message("-- HDF5 not found - Disabling analysis session save/load support.")
endif(HDF5_FOUND)

# qwt
find_path(QWT_INCLUDE_DIR
    NAMES qwt_plot.h
    PATH_SUFFIXES qwt qwt-qt5 qwt-6.1.3 qt5/qwt6)

find_library(QWT_LIBRARY NAMES qwt qwt-qt5 qwt6)
include_directories(${QWT_INCLUDE_DIR})
#include(FindPackageHandleStandardArgs)
#find_package_handle_standard_args(Qwt DEFAULT_MSG QWT_LIBRARY QWT_INCLUDE_DIR)

message("-- Using QWT_INCLUDE_DIR=${QWT_INCLUDE_DIR}")
message("-- Using QWT_LIBRARY=${QWT_LIBRARY}")

# ROOT
# https://root.cern.ch/how/integrate-root-my-project-cmake

# FIXME: This interferes with our own CFLAGS and does not use CMAKE_BUILD_TYPE!
option(ENABLE_ROOT "Enable ROOT support" OFF)

if (ENABLE_ROOT)
    list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})

    find_package(ROOT
        COMPONENTS Core RIO Tree Hist)

    if (ROOT_FOUND)
        include(${ROOT_USE_FILE})

        find_program(ROOT_CONFIG_EXECUTABLE root-config
            HINTS $ENV{ROOTSYS}/bin)

        add_definitions(-DMVME_ENABLE_ROOT)

        message("-- Found ROOT in ${ROOT_USE_FILE}")
    endif(ROOT_FOUND)
endif(ENABLE_ROOT)

# Subdirectories containing build targets
add_subdirectory(src)
add_subdirectory(extras)

# To get a 'make test' target. If this is not in the top level CMakeLists.txt
# no test target is generated :(
option(BUILD_TESTS "Build test binaries" OFF)

if (BUILD_TESTS)
    message("-- Building test binaries")
    enable_testing()
    add_subdirectory(test)
endif()

option(BUILD_DOCS "Build the documentation (if Sphinx is found)" ON)

if (BUILD_DOCS)
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

if (WIN32)
    # C:\src\deploy\mvme-x64-libusb-0.1-Debug
    # C:\src\deploy\mvme-x64-libusb-0.1-Release
    set(DEPLOY_DIR "c:/src/mvme2-deploy/windows/mvme-${PACKAGE_ARCH_STRING}-libusb-0.1-${CMAKE_BUILD_TYPE}/")

    install(DIRECTORY "${DEPLOY_DIR}" DESTINATION . USE_SOURCE_PERMISSIONS)

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
    install(DIRECTORY "$ENV{HOME}/src/mvme2-deploy/linux/mvme-${PACKAGE_ARCH_STRING}-libusb-0.1/"
        DESTINATION .
        USE_SOURCE_PERMISSIONS)

    set(CPACK_GENERATOR "TBZ2")
    set(CPACK_SOURCE_GENERATOR "TBZ2")
endif()

configure_file("CPackOptions.cmake.in" "${PROJECT_BINARY_DIR}/CPackOptions.cmake" @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${PROJECT_BINARY_DIR}/CPackOptions.cmake")

include(CPack)

# vim:tw=0
