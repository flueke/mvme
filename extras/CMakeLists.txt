install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/mvme-listfile-dumper" DESTINATION "extras")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vm-usb" DESTINATION "extras")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/sis3153" DESTINATION "extras")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/mvlc" DESTINATION "extras")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/rcbus-example" DESTINATION "extras")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/metrics" DESTINATION "extras")
install(FILES     "${CMAKE_CURRENT_SOURCE_DIR}/initMVME" DESTINATION "bin")
install(PROGRAMS  "${CMAKE_CURRENT_SOURCE_DIR}/mvme.sh" DESTINATION "bin")

install(FILES mvme_jsonrpc_client.py mvme_jsonrpc_daq_runner.py mvme_jsonrpc_replay.py
    DESTINATION extras)

add_subdirectory(mvme-listfile-dumper)

# vim:tw=0
