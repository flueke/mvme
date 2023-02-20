message("++ CPack external: Hello, World!")
include(CMakePrintHelpers)
cmake_print_variables(CPACK_TEMPORARY_DIRECTORY)
cmake_print_variables(CPACK_TOPLEVEL_DIRECTORY)
cmake_print_variables(CPACK_PACKAGE_DIRECTORY)
cmake_print_variables(CPACK_PACKAGE_FILE_NAME)
cmake_print_variables(CPACK_GENERATOR)

#-- CPACK_TEMPORARY_DIRECTORY="/home/florian/src/mvme2/build/_CPack_Packages/Linux/External/mvme-1.6.1-rc2-10-Linux-x64"
#-- CPACK_TOPLEVEL_DIRECTORY="/home/florian/src/mvme2/build/_CPack_Packages/Linux/External"
#-- CPACK_PACKAGE_DIRECTORY="/home/florian/src/mvme2/build"
#-- CPACK_PACKAGE_FILE_NAME="mvme-1.6.1-rc2-10-Linux-x64"
#-- CPACK_GENERATOR="External"


set(MVME_EXECUTABLE "${CPACK_TEMPORARY_DIRECTORY}/bin/mvme")

find_program(LINUXDEPLOYQT_EXECUTABLE linuxdeployqt REQUIRED)

execute_process(
    COMMAND ${LINUXDEPLOYQT_EXECUTABLE} ${MVME_EXECUTABLE}
    -unsupported-allow-new-glibc -bundle-non-qt-libs -no-translations -no-copy-copyright-files -no-strip
    COMMAND_ERROR_IS_FATAL ANY
)

# Add additional shared objects that are excluded by linuxdeployqt. An
# alternative would be to pass "-unsupported-bundle-everything" to
# linuxdeployqt but that would also bundle glibc.

file(GET_RUNTIME_DEPENDENCIES
    RESOLVED_DEPENDENCIES_VAR MVME_ADDITIONAL_LIBS
    POST_INCLUDE_REGEXES ".*libgcc_s\\.so*" ".*libstdc\\+\\+\\.so*" ".*libz\\.so.*"
    POST_EXCLUDE_REGEXES ".*"
    EXECUTABLES ${MVME_EXECUTABLE}
)

message("Copying additional libraries into staging directory: ${MVME_ADDITIONAL_LIBS}")

file(COPY ${MVME_ADDITIONAL_LIBS}
    DESTINATION "${CPACK_TEMPORARY_DIRECTORY}/lib"
    FOLLOW_SYMLINK_CHAIN
)

set(PACKAGE_OUTPUT_DIR "${CPACK_PACKAGE_DIRECTORY}/packages")
set(PACKAGE_ARCHIVE_FILE "${PACKAGE_OUTPUT_DIR}/${CPACK_PACKAGE_FILE_NAME}.tar.bz2")

file(MAKE_DIRECTORY ${PACKAGE_OUTPUT_DIR})


execute_process(
    COMMAND tar -cjvf ${PACKAGE_ARCHIVE_FILE} "${CPACK_PACKAGE_FILE_NAME}"
    WORKING_DIRECTORY  ${CPACK_TOPLEVEL_DIRECTORY}
    COMMAND_ERROR_IS_FATAL ANY
)

message("CPackExternal_mvme: created archive ${PACKAGE_ARCHIVE_FILE}")
