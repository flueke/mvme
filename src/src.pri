INCLUDEPATH += $$PWD .
DEPENDPATH += $$PWD .


SOURCES += \
    $$PWD/CVMUSBReadoutList.cpp \
    $$PWD/hist1d.cc \
    $$PWD/hist2d.cpp \
    $$PWD/hist2ddialog.cc \
    $$PWD/histogram.cpp \
    $$PWD/mvme_config.cc \
    $$PWD/mvme_context.cc \
    $$PWD/mvme.cpp \
    $$PWD/mvme_event_processor.cc \
    $$PWD/mvme_listfile.cc \
    $$PWD/scrollbar.cpp \
    $$PWD/scrollzoomer.cpp \
    $$PWD/twodimwidget.cpp \
    $$PWD/util.cc \
    $$PWD/vme_controller.cpp \
    $$PWD/vme_script.cc \
    $$PWD/vmusb_buffer_processor.cc \
    $$PWD/vmusb_readout_worker.cc \
    $$PWD/vmusb_stack.cc \
    $$PWD/daqconfig_tree.cc \
    $$PWD/vme_script_editor.cc \
    $$PWD/vme_debug_widget.cc \
    $$PWD/histogram_tree.cc \
    $$PWD/daqcontrol_widget.cc \
    $$PWD/daqstats_widget.cc \
    $$PWD/data_filter.cc \
    $$PWD/config_ui.cpp \
    $$PWD/mesytec_diagnostics.cc \
    $$PWD/realtimedata.cpp \
    $$PWD/histo_util.cc \


HEADERS += \
    $$PWD/CVMUSBReadoutList.h \
    $$PWD/databuffer.h \
    $$PWD/globals.h \
    $$PWD/hist1d.h \
    $$PWD/hist2ddialog.h \
    $$PWD/hist2d.h \
    $$PWD/histogram.h \
    $$PWD/mvme_config.h \
    $$PWD/mvme_context.h \
    $$PWD/mvmedefines.h \
    $$PWD/mvme_event_processor.h \
    $$PWD/mvme.h \
    $$PWD/mvme_listfile.h \
    $$PWD/scrollbar.h \
    $$PWD/scrollzoomer.h \
    $$PWD/twodimwidget.h \
    $$PWD/util.h \
    $$PWD/vme_controller.h \
    $$PWD/vme.h \
    $$PWD/vme_script.h \
    $$PWD/vmusb_buffer_processor.h \
    $$PWD/vmusb_constants.h \
    $$PWD/vmusb_readout_worker.h \
    $$PWD/vmusb_stack.h \
    $$PWD/daqconfig_tree.h \
    $$PWD/vme_script_editor.h \
    $$PWD/vme_debug_widget.h \
    $$PWD/histogram_tree.h \
    $$PWD/daqcontrol_widget.h \
    $$PWD/daqstats_widget.h \
    $$PWD/data_filter.h \
    $$PWD/config_ui.h \
    $$PWD/mesytec_diagnostics.h \
    $$PWD/realtimedata.h \
    $$PWD/histo_util.h \


FORMS += \
    $$PWD/event_config_dialog.ui \
    $$PWD/hist2ddialog.ui \
    $$PWD/hist1dwidget.ui \
    $$PWD/hist2dwidget.ui \
    $$PWD/mvme.ui \
    $$PWD/twodimwidget.ui \
    $$PWD/vme_debug_widget.ui \
    $$PWD/datafilter_dialog.ui \
    $$PWD/mesytec_diagnostics.ui \
    $$PWD/hist2ddialog_axis_widget.ui \


contains(DEFINES, "VME_CONTROLLER_WIENER") {
    message("Building with WIENER VM_USB support")

    unix:!macx:!symbian: LIBS += -L/usr/lib/ -lusb

    win32 {
        INCLUDEPATH += "C:\libusb-win32-bin-1.2.6.0\include"
        LIBS += -L"C:\libusb-win32-bin-1.2.6.0\lib\gcc" -lusb
    }


    HEADERS += \
        $$PWD/vmusb.h \

    SOURCES += \
        $$PWD/vmusb.cpp \
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

# vim:ft=conf
