INCLUDEPATH += $$PWD .
DEPENDPATH += $$PWD .

HEADERS += \
    $$PWD/CVMUSBReadoutList.h \
    $$PWD/config_ui.h \
    $$PWD/daqcontrol_widget.h \
    $$PWD/daqstats_widget.h \
    $$PWD/databuffer.h \
    $$PWD/globals.h \
    $$PWD/gui_util.h \
    $$PWD/histo1d.h \
    $$PWD/histo1d_widget.h \
    $$PWD/histo1d_widget_p.h \
    $$PWD/histo2d.h \
    $$PWD/histo2d_widget.h \
    $$PWD/histo2d_widget_p.h \
    $$PWD/histo_util.h \
    $$PWD/mesytec_diagnostics.h \
    $$PWD/mvme.h \
    $$PWD/mvme_context.h \
    $$PWD/mvme_event_processor.h \
    $$PWD/mvme_listfile.h \
    $$PWD/mvmedefines.h \
    $$PWD/qt_util.h \
    $$PWD/realtimedata.h \
    $$PWD/scrollbar.h \
    $$PWD/scrollzoomer.h \
    $$PWD/template_system.h \
    $$PWD/treewidget_utils.h \
    $$PWD/util.h \
    $$PWD/vme.h \
    $$PWD/vme_controller.h \
    $$PWD/vme_debug_widget.h \
    $$PWD/vme_script.h \
    $$PWD/vme_script_editor.h \
    $$PWD/vmusb_buffer_processor.h \
    $$PWD/vmusb_constants.h \
    $$PWD/vmusb_readout_worker.h \
    $$PWD/vmusb_stack.h \
    $$PWD/vme_analysis_common.h \
    $$PWD/vme_config.h \
    $$PWD/vme_config_tree.h \


SOURCES += \
    $$PWD/config_ui.cpp \
    $$PWD/CVMUSBReadoutList.cpp \
    $$PWD/daqcontrol_widget.cc \
    $$PWD/daqstats_widget.cc \
    $$PWD/gui_util.cc \
    $$PWD/histo1d.cc \
    $$PWD/histo1d_widget.cc \
    $$PWD/histo1d_widget_p.cc \
    $$PWD/histo2d.cc \
    $$PWD/histo2d_widget.cc \
    $$PWD/histo2d_widget_p.cc \
    $$PWD/histo_util.cc \
    $$PWD/mesytec_diagnostics.cc \
    $$PWD/mvme_context.cc \
    $$PWD/mvme.cpp \
    $$PWD/mvme_event_processor.cc \
    $$PWD/mvme_listfile.cc \
    $$PWD/qt_util.cc \
    $$PWD/realtimedata.cpp \
    $$PWD/scrollbar.cpp \
    $$PWD/scrollzoomer.cpp \
    $$PWD/template_system.cc \
    $$PWD/treewidget_utils.cc \
    $$PWD/util.cc \
    $$PWD/vme_controller.cpp \
    $$PWD/vme_debug_widget.cc \
    $$PWD/vme_script.cc \
    $$PWD/vme_script_editor.cc \
    $$PWD/vmusb_buffer_processor.cc \
    $$PWD/vmusb_readout_worker.cc \
    $$PWD/vmusb_stack.cc \
    $$PWD/vme_analysis_common.cc \
    $$PWD/vme_config.cc \
    $$PWD/vme_config_tree.cc \


FORMS += \
    $$PWD/daqcontrol_widget.ui \
    $$PWD/event_config_dialog.ui \
    $$PWD/histo1d_widget.ui \
    $$PWD/histo2d_widget.ui \
    $$PWD/mesytec_diagnostics.ui \
    $$PWD/mvme.ui \
    $$PWD/vme_debug_widget.ui \

contains(DEFINES, "VME_CONTROLLER_WIENER") {
    contains(DEFINES, "WIENER_USE_LIBUSB0") {
        message("Building with WIENER VM_USB support (libusb-0.1)")

        unix:!macx:!symbian {
            LIBS += -L/usr/lib/ -lusb
        }

        win32 {
            # Old libusb-win32 paths.
            INCLUDEPATH += "C:\libusb-win32-bin-1.2.6.0\include"

	    # For 32-bit builds
            LIBS += -L"C:\libusb-win32-bin-1.2.6.0\lib\gcc" -lusb

	    # For 64-bit builds
	    #LIBS += -L"C:\libusb-win32-bin-1.2.6.0\bin\amd64" -lusb0
        }
    }

    contains(DEFINES, "WIENER_USE_LIBUSB1") {
        message("Building with WIENER VM_USB support (libusb-1.0)")

        unix:!macx:!symbian {
            #QMAKE_CXXFLAGS += `pkg-config --cflags libusb-1.0`
            #LIBS += `pkg-config --libs libusb-1.0`
            CONFIG += link_pkgconfig
            PKGCONFIG += libusb-1.0
        }

        win32 {
            # When building with libusb-1.0 under msys pkgconfig is available.
            #CONFIG += link_pkgconfig
            #PKGCONFIG += libusb-1.0

            # Manually adding paths for libusb-1.0 when not building under msys as
            # I did not bother to install a win32-native pkgconfig.
            INCLUDEPATH += C:\libusb-1.0.21\include\libusb-1.0
            LIBS += -L"C:\libusb-1.0.21\MinGW32\dll" -lusb-1.0
        }
    }

    HEADERS += \
        $$PWD/vmusb.h \
        $$PWD/vmusb_firmware_loader.h \
        $$PWD/vmusb_skipHeader.h \

    SOURCES += \
        $$PWD/vmusb.cpp \
        $$PWD/vmusb_firmware_loader.cc \
        $$PWD/vmusb_skipHeader.cpp \
}

include($$PWD/3rdparty/3rdparty.pri)
include($$PWD/analysis/analysis.pri)

# vim:ft=conf
