# Build Sphinx documentation.
# Source: https://eb2.co/blog/2012/03/sphinx-and-cmake-beautiful-documentation-for-c-projects/

find_package(Sphinx)

if (SPHINX_EXECUTABLE)

    set(SPHINX_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source")
    set(SPHINX_BUILD_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_build")
    set(SPHINX_CACHE_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_doctrees")
    set(SPHINX_HTML_DIR   "${CMAKE_CURRENT_BINARY_DIR}/html")
    set(SPHINX_LATEX_DIR  "${CMAKE_CURRENT_BINARY_DIR}/latex")
    set(SPHINX_QTHELP_DIR  "${CMAKE_CURRENT_BINARY_DIR}/qthelp")

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

    add_custom_target(html_doc ALL
        ${SPHINX_EXECUTABLE}
        -b html
        -c "${SPHINX_BUILD_DIR}"
        -d "${SPHINX_CACHE_DIR}"
        "${SPHINX_SOURCE_DIR}"
        "${SPHINX_HTML_DIR}"
        COMMENT "Building HTML documentation with Sphinx"
        )

    add_custom_target(latex_doc ALL
        ${SPHINX_EXECUTABLE}
        -b latex
        -c "${SPHINX_BUILD_DIR}"
        -d "${SPHINX_CACHE_DIR}"
        "${SPHINX_SOURCE_DIR}"
        "${SPHINX_LATEX_DIR}"
        COMMENT "Building latex documentation with Sphinx"
        )

    add_custom_target(qthelp_doc ALL
        ${SPHINX_EXECUTABLE}
        -b qthelp
        -c "${SPHINX_BUILD_DIR}"
        -d "${SPHINX_CACHE_DIR}"
        "${SPHINX_SOURCE_DIR}"
        "${SPHINX_QTHELP_DIR}"
        COMMENT "Building qthelp documentation with Sphinx"
        )

    add_custom_target(doc)
    add_dependencies(doc html_doc latex_doc qthelp_doc)

endif()

# vim:tw=0
