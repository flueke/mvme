include_directories(common)
add_subdirectory(clients)
add_subdirectory(server)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/common" DESTINATION include/event_server)
