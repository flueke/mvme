add_library(mesytec-mvlc SHARED
    mvlc_basic_interface.cc
    mvlc.cc
    mvlc_commands.cc
    mvlc_constants.cc
    mvlc_dialog.cc
    mvlc_error.cc
    mvlc_factory.cc
    mvlc_impl_eth.cc
    mvlc_impl_usb.cc
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

install(TARGETS mesytec-mvlc DESTINATION lib)
# TODO: install headers

if (BUILD_TESTS)
    function(add_gtest exe_name exe_source)
        add_executable(${exe_name} ${exe_source})

        target_link_libraries(${exe_name}
            PRIVATE mesytec-mvlc
            PRIVATE gtest
            PRIVATE gtest_main
            )

        add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
    endfunction(add_gtest)

    add_gtest(test_mvlc_commands mvlc_commands.test.cc)
endif(BUILD_TESTS)
