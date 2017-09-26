# TODO:
# asan
# profiling

include(GenerateExportHeader)

set(WARNING_FLAGS "-Wall -Wno-unused-variable -Wno-unused-function")

set(CMAKE_CXX_FLAGS "-std=c++14 -pipe ${WARNING_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "-g -Og")
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3 -march=nocona -mtune=core2")
# Explicitly set the required c++ standard (CMake >= 3.1)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt5Concurrent REQUIRED)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Network REQUIRED)
find_package(Qt5Widgets REQUIRED)

# 3rdparty
include_directories("${CMAKE_SOURCE_DIR}/3rdparty/pcg-cpp-0.98/include/")

# quazip
find_path(QUAZIP_INCLUDE_DIR
    NAMES quazip.h
    PATH_SUFFIXES quazip quazip-qt5 quazip5)
find_library(QUAZIP_QT5 NAMES quazip quazip-qt5 quazip5)
include_directories(${QUAZIP_INCLUDE_DIR})

# qwt
find_path(QWT_INCLUDE_DIR
    NAMES qwt_plot.h
    PATH_SUFFIXES qwt qwt-qt5 qwt-6.1.3)

find_library(QWT_LIBRARY NAMES qwt qwt-qt5)
include_directories(${QWT_INCLUDE_DIR})
#include(FindPackageHandleStandardArgs)
#find_package_handle_standard_args(Qwt DEFAULT_MSG QWT_LIBRARY QWT_INCLUDE_DIR)

message("Using QWT_INCLUDE_DIR=${QWT_INCLUDE_DIR}")
message("Using QWT_LIBRARY=${QWT_LIBRARY}")

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
endif()

include_directories(${LIBUSB_INCLUDE_PATH})

# git version information is contained in its own translation unit. Allows for
# quick build + relink on version change.
configure_file("git_sha1.cc.in" "git_sha1.cc" @ONLY)
add_definitions(-DMVME_USE_GIT_VERSION_FILE)

#
# libmvme
#
add_definitions(-DVME_CONTROLLER_WIENER -DWIENER_USE_LIBUSB0)

set(LIBMVME_SOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/git_sha1.cc"
    analysis/analysis.cc
    analysis/analysis_ui.cc
    analysis/analysis_ui_p.cc
    analysis/analysis_util.cc
    analysis/data_extraction_widget.cc
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
    mesytec_diagnostics.cc
    mvme_context.cc
    mvme.cpp
    mvme_event_processor.cc
    mvme_listfile.cc
    mvme_startup.cc
    qt_util.cc
    realtimedata.cpp
    scrollbar.cpp
    scrollzoomer.cpp
    sis3153.cc
    sis3153_readout_worker.cc
    sis3153/sis3153ETH_vme_class.cpp
    sis3153_util.cc
    template_system.cc
    treewidget_utils.cc
    util.cc
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
    mvme.ui
    vme_debug_widget.ui
    )

qt5_add_resources(LIBMVME_RC_OUT resources.qrc)

add_library(libmvme SHARED ${LIBMVME_SOURCES} ${LIBMVME_UIC_OUT} ${LIBMVME_RC_OUT})
generate_export_header(libmvme)
set_target_properties(libmvme PROPERTIES OUTPUT_NAME mvme)

qt5_use_modules(libmvme
  Concurrent
  Gui
  Network
  Widgets
  )

if (WIN32)
    set(LIBMVME_ADDITIONAL_LIBS ws2_32)
endif()

target_link_libraries(libmvme
    ${QUAZIP_QT5}
    ${QWT_LIBRARY}
    ${LIBUSB_LIBRARY}
    ${LIBMVME_ADDITIONAL_LIBS}
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
add_executable(sis3153_test sis3153_test.cc)
add_dependencies(sis3153_test libmvme)
target_link_libraries(sis3153_test libmvme)

#
# mvme_register_reader
#
add_executable(mvme_register_reader mvme_register_reader.cc)
add_dependencies(mvme_register_reader libmvme)
target_link_libraries(mvme_register_reader libmvme)

#
# vme_script_checker
#
add_executable(vme_script_checker vme_script_checker.cc)
add_dependencies(vme_script_checker libmvme)
target_link_libraries(vme_script_checker libmvme)

#
# test_launcher_vme_module_template
#
add_executable(test_launcher_module_template test_launcher_module_template.cc)
add_dependencies(test_launcher_module_template libmvme)
target_link_libraries(test_launcher_module_template libmvme)

#
# vmusb_read_buffers_file
#
add_mvme_executable(vmusb_read_buffers_file "vmusb_read_buffers_file.cc")

add_mvme_executable(mvme-listfile-dumper-multievent "listfile-multievent-dumper.cpp")
add_mvme_executable(dev_mvme_event_processing "dev_mvme_event_processing.cc")

# vim:tw=0
