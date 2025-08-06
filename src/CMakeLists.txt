# TODO: asan

include(GenerateExportHeader)

# Qt5
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt5Concurrent REQUIRED)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Network REQUIRED)
find_package(Qt5OpenGL REQUIRED)
find_package(Qt5PrintSupport REQUIRED)
find_package(Qt5Svg REQUIRED)
find_package(Qt5Widgets REQUIRED)
find_package(Qt5Xml REQUIRED)
find_package(Qt5WebEngineWidgets QUIET)

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
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.30)
    find_package(Boost CONFIG REQUIRED)
else()
    find_package(Boost REQUIRED)
endif()
include_directories(${Boost_INCLUDE_DIRS})
message("-- Found boost for mvme in ${Boost_INCLUDE_DIRS}")

# graphviz
find_package(Graphviz REQUIRED)
message("-- Using GRAPHVIZ_INCLUDE_DIRS=${GRAPHVIZ_INCLUDE_DIRS}")
message("-- Using GRAPHVIZ_LIBRARIES=${GRAPHVIZ_LIBRARIES}")

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
#include_directories(analysis/a2)

# Enable WIENER VMUSB support using libusb0. This can be changed to make use of
# libusb1.
add_definitions(-DVME_CONTROLLER_WIENER -DWIENER_USE_LIBUSB0)

#
# libmvme_resources
#
qt5_add_resources(LIBMVME_RES_RC_OUT mvme_resources.qrc)
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
# libmvme_mdpp_decode
#
add_library(libmvme_mdpp_decode SHARED
    mdpp-sampling/mdpp_decode.cc
    )

target_link_libraries(libmvme_mdpp_decode
    PUBLIC Qt5::Widgets
    PUBLIC mesytec-mvlc
    )

set_target_properties(libmvme_mdpp_decode PROPERTIES OUTPUT_NAME mvme_mdpp_decode)
generate_export_header(libmvme_mdpp_decode)

install(TARGETS libmvme_mdpp_decode RUNTIME DESTINATION bin LIBRARY DESTINATION lib)

#
# libmvme
#

qt5_wrap_ui(LIBMVME_UIC_OUT
    analysis/interval_condition_dialog.ui
    analysis/polygon_condition_dialog.ui
    analysis/mdpp_sample_decoder_monitor_widget.ui
    daq_run_settings_dialog.ui
    mesytec_diagnostics.ui
    mvlc/mvlc_dev_ui.ui
    replay_widget.ui
    vme_debug_widget.ui
    vme_debug_widget.ui
    )

