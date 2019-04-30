# TODO: asan

include(GenerateExportHeader)

# Qt5
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt5Concurrent REQUIRED)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Network REQUIRED)
find_package(Qt5Widgets REQUIRED)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
    #add_definitions(-DQT_NO_DEBUG)
    add_definitions(-DQT_NO_DEBUG_OUTPUT)
endif()

find_package(Threads)

# quazip
find_path(QUAZIP_INCLUDE_DIR
    NAMES quazip.h
    PATH_SUFFIXES quazip quazip-qt5 quazip5)

include_directories(${QUAZIP_INCLUDE_DIR})

find_library(QUAZIP_QT5_RELEASE NAMES quazip quazip-qt5 quazip5)
find_library(QUAZIP_QT5_DEBUG NAMES quazipd quazip-qt5d quazip5d)

if (WIN32 OR QUAZIP_QT5_DEBUG)
    set(QUAZIP_QT5
        optimized ${QUAZIP_QT5_RELEASE}
        debug ${QUAZIP_QT5_DEBUG}
        )
else()
    set(QUAZIP_QT5 ${QUAZIP_QT5_RELEASE})
endif()

message("-- Using QUAZIP_INCLUDE_DIR=${QUAZIP_INCLUDE_DIR}")
message("-- Using QUAZIP_QT5=${QUAZIP_QT5}")

# libusb-0.1
if (WIN32)
    find_path(LIBUSB_INCLUDE_PATH lusb0_usb.h
        PATHS "C:/libusb-win32-bin-1.2.6.0/include")

    if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
        find_library(LIBUSB_LIBRARY usb0
            PATHS "C:/libusb-win32-bin-1.2.6.0/bin/amd64")
        message("Using 64 bit libusb: ${LIBUSB_LIBRARY}")
    else()
        find_library(LIBUSB_LIBRARY usb0_x86
            PATHS "C:/libusb-win32-bin-1.2.6.0/bin/x86")
        message("Using 32 bit libusb: ${LIBUSB_LIBRARY}")
    endif()
else()
    find_library(LIBUSB_LIBRARY usb)
    find_path(LIBUSB_INCLUDE_PATH usb.h)
    message("-- Using LIBUSB_LIBRARY=${LIBUSB_LIBRARY}")
endif()

# Boost
#set(Boost_USE_MULTITHREADED ON)
#set(Boost_USE_STATIC_LIBS   ON)
#
#if (WIN32)
#    add_definitions(-D_WIN32_WINNT=0x0501)
#endif(WIN32)
#
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
message("-- Found boost for mvme in ${Boost_INCLUDE_DIRS}")

# git version information is contained in its own translation unit. Allows for
# quick build + relink on version change.
configure_file("git_sha1.cc.in" "git_sha1.cc" @ONLY)

