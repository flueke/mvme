# Author: f.lueke@mesytec.com (2023)
#
# Requires the following variables to be set:

# * LINUXDEPLOYQT_EXECUTABLE:
#   Path to the linuxdeployqt binary
# * DEPLOY_BINARY:
#   Path to the binary that's should be analyzed by linuxdeployqt. Relative
#   installation path. ${CPACK_TEMPORARY_DIRECTORY} will be prepended to
#   calculate the full path.
#
# Note: DEPLOY_BINARY and other variables are set in CPackOptions.cmake.in

message("-- CPackExternal_linuxdeployqt: creating package archive for '${CPACK_PACKAGE_FILE_NAME}'")

#include(CMakePrintHelpers)
#cmake_print_variables(CPACK_TEMPORARY_DIRECTORY)
#cmake_print_variables(CPACK_TOPLEVEL_DIRECTORY)
#cmake_print_variables(CPACK_PACKAGE_DIRECTORY)
#cmake_print_variables(CPACK_PACKAGE_FILE_NAME)
#cmake_print_variables(CPACK_GENERATOR)
#cmake_print_variables(DEPLOY_BINARY)

#-- CPACK_TEMPORARY_DIRECTORY="/home/florian/src/mvme2/build/_CPack_Packages/Linux/External/mvme-1.6.1-rc2-10-Linux-x64"
#-- CPACK_TOPLEVEL_DIRECTORY="/home/florian/src/mvme2/build/_CPack_Packages/Linux/External"
#-- CPACK_PACKAGE_DIRECTORY="/home/florian/src/mvme2/build"
#-- CPACK_PACKAGE_FILE_NAME="mvme-1.6.1-rc2-10-Linux-x64"
#-- CPACK_GENERATOR="External"

# Turn the relative binary path into an absolute one.
set(DEPLOY_BINARY ${CPACK_TEMPORARY_DIRECTORY}/${DEPLOY_BINARY})

execute_process(
    COMMAND ${LINUXDEPLOYQT_EXECUTABLE} ${DEPLOY_BINARY}
    -unsupported-allow-new-glibc -bundle-non-qt-libs -no-translations -no-copy-copyright-files -no-strip
    COMMAND_ERROR_IS_FATAL ANY
    COMMAND_ECHO STDOUT
)

message("-- CPackExternal_linuxdeployqt: linuxdeployqt step done")

# Add additional shared objects that are excluded by linuxdeployqt. An
# alternative would be to pass "-unsupported-bundle-everything" to
# linuxdeployqt but that would also bundle glibc.
file(GET_RUNTIME_DEPENDENCIES
    RESOLVED_DEPENDENCIES_VAR DEPLOY_ADDITIONAL_FILES
    POST_INCLUDE_REGEXES ".*libgcc_s\\.so*" ".*libstdc\\+\\+\\.so*"
    POST_EXCLUDE_REGEXES ".*"
    EXECUTABLES ${DEPLOY_BINARY}
)

# Find and copy graphviz plugins and config file.
find_library(GV_CORE_PLUGIN gvplugin_core PATH_SUFFIXES graphviz x86_64-linux-gnu/graphviz REQUIRED)
find_library(GV_DOT_PLUGIN gvplugin_dot_layout PATH_SUFFIXES graphviz x86_64-linux-gnu/graphviz REQUIRED)

message("-- Found graphviz core plugin: ${GV_CORE_PLUGIN}")
message("-- Found graphviz dot plugin: ${GV_DOT_PLUGIN}")

list(APPEND DEPLOY_ADDITIONAL_FILES ${GV_CORE_PLUGIN} ${GV_DOT_PLUGIN})
message("-- CPackExternal_linuxdeployqt: Copying additional libraries and files
            into staging directory: ${DEPLOY_ADDITIONAL_LIBS}")

file(COPY ${DEPLOY_ADDITIONAL_FILES}
    DESTINATION "${CPACK_TEMPORARY_DIRECTORY}/lib"
    FOLLOW_SYMLINK_CHAIN
)

# Copy our custom graphviz plugin config file directly into the lib directory.
configure_file(${SOURCE_DIR}/cmake/graphviz-config6a
               ${CPACK_TEMPORARY_DIRECTORY}/lib/config6a
               COPYONLY)

set(PACKAGE_OUTPUT_DIR "${CPACK_TOPLEVEL_DIRECTORY}/packages")
set(PACKAGE_ARCHIVE_FILE "${PACKAGE_OUTPUT_DIR}/${CPACK_PACKAGE_FILE_NAME}.tar.bz2")

file(MAKE_DIRECTORY ${PACKAGE_OUTPUT_DIR})

message("-- CPackExternal_linuxdeployqt: Creating package archive with 'tar'...")

execute_process(
    COMMAND tar -cjf ${PACKAGE_ARCHIVE_FILE} "${CPACK_PACKAGE_FILE_NAME}"
    WORKING_DIRECTORY  ${CPACK_TOPLEVEL_DIRECTORY}
    COMMAND_ERROR_IS_FATAL ANY
    COMMAND_ECHO STDOUT
)

message("-- CPackExternal_linuxdeployqt: created package archive ${PACKAGE_ARCHIVE_FILE}")

set(CPACK_EXTERNAL_BUILT_PACKAGES ${PACKAGE_ARCHIVE_FILE})
