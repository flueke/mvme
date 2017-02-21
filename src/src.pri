INCLUDEPATH += $$PWD .
DEPENDPATH += $$PWD .

HEADERS += \
    $$PWD/CVMUSBReadoutList.h \
    $$PWD/config_ui.h \
    $$PWD/daqconfig_tree.h \
    $$PWD/daqcontrol_widget.h \
    $$PWD/daqstats_widget.h \
    $$PWD/data_filter.h \
    $$PWD/databuffer.h \
    $$PWD/globals.h \
    $$PWD/gui_util.h \
    $$PWD/hist1d.h \
    $$PWD/hist2d.h \
    $$PWD/hist2ddialog.h \
    $$PWD/histo1d.h \
    $$PWD/histo1d_widget.h \
    $$PWD/histo2d.h \
    $$PWD/histo2d_widget.h \
    $$PWD/histo_util.h \
    $$PWD/histogram_tree.h \
    $$PWD/mesytec_diagnostics.h \
    $$PWD/mvme.h \
    $$PWD/mvme_config.h \
    $$PWD/mvme_context.h \
    $$PWD/mvme_event_processor.h \
    $$PWD/mvme_listfile.h \
    $$PWD/mvmedefines.h \
    $$PWD/realtimedata.h \
    $$PWD/scrollbar.h \
    $$PWD/scrollzoomer.h \
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


SOURCES += \
    $$PWD/config_ui.cpp \
    $$PWD/CVMUSBReadoutList.cpp \
    $$PWD/daqconfig_tree.cc \
    $$PWD/daqcontrol_widget.cc \
    $$PWD/daqstats_widget.cc \
    $$PWD/data_filter.cc \
    $$PWD/gui_util.cc \
    $$PWD/hist1d.cc \
    $$PWD/hist2d.cpp \
    $$PWD/hist2ddialog.cc \
    $$PWD/histo1d.cc \
    $$PWD/histo1d_widget.cc \
    $$PWD/histo2d.cc \
    $$PWD/histo2d_widget.cc \
    $$PWD/histogram_tree.cc \
    $$PWD/histo_util.cc \
    $$PWD/mesytec_diagnostics.cc \
    $$PWD/mvme_config.cc \
    $$PWD/mvme_context.cc \
    $$PWD/mvme.cpp \
    $$PWD/mvme_event_processor.cc \
    $$PWD/mvme_listfile.cc \
    $$PWD/realtimedata.cpp \
    $$PWD/scrollbar.cpp \
    $$PWD/scrollzoomer.cpp \
    $$PWD/util.cc \
    $$PWD/vme_controller.cpp \
    $$PWD/vme_debug_widget.cc \
    $$PWD/vme_script.cc \
    $$PWD/vme_script_editor.cc \
    $$PWD/vmusb_buffer_processor.cc \
    $$PWD/vmusb_readout_worker.cc \
    $$PWD/vmusb_stack.cc \


FORMS += \
    $$PWD/daqcontrol_widget.ui \
    $$PWD/datafilter_dialog.ui \
    $$PWD/dualword_datafilter_dialog.ui \
    $$PWD/event_config_dialog.ui \
    $$PWD/hist1dwidget.ui \
    $$PWD/hist2ddialog_axis_widget.ui \
    $$PWD/hist2ddialog.ui \
    $$PWD/hist2dwidget.ui \
    $$PWD/histo1d_widget.ui \
    $$PWD/histo2d_widget.ui \
    $$PWD/mesytec_diagnostics.ui \
    $$PWD/mvme.ui \
    $$PWD/vme_debug_widget.ui \


contains(DEFINES, "VME_CONTROLLER_WIENER") {
    message("Building with WIENER VM_USB support")

    unix:!macx:!symbian: LIBS += -L/usr/lib/ -lusb

    win32 {
        INCLUDEPATH += "C:\libusb-win32-bin-1.2.6.0\include"
        LIBS += -L"C:\libusb-win32-bin-1.2.6.0\lib\gcc" -lusb
    }


    HEADERS += $$PWD/vmusb.h

    SOURCES += $$PWD/vmusb.cpp
}

contains(DEFINES, "VME_CONTROLLER_CAEN") {
    message("Building with CAEN VM support")

    unix:!macx:!symbian: LIBS += -L/usr/lib/ -lCAENVME

    HEADERS += \
        CAENVMEtypes.h \
        CAENVMElib.h \
        caenusb.h \

    SOURCES += \
        caenusb.cpp \
}

include($$PWD/3rdparty/3rdparty.pri)
include($$PWD/analysis/analysis.pri)

# vim:ft=conf