add_library(libmvme SHARED
    ${LIBMVME_UIC_OUT}
    "${CMAKE_SOURCE_DIR}/external/qt-collapsible-section/Section.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/build_info.cc"
    "${CMAKE_CURRENT_BINARY_DIR}/git_sha1.cc"
    #${LIBMVME_CORE_RC_OUT}
    CVMUSBReadoutList.cpp
    analysis/a2_adapter.cc
    analysis/analysis.cc
    analysis/analysis_graphs.cc
    analysis/analysis_info_widget.cc
    analysis/analysis_serialization.cc
    analysis/analysis_session.cc
    analysis/analysis_ui.cc
    analysis/analysis_ui_p.cc
    analysis/analysis_ui_util.cc
    analysis/analysis_util.cc
    analysis/code_editor.cc
    analysis/condition_ui.cc
    analysis/data_extraction_widget.cc
    analysis/exportsink_codegen.cc
    analysis/expression_operator_dialog.cc
    analysis/event_builder_monitor.cc
    analysis/histo_ops_widget.cc
    analysis/histo_ops_widget_p.cc
    analysis/listfilter_extractor_dialog.cc
    analysis/mdpp_sample_decoder_monitor_widget.cc
    analysis/object_editor_dialog.cc
    analysis/object_factory.cc
    analysis/object_info_widget.cc
    analysis/sink_widget_factory.cc
    analysis/ui_eventwidget.cc
    analysis/ui_lib.cc
    analysis/waveform_sink_1d_widget.cc
    analysis/waveform_sink_2d_widget.cc
    analysis/waveform_sink_widget_common.cc
    analysis_bench.cc
    analysis_service_provider.cc
    daqcontrol.cc
    daqcontrol_widget.cc
    daqstats_widget.cc
    data_filter.cc
    data_filter_edit.cc
    file_autosaver.cc
    globals.cc
    graphicsview_util.cc
    graphviz_util.cc
    gui_util.cc
    histo1d.cc
    histo1d_util.cc
    histo1d_widget.cc
    histo1d_widget_p.cc
    histo2d.cc
    histo2d_widget.cc
    histo2d_widget_p.cc
    histo_algo.cc
    histo_gui_util.cc
    histo_stats_widget.cc
    histo_ui.cc
    histo_util.cc
    listfile_browser.cc
    listfile_filtering.cc
    listfile_recovery.cc
    listfile_recovery_wizard.cc
    listfile_replay.cc
    listfile_replay_worker.cc
    logfile_helper.cc
    mdpp-sampling/waveform_plotting.cc
    mdpp-sampling/waveform_interpolation.cc
    mdpp-sampling/waveform_traces.cc
    mesytec_diagnostics.cc
    multi_crate.cc
    multi_crate_gui.cc
    multi_crate_mainwindow.cc
    multi_crate_nng.cc
    multi_crate_nng_gui.cc
    multi_event_splitter.cc
    multiplot_widget.cc
    multiplot_widget_p.cc
    mvlc/mvlc_dev_gui.cc
    mvlc/mvlc_qt_object.cc
    mvlc/mvlc_register_names.cc
    mvlc/mvlc_script.cc
    mvlc/mvlc_trigger_io.cc
    mvlc/mvlc_trigger_io_editor.cc
    mvlc/mvlc_trigger_io_editor_p.cc
    mvlc/mvlc_trigger_io_script.cc
    mvlc/mvlc_trigger_io_util.cc
    mvlc/mvlc_util.cc
    mvlc/mvlc_vme_controller.cc
    mvlc/mvlc_vme_debug_widget.cc
    mvlc/scanbus_ui.cc
    mvlc/trigger_io_dso.cc
    mvlc/trigger_io_dso_plot_widget.cc
    mvlc/trigger_io_dso_sim_ui.cc
    mvlc/trigger_io_sim.cc
    mvlc/trigger_io_sim_pinaddress.cc
    mvlc/vmeconfig_from_crateconfig.cc
    mvlc/vmeconfig_to_crateconfig.cc
    mvlc_daq.cc
    mvlc_listfile_worker.cc
    mvlc_readout_worker.cc
    mvlc_stream_worker.cc
    mvme.cpp
    mvme.cpp
    mvme_context.cc
    mvme_context_lib.cc
    mvme_listfile.cc
    mvme_listfile_utils.cc
    mvme_listfile_worker.cc
    mvme_mvlc_listfile.cc
    mvme_qthelp.cc
    mvme_qwt.cc
    mvme_session.cc
    mvme_stream_iter.cc
    mvme_stream_processor.cc
    mvme_stream_util.cc
    mvme_stream_worker.cc
    mvme_tcp_stream_server.cc
    mvmecontext_analysis_service_provider.cc
    qt_assistant_remote_control.cc
    qt_util.cc
    rate_monitor_base.cc
    rate_monitor_gui.cc
    rate_monitor_plot_widget.cc
    rate_monitor_widget.cc
    realtimedata.cpp
    remote_control.cc
    replay_ui.cc
    replay_ui_p.cc
    scrollbar.cpp
    scrollzoomer.cpp
    sis3153.cc
    sis3153/sis3153ETH_vme_class.cpp
    sis3153_readout_worker.cc
    sis3153_util.cc
    stream_worker_base.cc
    stream_processor_consumers.cc
    template_system.cc
    timed_block.cc
    treewidget_utils.cc
    util.cc
    util/mesy_nng_pipeline.cc
    util/qledindicator.cpp
    util/qt_eventfilters.cc
    util/qt_gui_io.cc
    util/qt_logview.cc
    util/qt_model_view_util.cc
    util/qt_monospace_textedit.cc
    util/qt_plaintextedit.cc
    util/qwt_scalepicker.cpp
    util/strings.cc
    util/thread_name.cc
    util/variablify.cc
    util/version_compare.cc
    util_zip.cc
    vme_analysis_common.cc
    vme_config.cc
    vme_config_json_schema_updates.cc
    vme_config_model_view.cc
    vme_config_scripts.cc
    vme_config_tree.cc
    vme_config_ui.cc
    vme_config_ui_event_variable_editor.cc
    vme_config_ui_variable_editor.cc
    vme_config_util.cc
    vme_config_version.cc
    vme_controller.cpp
    vme_controller_factory.cc
    vme_controller_ui.cc
    vme_daq.cc
    vme_debug_widget.cc
    vme_readout_worker.cc
    vme_script.cc
    vme_script_editor.cc
    vme_script_exec.cc
    vme_script_util.cc
    vme_script_variables.cc
    vmusb.cpp
    vmusb_buffer_processor.cc
    vmusb_firmware_loader.cc
    vmusb_readout_worker.cc
    vmusb_skipHeader.cpp
    vmusb_stack.cc
    vmusb_util.cc
    widget_registry.cc
    )

