add_executable(mvlc-cli mvlc_cli_main.cc)
target_link_libraries(mvlc-cli PRIVATE mesytec-mvlc)

install(TARGETS mvlc-cli RUNTIME DESTINATION bin)
