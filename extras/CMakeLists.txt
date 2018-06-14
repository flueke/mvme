install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/mvme-listfile-dumper" DESTINATION "extras")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vm-usb" DESTINATION "extras")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/sis3153" DESTINATION "extras")
install(FILES     "${CMAKE_CURRENT_SOURCE_DIR}/mvme_jsonrpc_client.py" DESTINATION "extras")
add_subdirectory(mvme-listfile-dumper)

# vim:tw=0