# make build info available to the program
set(BUILD_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
if (NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
    string(TOUPPER ${CMAKE_BUILD_TYPE} build_type_upper)
    set(BUILD_CXX_FLAGS "${BUILD_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${build_type_upper}}")
endif()
configure_file("build_info.cc.in" "build_info.cc" @ONLY)

#
# liba2 - Analysis runtime system
#
add_subdirectory(analysis/a2)
include_directories(analysis/a2)

# Enable WIENER VMUSB support using libusb0. This can be changed to make use of
# libusb1.
add_definitions(-DVME_CONTROLLER_WIENER -DWIENER_USE_LIBUSB0)

#
# libmvme_core
#
add_library(libmvme_core SHARED util.cc)
set_target_properties(libmvme_core PROPERTIES OUTPUT_NAME mvme_core)

qt5_use_modules(libmvme_core Concurrent Gui Network Widgets)
qt5_add_resources(LIBMVME_CORE_RC_OUT resources.qrc)

generate_export_header(libmvme_core)

target_sources(libmvme_core PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/git_sha1.cc"
    "${CMAKE_CURRENT_BINARY_DIR}/build_info.cc"
    ${LIBMVME_CORE_RC_OUT}
    CVMUSBReadoutList.cpp
    qt_util.cc
    util.cc
    vme_controller.cpp
    vme_script.cc
    vmusb.cpp
)

target_include_directories(libmvme_core PRIVATE ${LIBUSB_INCLUDE_PATH})
target_link_libraries(libmvme_core ${LIBUSB_LIBRARY})
install(TARGETS libmvme_core DESTINATION .)

if (WIN32)
    target_link_libraries(libmvme_core ws2_32)
endif()

#
# libmvme_mvlc - MVLC support
#
if(MVME_ENABLE_MVLC)
    set(MVLC_USB_DEBUG_LEVEL "LOG_LEVEL_WARN" CACHE STRING
        "mvlc_usb layer log level LOG_LEVEL_(WARN|INFO|DEBUG|TRACE)")

    set(MVLC_UDP_DEBUG_LEVEL "LOG_LEVEL_WARN" CACHE STRING
        "mvlc_udp layer log level LOG_LEVEL_(WARN|INFO|DEBUG|TRACE)")

    # libmvme_mvlc
    add_library(libmvme_mvlc SHARED mvlc/mvlc_qt_object.cc)
    set_target_properties(libmvme_mvlc PROPERTIES OUTPUT_NAME mvme_mvlc)
    target_link_libraries(libmvme_mvlc libmvme_core ftd3xx)

    target_compile_definitions(libmvme_mvlc PRIVATE -DMVLC_USB_LOG_LEVEL=${MVLC_USB_DEBUG_LEVEL})
    target_compile_definitions(libmvme_mvlc PRIVATE -DMVLC_UDP_LOG_LEVEL=${MVLC_UDP_DEBUG_LEVEL})

    qt5_use_modules(libmvme_mvlc Concurrent Gui Network Widgets)
    qt5_wrap_ui(LIBMVME_MVLC_UIC_OUT vme_debug_widget.ui)

    generate_export_header(libmvme_mvlc)

    target_sources(libmvme_mvlc PRIVATE
        mvlc/mvlc_basic_mvlc.cc
        mvlc/mvlc_dialog.cc
        mvlc/mvlc_error.cc
        mvlc/mvlc_impl_factory.cc
        mvlc/mvlc_impl_udp.cc
        mvlc/mvlc_impl_usb.cc
        mvlc/mvlc_qt_object.cc
        mvlc/mvlc_register_names.cc
        mvlc/mvlc_script.cc
        mvlc/mvlc_util.cc
        mvlc/mvlc_vme_controller.cc
        mvlc/mvlc_vme_debug_widget.cc

        ${LIBMVME_MVLC_UIC_OUT}
        )

    install(TARGETS libmvme_mvlc DESTINATION .)

    # mvlc_dev_gui
    add_executable(mvlc_dev_gui mvlc/mvlc_dev_gui.cc)
    qt5_wrap_ui(MVLC_DEV_GUI_UIC mvlc/mvlc_dev_ui.ui)
    target_sources(mvlc_dev_gui PRIVATE ${MVLC_DEV_GUI_UIC})
    target_link_libraries(mvlc_dev_gui libmvme_mvlc)

    if (WIN32)
        target_link_libraries(libmvme_mvlc ws2_32)
    endif(WIN32)

    # mvlc_dev_win_usb_debug
    if (WIN32)
        add_executable(mvlc_dev_win_usb_debug mvlc/mvlc_dev_win_usb_debug.cc)
        target_link_libraries(mvlc_dev_win_usb_debug libmvme_mvlc)
    endif (WIN32)

    add_executable(dev_ftdi_list_devices mvlc/dev_ftdi_list_devices)
    target_link_libraries(dev_ftdi_list_devices ftd3xx)

    add_executable(dev_test_mvlc_usb_impl mvlc/dev_test_mvlc_usb_impl.cc)
    target_link_libraries(dev_test_mvlc_usb_impl libmvme_mvlc)

    add_executable(dev_test_mvlc_udp_impl mvlc/dev_test_mvlc_udp_impl.cc)
    target_link_libraries(dev_test_mvlc_udp_impl libmvme_mvlc)

    install(TARGETS mvlc_dev_gui dev_test_mvlc_usb_impl dev_test_mvlc_udp_impl
        DESTINATION .)

    if(BUILD_TESTS)
        function(add_mvlc_gtest exe_name exe_source)
            add_executable(${exe_name} ${exe_source})
            add_dependencies(${exe_name} libmvme_mvlc)
            target_link_libraries(${exe_name} libmvme_mvlc gtest gtest_main)
            add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
        endfunction(add_mvlc_gtest)
        add_mvlc_gtest(test_mvlc_script mvlc/test/test_mvlc_script.cc)
        add_mvlc_gtest(test_mvlc_error mvlc/test/test_mvlc_error.cc)
        add_mvlc_gtest(test_mvlc_calc_packet_loss mvlc/test/test_mvlc_calc_packet_loss.cc)
    endif(BUILD_TESTS)

    # Standalone mvme-like binary specifically for the mvlc. This is used to
    # explore how to best integrate the mvlc and improve the mvme codebase as a
    # whole.
    add_executable(mvme_mvlc mvlc/mvme_mvlc.cc)
    target_link_libraries(mvme_mvlc libmvme)
endif(MVME_ENABLE_MVLC)

#
# libmvme
#
add_library(libmvme SHARED mvme.cpp)
set_target_properties(libmvme PROPERTIES OUTPUT_NAME mvme)
generate_export_header(libmvme)

qt5_use_modules(libmvme Concurrent Gui Network Widgets)
qt5_wrap_ui(LIBMVME_UIC_OUT mesytec_diagnostics.ui vme_debug_widget.ui)

target_sources(libmvme PRIVATE
    #"${CMAKE_CURRENT_BINARY_DIR}/git_sha1.cc"
    #"${CMAKE_CURRENT_BINARY_DIR}/build_info.cc"
    "${CMAKE_SOURCE_DIR}/3rdparty/qt-collapsible-section/Section.cpp"
    ${LIBMVME_UIC_OUT}

    analysis/a2_adapter.cc
    analysis/analysis.cc
    analysis/analysis_info_widget.cc
    analysis/analysis_serialization.cc
    analysis/analysis_session.cc
    analysis/analysis_ui.cc
    analysis/analysis_ui_p.cc
    analysis/analysis_util.cc
    analysis/code_editor.cc
    analysis/condition_ui.cc
    analysis/condition_ui_p.cc
    analysis/data_extraction_widget.cc
    analysis/exportsink_codegen.cc
    analysis/expression_operator_dialog.cc
    analysis/listfilter_extractor_dialog.cc
    analysis/object_editor_dialog.cc
    analysis/object_factory.cc
    analysis/object_info_widget.cc
    analysis/sink_widget_factory.cc
    analysis/ui_eventwidget.cc
    analysis/ui_lib.cc
    event_server/server/event_server_util.cc
    event_server/server/event_server.cc
    daqcontrol.cc
    daqcontrol_widget.cc
    daqstats_widget.cc
    data_filter.cc
    data_filter_edit.cc
    file_autosaver.cc
    globals.cc
    gui_util.cc
    histo1d.cc
    histo1d_widget.cc
    histo1d_widget_p.cc
    histo2d.cc
    histo2d_widget.cc
    histo2d_widget_p.cc
    histo_gui_util.cc
    histo_util.cc
    listfile_browser.cc
    mesytec_diagnostics.cc
    mvme_context.cc
    mvme_context_lib.cc
    mvme.cpp
    mvme_listfile.cc
    mvme_listfile_utils.cc
    mvme_qwt.cc
    mvme_session.cc
    mvme_stream_iter.cc
    mvme_stream_processor.cc
    mvme_stream_util.cc
    mvme_stream_worker.cc
    rate_monitor_base.cc
    rate_monitor_gui.cc
    rate_monitor_plot_widget.cc
    rate_monitor_widget.cc
    realtimedata.cpp
    remote_control.cc
    scrollbar.cpp
    scrollzoomer.cpp
    sis3153.cc
    sis3153_readout_worker.cc
    sis3153/sis3153ETH_vme_class.cpp
    sis3153_util.cc
    template_system.cc
    timed_block.cc
    treewidget_utils.cc
    util/strings.cc
    util_zip.cc
    vme_analysis_common.cc
    vme_config.cc
    vme_config_tree.cc
    vme_config_ui.cc
    vme_controller_factory.cc
    vme_controller_ui.cc
    vme_debug_widget.cc
    vme_daq.cc
    vme_readout_worker.cc
    vme_script_editor.cc
    vmusb_buffer_processor.cc
    vmusb_firmware_loader.cc
    vmusb_readout_worker.cc
    vmusb_skipHeader.cpp
    vmusb_stack.cc
    vmusb_util.cc
    )

target_link_libraries(libmvme
    libmvme_core
    ${QUAZIP_QT5}
    ${QWT_LIBRARY}
    liba2_static
    Threads::Threads
    jcon
    )

if (MVME_ENABLE_MVLC)
    target_sources(libmvme PRIVATE
        mvlc/mvlc_daq.cc
    )
    target_link_libraries(libmvme libmvme_mvlc)
endif()

install(TARGETS libmvme DESTINATION .)

#
# mvme - the main GUI binary
#
if (CMAKE_BUILD_TYPE MATCHES "^Release$")
    add_executable(mvme WIN32 main.cpp)
else()
    #add_definitions(-DMVME_ENABLE_TIMED_BLOCKS)
    add_executable(mvme main.cpp)
endif()

target_sources(mvme PRIVATE main.cpp mvme.rc)

qt5_use_modules(mvme
  Concurrent
  Gui
  Widgets
  )

target_link_libraries(mvme libmvme)

install(TARGETS mvme DESTINATION .)

# Helper for additional utility / debug binaries and alternative launchers.
function(add_mvme_executable exe_name exe_sources)
    add_executable(${exe_name} ${exe_sources})
    target_link_libraries(${exe_name} libmvme)
endfunction(add_mvme_executable)

#
# SIS3153 daq stop test
#
add_mvme_executable(sis3153_test sis3153_test.cc)

#
# mvme_register_reader
#
add_mvme_executable(mvme_register_reader mvme_register_reader.cc)

#
# vme_script_checker
#
add_mvme_executable(vme_script_checker vme_script_checker.cc)

#
# test_launcher_vme_module_template
#
add_mvme_executable(test_launcher_module_template "test_launcher_module_template.cc")

#
# vmusb_read_buffers_file
#
add_mvme_executable(vmusb_read_buffers_file "vmusb_read_buffers_file.cc")

add_mvme_executable(mvme-listfile-dumper-multievent "listfile-multievent-dumper.cpp")
add_mvme_executable(dev_datagen "dev_datagen.cc")
add_mvme_executable(dev_data_filter_runner "dev_data_filter_runner.cc")
add_mvme_executable(dev_sis3153_read_raw_buffers_file "dev_sis3153_read_raw_buffers_file.cc")
add_mvme_executable(dev_udp_sender "dev_udp_sender.cc")
add_mvme_executable(dev_udp_receiver "dev_udp_receiver.cc")
add_mvme_executable(dev_listfile_tcp_sender "dev_listfile_tcp_sender.cc")
add_mvme_executable(dev_listfile_tcp_receiver "dev_listfile_tcp_receiver.cc")
#install(TARGETS dev_listfile_tcp_sender dev_listfile_tcp_receiver DESTINATION .)
add_mvme_executable(dev_listfile_dumper "dev_listfile_dumper.cc")
add_mvme_executable(dev_rate_monitoring "dev_rate_monitoring.cc")
add_mvme_executable(dev_rate_monitor_widget "dev_rate_monitor_widget.cc")

add_mvme_executable(dev_mvme_event_processing "dev_mvme_event_processing.cc")
add_mvme_executable(dev_data_filter_edit "dev_data_filter_edit.cc")
add_mvme_executable(dev_histo1d_testing "dev_histo1d_testing.cc")
add_mvme_executable(dev_histo2d_polygon_cuts "dev_histo2d_polygon_cuts.cc")


set(INCLUDES_TO_INSTALL
    #mvme_data_server_proto.h
    #mvme_data_server_lib.h
    ../3rdparty/nlohmann/json.hpp
    )

install(FILES ${INCLUDES_TO_INSTALL} DESTINATION include)

#TODO: install a CMakeLists.txt for the treewriter client application. how to
# find the installed includes from that directory?
#install(FILES data_export/mvme_root_treewriter_client.cc DESTINATION extras/root_treewriter_client/)

add_subdirectory(event_server)

#
# listfile recovery tool
#
if (NOT WIN32)
    find_library(ZLIB z)
    add_executable(mvme_recover_listfile mvme_recover_listfile_hack.c)
    target_link_libraries(mvme_recover_listfile ${ZLIB})
    install(TARGETS mvme_recover_listfile DESTINATION .)
endif (NOT WIN32)

if(BUILD_TESTS)

    function(add_mvme_gtest exe_name exe_source)
        add_executable(${exe_name} ${exe_source})
        add_dependencies(${exe_name} libmvme)
        target_link_libraries(${exe_name} libmvme gtest gtest_main)
        add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
    endfunction(add_mvme_gtest)

    add_mvme_gtest(test_object_visitor analysis/test_object_visitor.cc)
    add_mvme_gtest(test_analysis_util analysis/test_analysis_util.cc)
    add_mvme_gtest(test_listfile_constants test_listfile_constants.cc)
    #add_mvme_gtest(test_analysis_session analysis/test_analysis_session.cc)

    add_mvme_executable(dev_qtwi_checkstate_test "analysis/dev_qtwi_checkstate_test.cc")

endif(BUILD_TESTS)



# vim:tw=0
