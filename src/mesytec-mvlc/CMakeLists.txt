find_package(Threads)

configure_file("git_version.cc.in" "git_version.cc" @ONLY)

add_library(mesytec-mvlc SHARED
    "${CMAKE_CURRENT_BINARY_DIR}/git_version.cc"
    mvlc_basic_interface.cc
    mvlc.cc
    mvlc_command_builders.cc
    mvlc_constants.cc
    mvlc_dialog.cc
    mvlc_dialog_util.cc
    mvlc_error.cc
    mvlc_factory.cc
    mvlc_impl_eth.cc
    mvlc_impl_support.cc
    mvlc_impl_usb.cc
    mvlc_listfile.cc
    mvlc_listfile_zip.cc
    mvlc_readout.cc
    mvlc_readout_parser.cc
    mvlc_stack_executor.cc
    mvlc_util.cc
    util/string_util.cc
    util/threadsafequeue.cc
    util/ticketmutex.cc
    util/readout_buffer.cc
    util/readout_buffer_queues.cc
    )

target_include_directories(mesytec-mvlc
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/>
    )

target_link_libraries(mesytec-mvlc
    PRIVATE fmt::fmt-header-only # does add -DFMT_HEADER_ONLY=1
    PRIVATE lz4_static
    PRIVATE minizip
    PRIVATE yaml-cpp
    PUBLIC Threads::Threads
    )

if (UNIX AND NOT APPLE)
    target_link_libraries(mesytec-mvlc PRIVATE ftd3xx-static)
else()
    target_link_libraries(mesytec-mvlc PRIVATE ftd3xx)
endif()

if (WIN32)
    target_link_libraries(mesytec-mvlc PRIVATE ws2_32)
endif(WIN32)

target_compile_options(mesytec-mvlc PRIVATE -Wall -Wextra)

include(GenerateExportHeader)
generate_export_header(mesytec-mvlc)

install(TARGETS mesytec-mvlc
    EXPORT mesytec-mvlc
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    )

#export(EXPORT mesytec-mvlc FILE mesytec-mvlc.cmake)

install(EXPORT mesytec-mvlc
    DESTINATION lib/cmake/mesytec-mvlc
    NAMESPACE "mesytec-mvlc::"
    )

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/mesytec-mvlc
    FILES_MATCHING PATTERN "*.h")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mesytec-mvlc_export.h
    DESTINATION include/mesytec-mvlc)

if (MVLC_BUILD_TESTS)
    function(add_gtest exe_name exe_source)
        add_executable(${exe_name} ${exe_source})

        target_link_libraries(${exe_name}
            PRIVATE mesytec-mvlc
            PRIVATE gtest
            PRIVATE gtest_main
            PRIVATE fmt::fmt-header-only # does add -DFMT_HEADER_ONLY=1
            )

        add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
    endfunction(add_gtest)

    add_gtest(test_mvlc_command_builders mvlc_command_builders.test.cc)
    add_gtest(test_mvlc_listfile_zip mvlc_listfile_zip.test.cc)
    add_gtest(test_mvlc_stack_executor mvlc_stack_executor.test.cc)
    target_link_libraries(test_mvlc_listfile_zip PUBLIC minizip)
    add_gtest(test_mvlc_readout_config mvlc_readout_config.test.cc)
    add_gtest(test_threadsafequeue util/threadsafequeue.test.cc)
endif(MVLC_BUILD_TESTS)