set_target_properties(libmvme PROPERTIES OUTPUT_NAME mvme)
generate_export_header(libmvme)

target_link_libraries(libmvme
    PRIVATE ${GRAPHVIZ_CDT_LIBRARY}
    PRIVATE ${GRAPHVIZ_CGRAPH_LIBRARY}
    PRIVATE ${GRAPHVIZ_GVC_LIBRARY}
    PRIVATE ${LIBUSB_LIBRARY}
    PRIVATE Threads::Threads
    PRIVATE jcon
    PRIVATE asio
    PUBLIC liba2_static
    PRIVATE nlohmann-json
    PRIVATE qgvcore
    PRIVATE range-v3::range-v3
    PUBLIC ${QUAZIP_QT5}
    PUBLIC ${QWT_LIBRARY}
    PUBLIC BFG::Lyra
    PUBLIC Qt5::Concurrent
    PUBLIC Qt5::Gui
    PUBLIC Qt5::Network
    PUBLIC Qt5::OpenGL
    PUBLIC Qt5::PrintSupport
    PUBLIC Qt5::Svg
    PUBLIC Qt5::Widgets
    PUBLIC Qt5::Xml
    PUBLIC cpp11-on-multicore
    PUBLIC libmvme_resources
    PUBLIC libmvme_mdpp_decode
    PUBLIC mesytec-mvlc
    PUBLIC nng
    PUBLIC yaml-cpp
    )

#add_compile_definitions(QT_DISABLE_DEPRECATED_UP_TO=0x060700)

target_compile_options(libmvme
    PRIVATE $<${not-msvc}:-Wall -Wextra>
    PRIVATE $<${not-msvc}:-Wno-deprecated-declarations> # disable Qt deprecation warnings
    PRIVATE $<${not-msvc}:-Werror=return-type> # turn missing returns into errors. these cases lead to weird crashes.
    PRIVATE -DQT_DISABLE_DEPRECATED_UP_TO=0x060700
    )

#target_include_directories(libmvme PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/analysis/a2)

find_package(prometheus-cpp CONFIG)

if (PROMETHEUS_CPP_ENABLE_PULL)
    message(">>> mvme: Found prometheus-cpp with PULL support. Enabling prometheus support.")
    target_link_libraries(libmvme PUBLIC prometheus-cpp::pull)
    target_sources(libmvme PRIVATE mvme_prometheus.cc)
    target_compile_definitions(libmvme PUBLIC -DMVME_ENABLE_PROMETHEUS)
