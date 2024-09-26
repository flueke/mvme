add_subdirectory(sphinx)

find_package(Doxygen)

if (DOXYGEN_FOUND)
    file(REAL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../src DOXYGEN_STRIP_FROM_PATH)

    configure_file(Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    add_custom_target(mvme_doc_doxygen
        COMMAND Doxygen::doxygen ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        COMMENT "Running doxygen")

    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doxygen/html/ DESTINATION doc/mvme/doxygen)
endif()
