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

include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)
git_describe(GIT_VERSION "--always")
string(REGEX MATCH "^[0-9.]+-[0-9]+" GIT_VERSION_SHORT ${GIT_VERSION})

# Subdirectories containing build targets
add_subdirectory(src)
add_subdirectory(doc)

# Handle the template directory at installation and build time
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates" DESTINATION bin)

file(COPY templates DESTINATION .)

# CPack configuration used for 'make package'

if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    set(PACKAGE_ARCH_STRING "x64")
else()
    set(PACKAGE_ARCH_STRING "x32")
endif()


set(CPACK_PACKAGE_VERSION "${GIT_VERSION_SHORT}")
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}-${PACKAGE_ARCH_STRING}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_VENDOR "mesytec")
set(CPACK_PACKAGE_EXECUTABLES "mvme;mvme")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "mvme")
set(CPACK_OUTPUT_FILE_PREFIX "cpack-out")
#set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/src/client/mesycontrol/resources/gpl-notice.txt")
set(CPACK_STRIP_FILES TRUE)

if (WIN32)
    set(CPACK_GENERATOR "NSIS") # "ZIP"
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON")
    set(CPACK_NSIS_PACKAGE_NAME "mvme")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "mvme.exe")
    set(CPACK_NSIS_URL_INFO_ABOUT "http://www.mesytec.com")
    set(CPACK_NSIS_CONTACT "info@mesytec.com")
    #set(CPACK_NSIS_MENU_LINKS
    #    "share/doc/mesycontrol.pdf" "Documentation")
    set(CPACK_SOURCE_GENERATOR "ZIP")
else(WIN32)
    set(CPACK_GENERATOR "TBZ2") # "STGZ"
    set(CPACK_SOURCE_GENERATOR "TBZ2")
endif(WIN32)

include(CPack)

# vim:tw=0
