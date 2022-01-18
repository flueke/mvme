# TODO: asan

include(GenerateExportHeader)

# Qt5
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt5Concurrent REQUIRED)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Network REQUIRED)
find_package(Qt5OpenGL REQUIRED)
find_package(Qt5PrintSupport REQUIRED)
find_package(Qt5Svg REQUIRED)
find_package(Qt5Widgets REQUIRED)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
    #add_definitions(-DQT_NO_DEBUG)
    add_definitions(-DQT_NO_DEBUG_OUTPUT)
endif()

find_package(Threads)

# libusb-0.1
if (WIN32)
    find_path(LIBUSB_INCLUDE_PATH lusb0_usb.h
        PATHS "C:/libusb-win32-bin-1.2.6.0/include")

    if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
        message("CMAKE_FIND_LIBRARY_PREFIXES=${CMAKE_FIND_LIBRARY_PREFIXES}")
        message("CMAKE_FIND_LIBRARY_SUFFIXES=${CMAKE_FIND_LIBRARY_SUFFIXES}")
        set(CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES};.dll")
        find_library(LIBUSB_LIBRARY
            NAMES usb0 libusb0
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
# libmvme_resources
#
qt5_add_resources(LIBMVME_RES_RC_OUT resources.qrc)
add_library(libmvme_resources SHARED
    ${LIBMVME_RES_RC_OUT})

set_target_properties(libmvme_resources PROPERTIES OUTPUT_NAME mvme_resources)

target_link_libraries(libmvme_resources
    PUBLIC Qt5::Core
    )

install(TARGETS libmvme_resources
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)

#
# libmvme_core
#
#qt5_add_resources(LIBMVME_CORE_RC_OUT resources.qrc)

add_library(libmvme_core STATIC
    "${CMAKE_CURRENT_BINARY_DIR}/git_sha1.cc"
    "${CMAKE_CURRENT_BINARY_DIR}/build_info.cc"
    #${LIBMVME_CORE_RC_OUT}
    CVMUSBReadoutList.cpp
    qt_util.cc
    vme_controller.cpp
    vme_script.cc
    vme_script_exec.cc
    vme_script_util.cc
    vme_script_variables.cc
    vmusb.cpp
    util.cc)

set_target_properties(libmvme_core PROPERTIES OUTPUT_NAME mvme_core)
generate_export_header(libmvme_core)

target_include_directories(libmvme_core PRIVATE ${QUAZIP_INCLUDE_DIR})
target_include_directories(libmvme_core PRIVATE ${LIBUSB_INCLUDE_PATH})

target_link_libraries(libmvme_core
    PRIVATE liba2_static
    PRIVATE ${LIBUSB_LIBRARY}
    PUBLIC mesytec-mvlc
    PUBLIC ${QUAZIP_QT5}
    PUBLIC Qt5::Concurrent
    PUBLIC Qt5::Gui
    PUBLIC Qt5::Network
    PUBLIC Qt5::OpenGL
    PUBLIC Qt5::Svg
    PUBLIC Qt5::Widgets
    PUBLIC libmvme_resources
    )

target_compile_options(libmvme_core
    PRIVATE $<${not-msvc}:-Wall -Wextra>
    PRIVATE $<${not-msvc}:-Wno-deprecated-declarations> # disable Qt deprecation warnings
    )

if (WIN32)
    target_link_libraries(libmvme_core PRIVATE ws2_32)
    target_compile_options(libmvme_core PRIVATE $<${not-msvc}:-Wno-format>)
endif()


install(TARGETS libmvme_core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)

#
# libmvme_mvlc - MVLC support
#
#set(MVLC_USB_LOG_LEVEL "LOG_LEVEL_OFF" CACHE STRING
#    "mvlc_usb layer log level: LOG_LEVEL_(WARN|INFO|DEBUG|TRACE|OFF)")

#set(MVLC_ETH_LOG_LEVEL "LOG_LEVEL_OFF" CACHE STRING
#    "mvlc_eth layer log level: LOG_LEVEL_(WARN|INFO|DEBUG|TRACE|OFF)")

#set(MVLC_DIALOG_LOG_LEVEL "LOG_LEVEL_OFF" CACHE STRING
#    "mvlc_dialog layer log level: LOG_LEVEL_(WARN|INFO|DEBUG|TRACE|OFF)")

#set(MVLC_READOUT_PARSER_LOG_LEVEL "LOG_LEVEL_OFF" CACHE STRING
#    "mvlc readout parser log level: LOG_LEVEL_(WARN|INFO|DEBUG|TRACE|OFF)")

qt5_wrap_ui(MVLC_DEV_GUI_UIC
    vme_debug_widget.ui
    mvlc/mvlc_dev_ui.ui
    )

add_library(libmvme_mvlc STATIC
    ${MVLC_DEV_GUI_UIC}
    mvlc/mvlc_dev_gui.cc
    mvlc/mvlc_qt_object.cc
    mvlc/mvlc_register_names.cc
    mvlc/mvlc_script.cc
    mvlc/mvlc_util.cc
    mvlc/mvlc_vme_controller.cc
    mvlc/mvlc_vme_debug_widget.cc
    mvlc/readout_parser_support.cc
    )

set_target_properties(libmvme_mvlc PROPERTIES OUTPUT_NAME mvme_mvlc)
generate_export_header(libmvme_mvlc)

# FIXME: somehow break up the circular dependency between libmvme_core and
# libmvme_mvlc. It is caused by vme_script::run_command() directly using
# MVLC_VMEController and libmvme_mvlc making use of vme_script in
# mvlc/mvlc_script.h
target_link_libraries(libmvme_mvlc PUBLIC mesytec-mvlc PUBLIC libmvme_core)
target_link_libraries(libmvme_core PUBLIC libmvme_mvlc)

if (WIN32)
    target_link_libraries(libmvme_mvlc PRIVATE ws2_32)
    target_compile_options(libmvme_mvlc PRIVATE -Wno-format)
endif(WIN32)

#target_compile_definitions(libmvme_mvlc PRIVATE -DMVLC_USB_LOG_LEVEL=${MVLC_USB_LOG_LEVEL})
#target_compile_definitions(libmvme_mvlc PRIVATE -DMVLC_ETH_LOG_LEVEL=${MVLC_ETH_LOG_LEVEL})
#target_compile_definitions(libmvme_mvlc PRIVATE -DMVLC_DIALOG_LOG_LEVEL=${MVLC_DIALOG_LOG_LEVEL})
#target_compile_definitions(libmvme_mvlc PRIVATE -DMVLC_READOUT_PARSER_LOG_LEVEL=${MVLC_READOUT_PARSER_LOG_LEVEL})
target_compile_options(libmvme_mvlc
    PRIVATE $<${not-msvc}:-Wall -Wextra>
    PRIVATE $<${not-msvc}:-Wno-deprecated-declarations> # disable Qt deprecation warnings
    )

install(TARGETS libmvme_mvlc
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)

# mvlc_dev_gui
add_executable(mvlc_dev_gui mvlc/mvlc_dev_gui_main.cc)
target_link_libraries(mvlc_dev_gui PRIVATE libmvme_mvlc)
install(TARGETS mvlc_dev_gui
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)

# mvlc_dev_win_usb_debug
#if (WIN32)
#    add_executable(mvlc_dev_win_usb_debug mvlc/mvlc_dev_win_usb_debug.cc)
#    target_link_libraries(mvlc_dev_win_usb_debug
#        PRIVATE libmvme_mvlc
#        )
#endif (WIN32)

add_executable(dev_mvlc_vmescript_runner mvlc/dev_mvlc_vmescript_runner.cc)
target_link_libraries(dev_mvlc_vmescript_runner PRIVATE libmvme_mvlc)

if(MVME_BUILD_TESTS)
    function(add_mvlc_gtest exe_name exe_source)
        add_executable(${exe_name} ${exe_source})
        add_dependencies(${exe_name} libmvme_mvlc)
        target_link_libraries(${exe_name} libmvme_mvlc gtest gtest_main)
        add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
        target_compile_options(${exe_name} PRIVATE $<${not-msvc}:-Wall -Wextra>)
    endfunction(add_mvlc_gtest)
    add_mvlc_gtest(test_mvlc_script mvlc/test/test_mvlc_script.cc)
    add_mvlc_gtest(test_mvlc_calc_packet_loss mvlc/test/test_mvlc_calc_packet_loss.cc)
endif(MVME_BUILD_TESTS)

#
# libmvme
#

qt5_wrap_ui(LIBMVME_UIC_OUT mesytec_diagnostics.ui vme_debug_widget.ui)

add_library(libmvme SHARED
    "${CMAKE_SOURCE_DIR}/external/qt-collapsible-section/Section.cpp"
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
    analysis_bench.cc
    daqcontrol.cc
    daqcontrol_widget.cc
    daqstats_widget.cc
    data_filter.cc
    data_filter_edit.cc
    file_autosaver.cc
    globals.cc
    gui_util.cc
    histo1d.cc
    histo1d_util.cc
    histo1d_widget.cc
    histo1d_widget_p.cc
    histo2d.cc
    histo2d_widget.cc
    histo2d_widget_p.cc
    histo_gui_util.cc
    histo_util.cc
    listfile_browser.cc
    listfile_replay.cc
    listfile_replay_worker.cc
    logfile_helper.cc
    mesytec_diagnostics.cc
    multi_event_splitter.cc
    mvme_context.cc
    mvme_context_lib.cc
    mvme.cpp
    mvme.cpp
    mvme_listfile.cc
    mvme_listfile_utils.cc
    mvme_listfile_worker.cc
    mvme_qthelp.cc
    mvme_qwt.cc
    mvme_session.cc
    mvme_stream_iter.cc
    mvme_stream_processor.cc
    mvme_stream_util.cc
    mvme_stream_worker.cc
    qt_assistant_remote_control.cc
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
    stream_worker_base.cc
    template_system.cc
    timed_block.cc
    treewidget_utils.cc
    util/qt_logview.cc
    util/qt_monospace_textedit.cc
    util/qwt_scalepicker.cpp
    util/strings.cc
    util/variablify.cc
    util/qledindicator.cpp
    util_zip.cc
    vme_analysis_common.cc
    vme_config.cc
    vme_config_json_schema_updates.cc
    vme_config_scripts.cc
    vme_config_tree.cc
    vme_config_ui.cc
    vme_config_ui_event_variable_editor.cc
    vme_config_ui_variable_editor.cc
    vme_config_util.cc
    vme_config_version.cc
    vme_controller_factory.cc
    vme_controller_ui.cc
    vme_daq.cc
    vme_debug_widget.cc
    vme_readout_worker.cc
    vme_script_editor.cc
    vmusb_buffer_processor.cc
    vmusb_firmware_loader.cc
    vmusb_readout_worker.cc
    vmusb_skipHeader.cpp
    vmusb_stack.cc
    vmusb_util.cc
    )

set_target_properties(libmvme PROPERTIES OUTPUT_NAME mvme)
generate_export_header(libmvme)

target_link_libraries(libmvme
    PUBLIC mesytec-mvlc
    PUBLIC libmvme_core
    PUBLIC ${QWT_LIBRARY}
    PUBLIC cpp11-on-multicore
    PUBLIC Qt5::PrintSupport
    PRIVATE nlohmann-json
    PRIVATE liba2_static
    PRIVATE Threads::Threads
    PRIVATE jcon
    )

target_compile_options(libmvme
    PRIVATE $<${not-msvc}:-Wall -Wextra>
    PRIVATE $<${not-msvc}:-Wno-deprecated-declarations> # disable Qt deprecation warnings
    )

if (WIN32)
    target_link_libraries(libmvme
        PRIVATE winmm
        PRIVATE ws2_32)
    target_compile_options(libmvme PRIVATE -Wno-format)
endif()

target_include_directories(libmvme
    PRIVATE ${CMAKE_SOURCE_DIR}/external
    PRIVATE "${CMAKE_SOURCE_DIR}/external/jcon-cpp/src/"
    PUBLIC ${QUAZIP_INCLUDE_DIR}
    SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/external/pcg-cpp-0.98/include/"
    )

target_sources(libmvme PRIVATE
    mvlc_daq.cc
    mvlc_listfile_worker.cc
    mvlc/mvlc_trigger_io.cc
    mvlc/mvlc_trigger_io_editor.cc
    mvlc/mvlc_trigger_io_editor_p.cc
    mvlc/mvlc_trigger_io_script.cc
    mvlc/mvlc_trigger_io_util.cc
    mvlc_readout_worker.cc
    mvlc_stream_worker.cc
    mvlc/trigger_io_dso.cc
    mvlc/trigger_io_dso_plot_widget.cc
    mvlc/trigger_io_dso_sim_ui.cc
    mvlc/trigger_io_sim.cc
    mvlc/trigger_io_sim_pinaddress.cc
    mvlc/vmeconfig_from_crateconfig.cc
    mvlc/vmeconfig_to_crateconfig.cc
    mvme_mvlc_listfile.cc
)

target_link_libraries(libmvme
    PUBLIC libmvme_mvlc
    PUBLIC yaml-cpp
    )

# Note: the BUILD_INTERFACE line does not work on older CMake versions
# (tested with 3.7.2).
target_include_directories(libmvme
    PRIVATE ${MVME_YAML_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/external/minbool
    #PRIVATE $<BUILD_INTERFACE:${MVME_YAML_CPP_DIR}/include>
    )

# The above still did not work under debian9. So the next line add
# yaml-cpp to the global include paths.
#include_directories(${MVME_YAML_DIR}/include)

# FIXME: revive this tool
#add_executable(dev_mvlc_daq_init dev_mvlc_daq_init.cc)
#target_link_libraries(dev_mvlc_daq_init libmvme)

add_executable(dev_mvlc_trigger_gui dev_mvlc_trigger_gui_main.cc)
target_link_libraries(dev_mvlc_trigger_gui libmvme)

add_executable(dev_import_mvlc_crateconfig dev_import_mvlc_crateconfig.cc)
target_link_libraries(dev_import_mvlc_crateconfig PRIVATE libmvme)

if (NOT WIN32)
    add_executable(dev_mvlc_dump_eth_file dev_mvlc_dump_eth_file.cc)
    target_link_libraries(dev_mvlc_dump_eth_file PRIVATE libmvme)
endif(NOT WIN32)

add_executable(mvlc_listfile_updater mvlc_listfile_updater.cc)
target_link_libraries(mvlc_listfile_updater PRIVATE libmvme)
install(TARGETS mvlc_listfile_updater
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)

install(TARGETS libmvme
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)

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

target_link_libraries(mvme
    PRIVATE libmvme
    )

target_include_directories(mvme PRIVATE ${CMAKE_SOURCE_DIR}/external)

target_compile_options(mvme
    PRIVATE $<${not-msvc}:-Wall -Wextra>
    PRIVATE $<${not-msvc}:-Wno-deprecated-declarations> # disable Qt deprecation warnings
    )

if (WIN32)
    target_compile_options(mvme PRIVATE -Wno-format)
endif()

install(TARGETS mvme
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)

# Helper for additional utility / debug binaries and alternative launchers.
function(add_mvme_executable exe_name exe_sources)
    add_executable(${exe_name} ${exe_sources})
    target_link_libraries(${exe_name} PRIVATE libmvme)
    target_include_directories(${exe_name} PRIVATE ${CMAKE_SOURCE_DIR}/external)
    target_compile_options(${exe_name} PRIVATE $<${not-msvc}:-Wall -Wextra>)
endfunction(add_mvme_executable)

#
# vme_script_checker
#
add_mvme_executable(vme_script_checker vme_script_checker.cc)

#
# vmusb_read_buffers_file
#
add_mvme_executable(vmusb_read_buffers_file "vmusb_read_buffers_file.cc")

add_mvme_executable(dev_data_filter_runner "dev_data_filter_runner.cc")
add_mvme_executable(dev_sis3153_read_raw_buffers_file "dev_sis3153_read_raw_buffers_file.cc")
add_mvme_executable(dev_listfile_tcp_sender "dev_listfile_tcp_sender.cc")
add_mvme_executable(dev_listfile_tcp_receiver "dev_listfile_tcp_receiver.cc")
#install(TARGETS dev_listfile_tcp_sender dev_listfile_tcp_receiver DESTINATION .)
add_mvme_executable(dev_listfile_dumper "dev_listfile_dumper.cc")
add_mvme_executable(dev_rate_monitoring "dev_rate_monitoring.cc")
add_mvme_executable(dev_rate_monitor_widget "dev_rate_monitor_widget.cc")

add_mvme_executable(dev_mvme_event_processing "dev_mvme_event_processing.cc")
add_mvme_executable(dev_histo1d_testing "dev_histo1d_testing.cc")
add_mvme_executable(dev_histo2d_polygon_cuts "dev_histo2d_polygon_cuts.cc")
add_mvme_executable(dev_zip_write_test zip-write-test.cc)
target_link_libraries(dev_zip_write_test PRIVATE Threads::Threads)
add_mvme_executable(dev_make_default_module_analyses "dev_make_default_module_analyses.cc")
add_mvme_executable(dev_replay_bench dev_replay_bench.cc)

if (WIN32)
    add_mvme_executable(dev_timeBeginPeriod_test "dev_timeBeginPeriod_test.cc")
    target_link_libraries(dev_timeBeginPeriod_test PRIVATE winmm)
endif(WIN32)


add_mvme_executable(mvme_to_mvlc mvme_to_mvlc.cc)
add_mvme_executable(dev_mvlc_qt_debug_client dev_mvlc_qt_debug_client.cc)
add_mvme_executable(dev_mvlc_dso dev_mvlc_dso.cc)
add_mvme_executable(dev_trigger_io_sim mvlc/dev_trigger_io_sim.cc)

#TODO: install a CMakeLists.txt for the treewriter client application. how to
# find the installed includes from that directory?
#install(FILES data_export/mvme_root_treewriter_client.cc DESTINATION extras/root_treewriter_client/)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_subdirectory(event_server)
add_subdirectory(listfile_reader)

#
# listfile recovery tool
#
if (NOT WIN32)
    find_library(ZLIB z)
    add_executable(mvme_recover_listfile mvme_recover_listfile_hack.c)
    target_link_libraries(mvme_recover_listfile ${ZLIB})

    install(TARGETS mvme_recover_listfile
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)
endif (NOT WIN32)

if(MVME_BUILD_TESTS)

    function(add_mvme_gtest exe_name exe_source)
        add_executable(${exe_name} ${exe_source})
        target_link_libraries(${exe_name}
            PRIVATE libmvme
            PRIVATE gtest
            PRIVATE gtest_main
            PRIVATE Qt5::Gui
            PRIVATE Qt5::Widgets
            )
        target_include_directories(${exe_name}
            PRIVATE ${CMAKE_SOURCE_DIR}/external
            )
        target_compile_options(${exe_name} PRIVATE $<${not-msvc}:-Wall -Wextra>)
        add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
    endfunction(add_mvme_gtest)

    add_mvme_gtest(test_object_visitor analysis/test_object_visitor.cc)
    add_mvme_gtest(test_analysis_util analysis/test_analysis_util.cc)
    add_mvme_gtest(test_listfile_constants test_listfile_constants.cc)
    #add_mvme_gtest(test_analysis_session analysis/test_analysis_session.cc)
    add_mvme_gtest(test_trigger_io_sim mvlc/test/test_trigger_io_sim.cc)
    add_mvme_gtest(test_vmeconfig_to_crateconfig mvlc/vmeconfig_to_crateconfig.test.cc)

    add_mvme_executable(dev_qtwi_checkstate_test "analysis/dev_qtwi_checkstate_test.cc")

endif(MVME_BUILD_TESTS)

add_mvme_executable(cpp_vector_at_test "cpp_vector_at_test.cc")

# vim:tw=0
