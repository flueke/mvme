add_library(event_server_common INTERFACE)
target_include_directories(event_server_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)

# TODO: check install paths
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/common"
    DESTINATION include/mvme/event_server)

add_subdirectory(clients)
add_subdirectory(server)
