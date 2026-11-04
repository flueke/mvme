# Build and install the mvme_event_server_example_client binary. Also install the source
# code inside the 'share' directory of the installation tree.

add_executable(mvme_event_server_example_client mvme_event_server_example_client/mvme_event_server_example_client.cc)

target_link_libraries(mvme_event_server_example_client PRIVATE nlohmann-json)

if (WIN32)
    target_link_libraries(mvme_event_server_example_client PRIVATE ws2_32)
endif (WIN32)

if (MSVC)
    find_package(unofficial-getopt-win32 REQUIRED)
    target_link_libraries(mvme_event_server_example_client PRIVATE unofficial::getopt-win32::getopt)
endif()

install(TARGETS mvme_event_server_example_client
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/mvme_event_server_example_client/"
    DESTINATION share/mvme_event_server_example_client)

# Install the source code of the mvme_root_client into 'share'
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/mvme_root_client/"
    DESTINATION share/mvme_root_client)

if (pybind11_FOUND)
    add_executable(mvme_event_server_python_client mvme_event_server_python_client/mvme_event_server_python_client.cc)
    target_link_libraries(mvme_event_server_python_client PRIVATE nlohmann-json pybind11::embed)
    if (WIN32)
        target_link_libraries(mvme_event_server_python_client PRIVATE ws2_32)
    endif (WIN32)
    if (MSVC)
        target_link_libraries(mvme_event_server_python_client PRIVATE unofficial::getopt-win32::getopt)
    endif()
endif(pybind11_FOUND)
