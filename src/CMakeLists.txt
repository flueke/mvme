# TODO:
# asan
# profiling

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
find_package(Qt5Widgets REQUIRED)

# quazip
find_library(QUAZIP_QT5 NAMES quazip quazip-qt5)

# qwt
find_path(QWT_INCLUDE_DIR
    NAMES qwt_plot.h
    PATH_SUFFIXES qwt qwt-qt5 qwt-6.1.3)

find_library(QWT_LIBRARY NAMES qwt qwt-qt5)
include_directories(${QWT_INCLUDE_DIR})

# libusb
find_library(LIBUSB_LIBRARY usb)
find_path(LIBUSB_INCLUDE_PATH usb.h)

# Pull information from git:
# GIT_SHA1 contains the complete git hash
# GIT_VERSION contains the name of the latest git tag and the start of the git
# hash if there have been changes since the tag has been created.
include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)
git_describe(GIT_VERSION "--always")
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
    daqcontrol_widget.ui
    event_config_dialog.ui
    mesytec_diagnostics.ui
    mvme.ui
    vme_debug_widget.ui
    )

qt5_add_resources(LIBMVME_RC_OUT resources.qrc)

add_library(libmvme SHARED ${LIBMVME_SOURCES} ${LIBMVME_UIC_OUT} ${LIBMVME_RC_OUT})
set_target_properties(libmvme PROPERTIES OUTPUT_NAME mvme)

qt5_use_modules(libmvme
  Concurrent
  Gui
  Widgets
  )

#
# mvme
#
set(MVME_SOURCES
    main.cpp
    mvme.rc
    )

add_executable(mvme ${MVME_SOURCES})
add_dependencies(mvme libmvme)

qt5_use_modules(mvme
  Concurrent
  Gui
  Widgets
  )

target_link_libraries(mvme
    libmvme
    ${QUAZIP_QT5}
    ${QWT_LIBRARY}
    ${LIBUSB_LIBRARY}
    )

# vim:tw=0
