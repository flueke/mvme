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


SOURCES += main.cpp\
        mvme.cpp \
    mvmecontrol.cpp \
    histogram.cpp \
    datathread.cpp \
    datacruncher.cpp \
    vmedevice.cpp \
    twodimwidget.cpp \
    scrollzoomer.cpp \
    scrollbar.cpp \
    virtualmod.cpp \
    vmecontroller.cpp \
    diagnostics.cpp \
    realtimedata.cpp \
    simulator.cpp \
    CVMUSBReadoutList.cpp \
    channelspectro.cpp \
    libxxusb.cc \
    util.cc \
    vme_module.cc \
    vmusb_stack.cc \
    mvme_context.cc \
    vmusb_readout_worker.cc \
    vmusb_buffer_processor.cc \
    mvme_context_widget.cc \
    mvme_config.cc \


HEADERS  += \
    mvmecontrol.h \
    mvme.h \
    histogram.h \
    datathread.h \
    datacruncher.h \
    mvmedefines.h \
    vmedevice.h \
    twodimwidget.h \
    scrollbar.h \
    scrollzoomer.h \
    virtualmod.h \
    vmecontroller.h \
    diagnostics.h \
    realtimedata.h \
    simulator.h \
    CVMUSBReadoutList.h \
    channelspectro.h \
    util.h \
    vme.h \
    vme_module.h \
    vmusb_stack.h \
    mvme_context.h \
    vmusb_readout_worker.h \
    databuffer.h \
    vmusb_constants.h \
    vmusb_buffer_processor.h \
    mvme_context_widget.h \
    mvme_config.h \
    vmecommandlist.h


FORMS    += \
    mvmecontrol.ui \
    mvme.ui \
    twodimwidget.ui \
    channelspectrowidget.ui \
    moduleconfig_widget.ui

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
