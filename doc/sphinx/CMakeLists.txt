# Build Sphinx documentation.
# Source: https://eb2.co/blog/2012/03/sphinx-and-cmake-beautiful-documentation-for-c-projects/

# Notes (flueke):
# * Using different cache directories for each type of build to avoid race
#   conditions when using multiple make jobs.
# * add_custom_command() and add_custom_target() together are used to create a
#   dependency chain for CMake: the command specifies the output, the target
#   depends on the output.

find_package(Sphinx)

if (SPHINX_EXECUTABLE)

    set(SPHINX_SOURCE_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/source")
    set(SPHINX_BUILD_DIR    "${CMAKE_CURRENT_BINARY_DIR}/_build")
    set(SPHINX_CACHE_BASE   "${CMAKE_CURRENT_BINARY_DIR}/_doctrees")

    set(SPHINX_HTML_DIR     "${CMAKE_CURRENT_BINARY_DIR}/html")

    set(SPHINX_LATEX_DIR    "${CMAKE_CURRENT_BINARY_DIR}/latex")
    SET(SPHINX_PDF_OUT      "${SPHINX_LATEX_DIR}/mvme.pdf")

    set(SPHINX_QTHELP_DIR   "${CMAKE_CURRENT_BINARY_DIR}/qthelp")
    set(SPHINX_QCH_OUT      "${SPHINX_QTHELP_DIR}/mvme.qch")

    configure_file(
        "${SPHINX_SOURCE_DIR}/conf.py.in"
        "${SPHINX_BUILD_DIR}/conf.py"
        @ONLY
        )

    # Copies the footer logo for the latex build.
    configure_file(
        "${SPHINX_SOURCE_DIR}/images/mesytec-logo-yellow.png"
        "${SPHINX_LATEX_DIR}/mesytec-logo-yellow.png"
        COPYONLY
        )

    # Copy sphinx extension directory to build tree.
    add_custom_target(doc_sphinx_copy_ext
        ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ext"
        "${CMAKE_CURRENT_BINARY_DIR}/ext"
        )

    # html
    # ==================================================
    #add_custom_command(
    #    OUTPUT "${SPHINX_HTML_DIR}"
    #    COMMAND
    add_custom_target(doc_html
            ${SPHINX_EXECUTABLE}
            -E -b html
            -c "${SPHINX_BUILD_DIR}"
            -d "${SPHINX_CACHE_BASE}/html"
            "${SPHINX_SOURCE_DIR}"
            "${SPHINX_HTML_DIR}"
        COMMENT "Building HTML documentation with Sphinx"
        )

    #add_custom_target(doc_html
    #    ALL DEPENDS "${SPHINX_HTML_DIR}"
    #    )

    add_dependencies(doc_html doc_sphinx_copy_ext)

    install(DIRECTORY "${SPHINX_HTML_DIR}" DESTINATION doc)

    # latex
    # ==================================================
    add_custom_target(doc_latex
        ${SPHINX_EXECUTABLE}
        -E -b latex
        -c "${SPHINX_BUILD_DIR}"
        -d "${SPHINX_CACHE_BASE}/latex"
        "${SPHINX_SOURCE_DIR}"
        "${SPHINX_LATEX_DIR}"
        COMMENT "Building latex documentation with Sphinx"
        )
    add_dependencies(doc_latex doc_sphinx_copy_ext)

    # latex -> pdf
    #add_custom_command(
    #    OUTPUT "${SPHINX_PDF_OUT}"
    #    COMMAND
    add_custom_target(doc_latex_pdf
            ${CMAKE_MAKE_PROGRAM} -C ${SPHINX_LATEX_DIR}
        COMMENT "Generating PDF from Sphinx latex output"
        )

    #add_custom_target(doc_latex_pdf
    #    ALL DEPENDS "${SPHINX_PDF_OUT}"
    #    )

    add_dependencies(doc_latex_pdf doc_latex)

    install(FILES "${SPHINX_PDF_OUT}" DESTINATION doc)

    # qthelp
    # ==================================================
    add_custom_target(doc_qthelp
        ${SPHINX_EXECUTABLE}
        -E -b qthelp
        -c "${SPHINX_BUILD_DIR}"
        -d "${SPHINX_CACHE_BASE}/qthelp"
        "${SPHINX_SOURCE_DIR}"
        "${SPHINX_QTHELP_DIR}"
        COMMENT "Building qthelp documentation with Sphinx"
        )
    add_dependencies(doc_qthelp doc_sphinx_copy_ext)

    # qthelp collection

    add_custom_command(
        OUTPUT "${SPHINX_QCH_OUT}"
        COMMAND qcollectiongenerator "${SPHINX_QTHELP_DIR}/mvme.qhcp"
        COMMENT "Generating qthelp collection"
        )

    # The dependency on the QCH_OUT is what makes CMake actually run the command.
    # See https://cmake.org/Wiki/CustomCommandCustomTargetInstall for details.
    add_custom_target(doc_qthelp_collection
        ALL DEPENDS "${SPHINX_QCH_OUT}"
        )

    add_dependencies(doc_qthelp_collection doc_qthelp)

    install(FILES "${SPHINX_QCH_OUT}" DESTINATION doc)

    # 'doc' target for all formats
    # ==================================================
    add_custom_target(doc)
    add_dependencies(doc doc_html doc_latex_pdf doc_qthelp_collection)

endif()

# vim:tw=0
