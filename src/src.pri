SOURCES += \
    config_widgets.cpp \
    context_widget2.cpp \
    CVMUSBReadoutList.cpp \
    hist2d.cpp \
    hist2ddialog.cc \
    histogram.cpp \
    libxxusb.cc \
    mvme_config.cc \
    mvme_context.cc \
    mvme_context_widget.cc \
    mvmecontrol.cpp \
    mvme.cpp \
    mvme_event_processor.cc \
    mvme_listfile.cc \
    scrollbar.cpp \
    scrollzoomer.cpp \
    twodimwidget.cpp \
    util.cc \
    vmecommandlist.cc \
    vmecontroller.cpp \
    vmedevice.cpp \
    vme_script.cc \
    vmusb_buffer_processor.cc \
    vmusb_readout_worker.cc \
    vmusb_stack.cc \


HEADERS += \
    config_widgets.h \
    context_widget2.h \
    CVMUSBReadoutList.h \
    databuffer.h \
    globals.h \
    hist2ddialog.h \
    hist2d.h \
    histogram.h \
    mvme_config.h \
    mvme_context.h \
    mvme_context_widget.h \
    mvmecontrol.h \
    mvmedefines.h \
    mvme_event_processor.h \
    mvme.h \
    mvme_listfile.h \
    scrollbar.h \
    scrollzoomer.h \
    twodimwidget.h \
    util.h \
    vmecommandlist.h \
    vmecontroller.h \
    vmedevice.h \
    vme.h \
    vme_script.h \
    vmusb_buffer_processor.h \
    vmusb_constants.h \
    vmusb_readout_worker.h \
    vmusb_stack.h \

FORMS += \
    context_widget2.ui \
    event_config_dialog.ui \
    hist2ddialog.ui \
    hist2dwidget.ui \
    module_config_widget.ui \
    mvmecontrol.ui \
    mvme.ui \
    twodimwidget.ui \
    vhs4030p.ui \

contains(DEFINES, "VME_CONTROLLER_WIENER") {
    message("Building with WIENER VM_USB support")

    unix:!macx:!symbian: LIBS += -L/usr/lib/ -lusb

    win32 {
        INCLUDEPATH += "C:\libusb-win32-bin-1.2.6.0\include"
        LIBS += -L"C:\libusb-win32-bin-1.2.6.0\lib\gcc" -lusb
    }


    HEADERS += \
        libxxusb.h \
        vmusb.h \

    SOURCES += \
        vmusb.cpp \
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
