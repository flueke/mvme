add_library(mesytec-mvlc SHARED
    mvlc.cc
    mvlc_basic_interface.cc
    mvlc_factory.cc
    mvlc_impl_eth.cc
    #mvlc_impl_usb.cc
    util/ticketmutex.cc
    )

target_include_directories(mesytec-mvlc
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

target_link_libraries(mesytec-mvlc
    PRIVATE ftd3xx
    )

target_compile_options(mesytec-mvlc PRIVATE -Wall -Wextra)

include(GenerateExportHeader)
generate_export_header(mesytec-mvlc)
