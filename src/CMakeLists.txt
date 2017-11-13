# TODO: asan

include(GenerateExportHeader)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt5Concurrent REQUIRED)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Network REQUIRED)
find_package(Qt5Widgets REQUIRED)

# quazip
find_path(QUAZIP_INCLUDE_DIR
    NAMES quazip.h
    PATH_SUFFIXES quazip quazip-qt5 quazip5)
find_library(QUAZIP_QT5 NAMES quazip quazip-qt5 quazip5)
include_directories(${QUAZIP_INCLUDE_DIR})

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

# git version information is contained in its own translation unit. Allows for
# quick build + relink on version change.
configure_file("git_sha1.cc.in" "git_sha1.cc" @ONLY)
add_definitions(-DMVME_USE_GIT_VERSION_FILE)

# make build info available to the program
set(BUILD_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
if (NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
    string(TOUPPER ${CMAKE_BUILD_TYPE} build_type_upper)
    set(BUILD_CXX_FLAGS "${BUILD_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${build_type_upper}}")
endif()
configure_file("build_info.cc.in" "build_info.cc" @ONLY)

#
# liba2
#
add_subdirectory(analysis/a2)
include_directories(analysis/a2)

#
# libmvme
#
add_definitions(-DVME_CONTROLLER_WIENER -DWIENER_USE_LIBUSB0)

set(LIBMVME_SOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/git_sha1.cc"
    "${CMAKE_CURRENT_BINARY_DIR}/build_info.cc"
    "${CMAKE_SOURCE_DIR}/3rdparty/qt-collapsible-section/Section.cpp"
    analysis/a2_adapter.cc
    analysis/analysis.cc
    analysis/analysis_ui.cc
    analysis/analysis_ui_p.cc
    analysis/analysis_util.cc
    analysis/data_extraction_widget.cc
    analysis/analysis_info_widget.cc
    config_ui.cpp
    CVMUSBReadoutList.cpp
    daqcontrol_widget.cc
    daqstats_widget.cc
    data_filter.cc
    globals.cc
    gui_util.cc
    histo1d.cc
    histo1d_widget.cc
    histo1d_widget_p.cc
    histo2d.cc
    histo2d_widget.cc
    histo2d_widget_p.cc
    histo_util.cc
    listfile_browser.cc
    mesytec_diagnostics.cc
    mvme_context.cc
    mvme_context_lib.cc
    mvme.cpp
    mvme_listfile.cc
    mvme_startup.cc
    mvme_stream_processor.cc
    mvme_stream_worker.cc
    qt_util.cc
    realtimedata.cpp
    scrollbar.cpp
    scrollzoomer.cpp
    sis3153.cc
    sis3153_readout_worker.cc
    sis3153/sis3153ETH_vme_class.cpp
    sis3153_util.cc
    template_system.cc
    timed_block.cc
    treewidget_utils.cc
    util.cc
    util_zip.cc
    util/strings.cc
    vme_analysis_common.cc
    vme_config.cc
    vme_config_tree.cc
    vme_controller.cpp
    vme_controller_factory.cc
    vme_controller_ui.cc
    vme_debug_widget.cc
    vme_daq.cc
    vme_readout_worker.cc
    vme_script.cc
    vme_script_editor.cc
    vmusb_buffer_processor.cc
    vmusb.cpp
    vmusb_firmware_loader.cc
    vmusb_readout_worker.cc
    vmusb_skipHeader.cpp
    vmusb_stack.cc
    vmusb_util.cc
    )

qt5_wrap_ui(LIBMVME_UIC_OUT
    mesytec_diagnostics.ui
    vme_debug_widget.ui
    )

qt5_add_resources(LIBMVME_RC_OUT resources.qrc)

if (ENABLE_HDF5)
    list(APPEND LIBMVME_ADDITIONAL_LIBS ${HDF5_CXX_LIBRARIES})
    list(APPEND LIBMVME_SOURCES analysis/analysis_session.cc)
endif()

add_library(libmvme SHARED ${LIBMVME_SOURCES} ${LIBMVME_UIC_OUT} ${LIBMVME_RC_OUT})
add_dependencies(libmvme liba2_static)
generate_export_header(libmvme)
set_target_properties(libmvme PROPERTIES OUTPUT_NAME mvme)

qt5_use_modules(libmvme
  Concurrent
  Gui
  Network
  Widgets
  )

if (WIN32)
    list(APPEND LIBMVME_ADDITIONAL_LIBS ws2_32)
endif()

include_directories(${LIBUSB_INCLUDE_PATH})


target_link_libraries(libmvme
    ${QUAZIP_QT5}
    ${QWT_LIBRARY}
    ${LIBUSB_LIBRARY}
    ${LIBMVME_ADDITIONAL_LIBS}
    liba2_static
    )

install(TARGETS libmvme DESTINATION .)

#
# mvme - the main GUI binary
#
set(MVME_SOURCES
    main.cpp
    mvme.rc
    )

if (CMAKE_BUILD_TYPE MATCHES "^Release$")
    add_executable(mvme WIN32 ${MVME_SOURCES})
else()
    #add_definitions(-DMVME_ENABLE_TIMED_BLOCKS)
    add_executable(mvme ${MVME_SOURCES})
endif()

add_dependencies(mvme libmvme)

qt5_use_modules(mvme
  Concurrent
  Gui
  Widgets
  )

target_link_libraries(mvme
    libmvme
    )

install(TARGETS mvme DESTINATION .)

# Helper for additional utility / debug binaries and alternative launchers.
function(add_mvme_executable exe_name exe_sources)
    add_executable(${exe_name} ${exe_sources})
    add_dependencies(${exe_name} libmvme)
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
add_mvme_executable(dev_mvme_event_processing "dev_mvme_event_processing.cc")
add_mvme_executable(dev_datagen "dev_datagen.cc")
add_mvme_executable(dev_data_filter_runner "dev_data_filter_runner.cc")
add_mvme_executable(dev_sis3153_read_raw_buffers_file "dev_sis3153_read_raw_buffers_file.cc")
add_mvme_executable(dev_udp_sender "dev_udp_sender.cc")
add_mvme_executable(dev_udp_receiver "dev_udp_receiver.cc")

# root subdir if ROOT support is enabled
if (ENABLE_ROOT)
    add_subdirectory(root)
endif (ENABLE_ROOT)

# vim:tw=0
