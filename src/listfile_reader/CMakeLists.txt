add_mvme_executable(listfile_reader "listfile_reader_main.cc")

add_library(listfile_reader_print_plugin SHARED listfile_reader_print_plugin.cc)

install(TARGETS listfile_reader DESTINATION bin)
install(TARGETS listfile_reader_print_plugin DESTINATION lib)

option(MVME_WITH_PYBIND11 "Enable usage of pybind11 in mvme" OFF)

if (MVME_WITH_PYBIND11)

    add_subdirectory(${CMAKE_SOURCE_DIR}/external/pybind11
                     ${CMAKE_BINARY_DIR}/external/pybind11)

    add_library(listfile_reader_python_plugin SHARED listfile_reader_python_plugin.cc)
    target_link_libraries(listfile_reader_python_plugin PRIVATE pybind11::embed)
    target_include_directories(listfile_reader_python_plugin
        PRIVATE ${PYBIND11_INCLUDE_DIR}
        PRIVATE ${PYTHON_INCLUDE_DIRS})

    install(TARGETS listfile_reader_python_plugin DESTINATION lib)
    install(FILES listfile_reader_python_printer.py DESTINATION lib)
    file(COPY listfile_reader_python_printer.py DESTINATION ${CMAKE_BINARY_DIR})

endif(MVME_WITH_PYBIND11)

install(FILES listfile_reader.h listfile_reader_print_plugin.cc
    listfile_reader_python_plugin.cc listfile_reader_python_printer.py
    DESTINATION share/listfile_reader)

install(FILES CMakeLists_to_install.txt DESTINATION share/listfile_reader
    RENAME CMakeLists.txt)

# vim:tw=0
