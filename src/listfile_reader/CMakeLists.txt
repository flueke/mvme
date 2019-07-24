add_mvme_executable(listfile_reader "listfile_reader_main.cc")

add_library(listfile_reader_print_plugin SHARED listfile_reader_print_plugin.cc)

option(MVME_WITH_PYBIND11 "Enable usage of pybind11 in mvme" OFF)

if (MVME_WITH_PYBIND11)

    add_subdirectory(${CMAKE_SOURCE_DIR}/3rdparty/pybind11
                     ${CMAKE_BINARY_DIR}/3rdparty/pybind11)

    add_library(listfile_reader_python_plugin SHARED listfile_reader_python_plugin.cc)
    target_link_libraries(listfile_reader_python_plugin PRIVATE pybind11::embed)
    target_include_directories(listfile_reader_python_plugin
        PRIVATE ${PYBIND11_INCLUDE_DIR}
        PRIVATE ${PYTHON_INCLUDE_DIRS})

endif(MVME_WITH_PYBIND11)

# vim:tw=0
