# Build Sphinx documentation.
# Source: https://eb2.co/blog/2012/03/sphinx-and-cmake-beautiful-documentation-for-c-projects/

# Notes (flueke):
# * Using different cache directories for each type of build to avoid race
#   conditions when using multiple make jobs.
# * add_custom_command() and add_custom_target() together are used to create a
#   dependency chain for CMake: the command specifies the output, the target
#   depends on the output.

find_package(Sphinx REQUIRED)
find_program(LATEXMK latexmk)
find_program(QCOLLECTIONGENERATOR qcollectiongenerator)

if (SPHINX_EXECUTABLE)

    set(SPHINX_SOURCE_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/source")
    set(SPHINX_BUILD_BASE       "${CMAKE_CURRENT_BINARY_DIR}/_build")
    set(SPHINX_CACHE_BASE       "${CMAKE_CURRENT_BINARY_DIR}/_doctrees")

    set(SPHINX_HTML_DIR         "${CMAKE_CURRENT_BINARY_DIR}/html")

    set(SPHINX_LATEX_DIR        "${CMAKE_CURRENT_BINARY_DIR}/latex")
    set(SPHINX_PDF_OUT          "${SPHINX_LATEX_DIR}/mvme.pdf")

    set(SPHINX_QTHELP_DIR       "${CMAKE_CURRENT_BINARY_DIR}/qthelp")
    set(SPHINX_QCH_OUT          "${SPHINX_QTHELP_DIR}/mvme.qch")
    set(SPHINX_QHC_OUT          "${SPHINX_QTHELP_DIR}/mvme.qhc")

    # Copies the footer logo for the latex build.
    configure_file(
        "${SPHINX_SOURCE_DIR}/images/mesytec-logo-yellow.png"
        "${SPHINX_LATEX_DIR}/mesytec-logo-yellow.png"
        COPYONLY
        )

    # Copy sphinx extension directory to build tree.
    add_custom_target(mvme_doc_sphinx_copy_ext
        ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ext"
        "${CMAKE_CURRENT_BINARY_DIR}/ext"
        )

    add_custom_target(mvme_doc ALL)

    # html
    # ==================================================

    configure_file(
        "${SPHINX_SOURCE_DIR}/conf.py.in"
        "${SPHINX_BUILD_BASE}/html/conf.py"
        @ONLY
        )

    add_custom_target(mvme_doc_html
            ${SPHINX_EXECUTABLE}
            -E -b html
            -c "${SPHINX_BUILD_BASE}/html"
            -d "${SPHINX_CACHE_BASE}/html"
            "${SPHINX_SOURCE_DIR}"
            "${SPHINX_HTML_DIR}"
        COMMENT "Building HTML documentation with Sphinx"
        )

    add_dependencies(mvme_doc_html mvme_doc_sphinx_copy_ext)
    add_dependencies(mvme_doc mvme_doc_html)

    install(DIRECTORY "${SPHINX_HTML_DIR}" DESTINATION doc)

    # latex + pdf
    # ==================================================
    if (LATEXMK)
        configure_file(
            "${SPHINX_SOURCE_DIR}/conf.py.in"
            "${SPHINX_BUILD_BASE}/latex/conf.py"
            @ONLY
            )

        add_custom_target(mvme_doc_latex
            ${SPHINX_EXECUTABLE}
            -E -b latex
            -c "${SPHINX_BUILD_BASE}/latex"
            -d "${SPHINX_CACHE_BASE}/latex"
            "${SPHINX_SOURCE_DIR}"
            "${SPHINX_LATEX_DIR}"
            COMMENT "Building latex documentation with Sphinx"
            )
        add_dependencies(mvme_doc_latex mvme_doc_sphinx_copy_ext)

        # latex -> pdf
        # Trap here: as I'm using ninja for most builds ${CMAKE_MAKE_PROGRAM} is
        # set to 'ninja' not 'make' but sphinx generates a Makefile, so the ninja
        # build will fail here!
        # I'm now calling 'make' manually to work around this issue. This might
        # brake under windows as make could be 'mingw32-make' or 'nmake' or
        # something else.

        #add_custom_target(doc_latex_pdf
        #        ${CMAKE_MAKE_PROGRAM} -C ${SPHINX_LATEX_DIR}
        #    COMMENT "Generating PDF from Sphinx latex output"
        #    )

        add_custom_target(mvme_doc_latex_pdf
                make -C ${SPHINX_LATEX_DIR}
                COMMENT "Generating PDF from Sphinx latex output")

        add_dependencies(mvme_doc_latex_pdf mvme_doc_latex)
        add_dependencies(mvme_doc mvme_doc_latex_pdf)

        install(FILES "${SPHINX_PDF_OUT}" DESTINATION doc)
    else(LATEXMK)
        message("-- The latexmk program was not found on your system. Will not generate PDF documentation.")
    endif(LATEXMK)

    # qthelp
    # ==================================================

    if (QCOLLECTIONGENERATOR)
        configure_file(
            "${SPHINX_SOURCE_DIR}/conf.py.in"
            "${SPHINX_BUILD_BASE}/qthelp/conf.py"
            @ONLY
            )

        # produces mvme.qhp and mvme.qhcp
        add_custom_target(mvme_doc_qthelp
            ${SPHINX_EXECUTABLE}
            -E -b qthelp
            -c "${SPHINX_BUILD_BASE}/qthelp"
            -d "${SPHINX_CACHE_BASE}/qthelp"
            "${SPHINX_SOURCE_DIR}"
            "${SPHINX_QTHELP_DIR}"
            COMMENT "Building qthelp documentation with Sphinx"
            BYPRODUCTS
                "${SPHINX_QTHELP_DIR}/mvme.qhp"
                "${SPHINX_QTHELP_DIR}/mvme.qhcp"
            )
        add_dependencies(mvme_doc_qthelp mvme_doc_sphinx_copy_ext)

        # qthelp collection
        # Produces mvme.qhc and mvme.qch
        add_custom_command(
            DEPENDS "${SPHINX_QTHELP_DIR}/mvme.qhp"
            OUTPUT "${SPHINX_QCH_OUT}" "${SPHINX_QHC_OUT}"
            COMMAND qcollectiongenerator "${SPHINX_QTHELP_DIR}/mvme.qhcp"
            COMMENT "Generating qthelp collection"
            )

        # The dependency on the QCH_OUT is what makes CMake actually run the command.
        # See https://cmake.org/Wiki/CustomCommandCustomTargetInstall for details.
        add_custom_target(mvme_doc_qthelp_collection
            ALL DEPENDS "${SPHINX_QHC_OUT}"
            )

        add_dependencies(mvme_doc_qthelp_collection mvme_doc_qthelp)
        add_dependencies(mvme_doc mvme_doc_qthelp_collection)

        install(FILES "${SPHINX_QHC_OUT}" "${SPHINX_QCH_OUT}" DESTINATION doc)

        add_custom_command(TARGET mvme_doc
            DEPENDS "${SPHINX_QHC_OUT}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SPHINX_QHC_OUT}" "${SPHINX_QCH_OUT}" "${CMAKE_BINARY_DIR}/doc")
    endif(QCOLLECTIONGENERATOR)
else()
    message("-- sphinx-build was not found on your system. Will not generate documentation.")
endif()

# vim:tw=0
