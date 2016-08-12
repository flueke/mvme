#-------------------------------------------------
#
# Project created by QtCreator 2014-04-02T11:09:33
#
#-------------------------------------------------

QT       += core gui concurrent
CONFIG   += c++11

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = mvme2
TEMPLATE = app

QMAKE_CXXFLAGS += -Wno-unused -Wno-format
QMAKE_CFLAGS += -Wno-unused -Wno-format


SOURCES += \
    channelspectro.cpp \
    config_widgets.cpp \
    CVMUSBReadoutList.cpp \
    datacruncher.cpp \
    datathread.cpp \
    diagnostics.cpp \
    histogram.cpp \
    libxxusb.cc \
    main.cpp \
    mvme_config.cc \
    mvme_context.cc \
    mvme_context_widget.cc \
    mvmecontrol.cpp \
    mvme.cpp \
    mvme_event_processor.cc \
    mvme_listfile.cc \
    realtimedata.cpp \
    scrollbar.cpp \
    scrollzoomer.cpp \
    simulator.cpp \
    twodimwidget.cpp \
    util.cc \
    virtualmod.cpp \
    vmecommandlist.cc \
    vmecontroller.cpp \
    vmedevice.cpp \
    vmusb_buffer_processor.cc \
    vmusb_readout_worker.cc \
    vmusb_stack.cc \
    hist2ddialog.cc


HEADERS  += \
    channelspectro.h \
    config_widgets.h \
    CVMUSBReadoutList.h \
    databuffer.h \
    datacruncher.h \
    datathread.h \
    diagnostics.h \
    histogram.h \
    mvme_config.h \
    mvme_context.h \
    mvme_context_widget.h \
    mvmecontrol.h \
    mvmedefines.h \
    mvme_event_processor.h \
    mvme.h \
    mvme_listfile.h \
    realtimedata.h \
    scrollbar.h \
    scrollzoomer.h \
    simulator.h \
    twodimwidget.h \
    util.h \
    virtualmod.h \
    vmecommandlist.h \
    vmecontroller.h \
    vmedevice.h \
    vme.h \
    vmusb_buffer_processor.h \
    vmusb_constants.h \
    vmusb_readout_worker.h \
    vmusb_stack.h \
    hist2ddialog.h


FORMS    += \
    mvmecontrol.ui \
    mvme.ui \
    twodimwidget.ui \
    channelspectrowidget.ui \
    moduleconfig_widget.ui \
    hist2ddialog.ui

DEFINES += VME_CONTROLLER_WIENER
#DEFINES += VME_CONTROLLER_CAEN

contains(DEFINES, "VME_CONTROLLER_WIENER") {
    message("Building with WIENER VM_USB support")

    unix:!macx:!symbian: LIBS += -L/usr/lib/ -lusb

    win32 {
        INCLUDEPATH += "C:\libusb-win32-bin-1.2.6.0\include"
        LIBS += -L"C:\libusb-win32-bin-1.2.6.0\lib\gcc" -lusb
    }


    HEADERS += libxxusb.h \
         vmusb.h \

    SOURCES += \
        vmusb.cpp \
}

contains(DEFINES, "VME_CONTROLLER_CAEN") {
    message("Building with CAEN VM support")

    unix:!macx:!symbian: LIBS += -L/usr/lib/ -lCAENVME

    HEADERS += CAENVMEtypes.h \
        CAENVMElib.h \
        caenusb.h \

    SOURCES += caenusb.cpp \
}

unix:!macx:!symbian {
    CONFIG += qwt

    LIBS += -L/usr/local/qwt-6.1.3/lib/ -lqwt
    INCLUDEPATH += /usr/local/qwt-6.1.3/include
    DEPENDPATH  += /usr/local/qwt-6.1.3/include

    LIBS += -L/usr/local/qwt-6.1.2/lib/ -lqwt
    INCLUDEPATH += /usr/local/qwt-6.1.2/include
    DEPENDPATH  += /usr/local/qwt-6.1.2/include
}

unix {
    # suppress the default RPATH
    QMAKE_LFLAGS_RPATH=
    QMAKE_LFLAGS += "-Wl,-rpath,\'\$$ORIGIN\'"
}

win32 {
    include("C:\Qwt-6.1.3\features\qwt.prf")
}
