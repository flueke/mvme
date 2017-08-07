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
    analysis/data_extraction_widget.cc
    analysis/data_filter.cc
    config_ui.cpp
    CVMUSBReadoutList.cpp
    daqcontrol_widget.cc
    daqstats_widget.cc
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
    qt_util.cc
    realtimedata.cpp
    scrollbar.cpp
    scrollzoomer.cpp
    template_system.cc
    treewidget_utils.cc
    util.cc
    vme_analysis_common.cc
    vme_config.cc
    vme_config_tree.cc
    vme_controller.cpp
    vme_debug_widget.cc
    vme_script.cc
    vme_script_editor.cc
    vmusb_buffer_processor.cc
    vmusb.cpp
    vmusb_firmware_loader.cc
    vmusb_readout_worker.cc
    vmusb_skipHeader.cpp
    vmusb_stack.cc
    )

qt5_wrap_ui(LIBMVME_UIC_OUT
    event_config_dialog.ui
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

target_link_libraries(libmvme
    ${QUAZIP_QT5}
    ${QWT_LIBRARY}
    ${LIBUSB_LIBRARY}
    )

install(TARGETS libmvme DESTINATION .)

#
# mvme
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

# vim:tw=0