endif()


if (WIN32)
    target_link_libraries(libmvme
        PUBLIC winmm
        PUBLIC ws2_32
        )
    target_compile_options(libmvme PRIVATE -Wno-format)
    target_compile_options(libmvme PRIVATE $<${not-msvc}:-Wno-format>)
endif()

target_include_directories(libmvme
    PRIVATE "${CMAKE_SOURCE_DIR}/external/jcon-cpp/src/"
    PRIVATE ${CMAKE_SOURCE_DIR}/external
    PRIVATE ${GRAPHVIZ_INCLUDE_DIRS}
    PRIVATE ${LIBUSB_INCLUDE_PATH}
    PUBLIC ${QUAZIP_INCLUDE_DIR}
    SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/external/pcg-cpp-0.98/include/"
    )

# Note: the BUILD_INTERFACE line does not work on older CMake versions
# (tested with 3.7.2).
target_include_directories(libmvme
    PRIVATE ${MVME_YAML_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/external/minbool
    #PRIVATE $<BUILD_INTERFACE:${MVME_YAML_CPP_DIR}/include>
    )

install(TARGETS libmvme RUNTIME DESTINATION bin LIBRARY DESTINATION lib)

# mvlc_dev_win_usb_debug
#if (WIN32)
#    add_executable(mvlc_dev_win_usb_debug mvlc/mvlc_dev_win_usb_debug.cc)
#    target_link_libraries(mvlc_dev_win_usb_debug
#        PRIVATE libmvme_mvlc
#        )
#endif (WIN32)

# FIXME: revive this tool
#add_executable(dev_mvlc_daq_init dev_mvlc_daq_init.cc)
#target_link_libraries(dev_mvlc_daq_init libmvme)

#
# mvme - the main GUI binary
#
if (CMAKE_BUILD_TYPE MATCHES "^Release$")
    #add_executable(mvme WIN32 main.cpp)
    add_executable(mvme main.cpp)
else()
    #add_definitions(-DMVME_ENABLE_TIMED_BLOCKS)
    add_executable(mvme main.cpp)
endif()

target_sources(mvme PRIVATE main.cpp mvme.rc)
target_link_libraries(mvme PRIVATE libmvme)
target_include_directories(mvme PRIVATE ${CMAKE_SOURCE_DIR}/external)

target_compile_options(mvme
    PRIVATE $<${not-msvc}:-Wall -Wextra>
    PRIVATE $<${not-msvc}:-Wno-deprecated-declarations> # disable Qt deprecation warnings
    )

if (WIN32)
    target_compile_options(mvme PRIVATE -Wno-format)
endif()

install(TARGETS mvme RUNTIME DESTINATION bin LIBRARY DESTINATION lib)

# Helper for additional utility / debug binaries and alternative launchers.
function(add_mvme_dev_exe exe_name exe_sources)
    if (CMAKE_BUILD_TYPE MATCHES "^Debug$")
        add_executable(${exe_name} ${exe_sources})
        target_link_libraries(${exe_name} PRIVATE libmvme)
        target_include_directories(${exe_name} PRIVATE ${CMAKE_SOURCE_DIR}/external)
        target_compile_options(${exe_name} PRIVATE $<${not-msvc}:-Wall -Wextra>)
    endif()
endfunction(add_mvme_dev_exe)
# Same as above for tools that should be part of release builds.
function(add_mvme_exe exe_name exe_sources)
        add_executable(${exe_name} ${exe_sources})
        target_link_libraries(${exe_name} PRIVATE libmvme)
        target_include_directories(${exe_name} PRIVATE ${CMAKE_SOURCE_DIR}/external)
        target_compile_options(${exe_name} PRIVATE $<${not-msvc}:-Wall -Wextra>)
endfunction(add_mvme_exe)

add_mvme_dev_exe(vme_script_checker vme_script_checker.cc)
add_mvme_dev_exe(vmusb_read_buffers_file "vmusb_read_buffers_file.cc")
add_mvme_dev_exe(dev_data_filter_runner "dev_data_filter_runner.cc")
add_mvme_dev_exe(dev_sis3153_read_raw_buffers_file "dev_sis3153_read_raw_buffers_file.cc")
add_mvme_dev_exe(dev_listfile_tcp_sender "dev_listfile_tcp_sender.cc")
add_mvme_dev_exe(dev_listfile_tcp_receiver "dev_listfile_tcp_receiver.cc")
add_mvme_dev_exe(dev_listfile_dumper "dev_listfile_dumper.cc")
add_mvme_dev_exe(dev_rate_monitoring "dev_rate_monitoring.cc")
add_mvme_dev_exe(dev_rate_monitor_widget "dev_rate_monitor_widget.cc")
add_mvme_dev_exe(dev_mvme_event_processing "dev_mvme_event_processing.cc")
add_mvme_dev_exe(dev_histo1d_testing "dev_histo1d_testing.cc")
add_mvme_dev_exe(dev_histo2d_polygon_cuts "dev_histo2d_polygon_cuts.cc")
add_mvme_dev_exe(dev_zip_write_test zip-write-test.cc)
add_mvme_dev_exe(dev_make_default_module_analyses "dev_make_default_module_analyses.cc")
add_mvme_dev_exe(dev_replay_bench dev_replay_bench.cc)
add_mvme_dev_exe(dev_module_sources_to_simple_json dev_module_sources_to_simple_json.cc)
add_mvme_dev_exe(dev_vme_configs_to_mvlc_json dev_vme_configs_to_mvlc_json.cc)
#add_mvme_dev_exe(dev_qtconcurrent dev_qtconcurrent.cc) # disabled due to some tbb issues

if (WIN32)
    add_mvme_dev_exe(dev_timeBeginPeriod_test "dev_timeBeginPeriod_test.cc")
    if (TARGET dev_timeBeginPeriod_test)
        target_link_libraries(dev_timeBeginPeriod_test PRIVATE winmm)
    endif()
endif(WIN32)

add_mvme_dev_exe(mvme_to_mvlc mvme_to_mvlc.cc)
add_mvme_dev_exe(dev_mvlc_qt_debug_client dev_mvlc_qt_debug_client.cc)
add_mvme_dev_exe(dev_mvlc_dso dev_mvlc_dso.cc)
add_mvme_dev_exe(dev_trigger_io_sim mvlc/dev_trigger_io_sim.cc)
add_mvme_dev_exe(mvme_multi_crate_gui multi_crate_gui_main.cc)
add_mvme_dev_exe(dev_multi_crate_config_merger dev_multi_crate_config_merger.cc)
add_mvme_dev_exe(dev_multi_crate_cli dev_multi_crate_cli.cc)
add_mvme_dev_exe(decode_mesytec_vme_data decode_mesytec_vme_data.cc)
add_mvme_dev_exe(dev_vme_config_model_view dev_vme_config_model_view.cc)
add_mvme_dev_exe(dev_histo_ui dev_histo_ui.cc)
add_mvme_dev_exe(dev_replay_ui dev_replay_ui_main.cc)
if (Qt5WebEngineWidgets_FOUND)
    add_mvme_dev_exe(dev_mvme_qwebengine dev_mvme_qwebengine.cc)
    if (TARGET dev_mvme_qwebengine)
    target_link_libraries(dev_mvme_qwebengine PRIVATE Qt5::WebEngineWidgets)
    endif()
endif()
#add_mvme_dev_exe(dev_qthelpengine_test dev_qthelpengine_test.cc)

add_mvme_dev_exe(dev_test_daqrunsettingsdialog dev_test_daqrunsettingsdialog.cc)
add_mvme_dev_exe(dev_mvlc_trigger_gui dev_mvlc_trigger_gui_main.cc)
add_mvme_dev_exe(dev_import_mvlc_crateconfig dev_import_mvlc_crateconfig.cc)
add_mvme_dev_exe(dev_qfiledialog_save_test "dev_qfiledialog_save_test.cc")
add_mvme_dev_exe(dev_listfile_recovery "dev_listfile_recovery.cc")
add_mvme_exe(mvlc_dev_gui mvlc/mvlc_dev_gui_main.cc)
install(TARGETS mvlc_dev_gui RUNTIME DESTINATION bin LIBRARY DESTINATION lib)
add_mvme_dev_exe(dev_mvlc_vmescript_runner mvlc/dev_mvlc_vmescript_runner.cc)

if (NOT WIN32)
    add_mvme_dev_exe(dev_mvlc_dump_eth_file dev_mvlc_dump_eth_file.cc)
endif(NOT WIN32)

add_mvme_dev_exe(mvlc_listfile_updater mvlc_listfile_updater.cc)
if (CMAKE_BUILD_TYPE MATCHES "^Debug$")
    install(TARGETS mvlc_listfile_updater RUNTIME DESTINATION bin LIBRARY DESTINATION lib)
endif()

add_mvme_dev_exe(cpp_vector_at_test "cpp_vector_at_test.cc")

add_mvme_dev_exe(dev_analysis_to_dot dev_analysis_to_dot.cc)

if (CMAKE_BUILD_TYPE MATCHES "^Debug$")
    add_mvme_dev_exe(dev_graphviz_lib_test0 dev_graphviz_lib_test0.cc)
    target_link_libraries(dev_graphviz_lib_test0 PRIVATE cdt PRIVATE cgraph PRIVATE gvc)

    add_mvme_dev_exe(dev_graphviz_lib_test1 dev_graphviz_lib_test1.cc)
    target_link_libraries(dev_graphviz_lib_test1 PRIVATE cdt PRIVATE cgraph PRIVATE gvc)

    add_mvme_dev_exe(dev_graphviz_lib_test2 dev_graphviz_lib_test2.cc)
    target_link_libraries(dev_graphviz_lib_test2 PRIVATE cdt PRIVATE cgraph PRIVATE gvc)

    add_mvme_dev_exe(dev_graphviz_lib_test3 dev_graphviz_lib_test3.cc)
    #target_link_libraries(dev_graphviz_lib_test3 PRIVATE cdt PRIVATE cgraph PRIVATE gvc PRIVATE libgvplugin_dot_layout)
    install(TARGETS dev_graphviz_lib_test3 RUNTIME DESTINATION bin)

    add_executable(dev_graphviz_lib_test4 dev_graphviz_lib_test4.cc)
    target_link_libraries(dev_graphviz_lib_test4
        PRIVATE cdt
        PRIVATE cgraph
        PRIVATE gvc
    )

    qt5_wrap_ui(dev_graphviz_lib_test5_uic dev_graphviz_lib_test5_mainwin.ui)
    add_executable(dev_graphviz_lib_test5
        ${dev_graphviz_lib_test5_uic}
        dev_graphviz_lib_test5_main.cc
        dev_graphviz_lib_test5.cc)
    target_link_libraries(dev_graphviz_lib_test5 PRIVATE qgvcore PRIVATE libmvme)

    add_mvme_dev_exe(dev_dotwidget dev_dotwidget.cc)
    install(TARGETS dev_dotwidget RUNTIME DESTINATION bin)
endif()

#TODO: install a CMakeLists.txt for the treewriter client application. how to
# find the installed includes from that directory?
#install(FILES data_export/mvme_root_treewriter_client.cc DESTINATION extras/root_treewriter_client/)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_subdirectory(event_server)

# handling of GSI MBS LMD files
if (NOT WIN32)
  add_subdirectory(gsi-mbs-api)
  add_library(gsi-mbs-wrappers STATIC mbs_wrappers.cc)
  target_link_libraries(gsi-mbs-wrappers PRIVATE mesytec-mvlc PRIVATE gsi-mbs-api)

  add_mvme_exe(lmd2mvme "lmd2mvme.cc")
  target_link_libraries(lmd2mvme PRIVATE gsi-mbs-wrappers)
endif()

#add_subdirectory(listfile_reader)

add_mvme_exe(mvme_histo_viewer mvme_histo_viewer_main.cc)
install(TARGETS mvme_histo_viewer RUNTIME DESTINATION bin LIBRARY DESTINATION lib)

# listfile recovery tool
add_mvme_exe(mvme_listfile_recovery_gui mvme_listfile_recovery_gui.cc)
install(TARGETS mvme_listfile_recovery_gui RUNTIME DESTINATION bin LIBRARY DESTINATION lib)

# mvlc vmeconfig -> crateconfig debug tool
add_mvme_exe(mvme_crateconfig_tool mvlc/mvme_crateconfig_tool.cc)
install(TARGETS mvme_crateconfig_tool RUNTIME DESTINATION bin LIBRARY DESTINATION lib)

# mvme multicrate collector
#add_mvme_exe(mvme_multicrate_collector mvme_multicrate_collector.cc)
#install(TARGETS mvme_multicrate_collector RUNTIME DESTINATION bin LIBRARY DESTINATION lib)

# mvme multicrate replay
#add_mvme_exe(mvme_multicrate_replay mvme_multicrate_replay.cc)
#install(TARGETS mvme_multicrate_replay RUNTIME DESTINATION bin LIBRARY DESTINATION lib)

# mvme multicrate replay2
add_mvme_exe(mvme_multicrate_replay2 mvme_multicrate_replay2.cc)
install(TARGETS mvme_multicrate_replay2 RUNTIME DESTINATION bin LIBRARY DESTINATION lib)

# mvme multicrate readout2
add_mvme_exe(mvme_multicrate_readout2 mvme_multicrate_readout2.cc)
install(TARGETS mvme_multicrate_readout2 RUNTIME DESTINATION bin LIBRARY DESTINATION lib)

add_mvme_exe(qfuturewatcher_fire_forget qfuturewatcher_fire_forget.cc)
add_mvme_exe(statemachine_test statemachine_test.cc)
add_mvme_dev_exe(dev_some_gui_thing dev_some_gui_thing.cc)

add_custom_target(multicrate2_dev_tools)
add_dependencies(multicrate2_dev_tools
    mvme_multicrate_replay2
    mvme_multicrate_readout2
    qfuturewatcher_fire_forget
    statemachine_test
)

# automated gtests
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
    add_mvme_gtest(test_analysis_operators analysis/analysis_operators.test.cc)
    add_mvme_gtest(test_listfile_constants test_listfile_constants.cc)
    #add_mvme_gtest(test_analysis_session analysis/test_analysis_session.cc)
    add_mvme_gtest(test_trigger_io_sim mvlc/test/test_trigger_io_sim.cc)
    add_mvme_gtest(test_vmeconfig_crateconfig mvlc/vmeconfig_crateconfig.test.cc)
    add_mvme_gtest(test_multi_crate multi_crate.test.cc)
    add_mvme_gtest(test_util_version_compare util/version_compare.test.cc)
    add_mvme_gtest(test_mesy_nng_pipeline2 util/mesy_nng_pipeline2.test.cc)
    add_mvme_gtest(test_multi_crate_nng multi_crate_nng.test.cc)
    add_mvme_gtest(test_mdpp_sampling mdpp-sampling/mdpp_sampling.test.cc)
    add_mvme_gtest(test_waveform_plotting mdpp-sampling/waveform_plotting.test.cc)
    add_mvme_gtest(test_waveform_interpolation mdpp-sampling/waveform_interpolation.test.cc)
    add_mvme_gtest(test_mdpp_decode mdpp-sampling/mdpp_decode.test.cc)
    add_mvme_gtest(test_template_system template_system.test.cc)

    add_mvme_dev_exe(dev_qtwi_checkstate_test "analysis/dev_qtwi_checkstate_test.cc")
endif(MVME_BUILD_TESTS)

# vim:tw=0
