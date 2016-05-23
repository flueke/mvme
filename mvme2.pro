#-------------------------------------------------
#
# Project created by QtCreator 2014-04-02T11:09:33
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = mvme2
TEMPLATE = app


SOURCES += main.cpp\
        mvme.cpp \
        vmusb.cpp \
    mvmecontrol.cpp \
    histogram.cpp \
    datathread.cpp \
    datacruncher.cpp \
    vmedevice.cpp \
    twodimwidget.cpp \
    twodimdisp.cpp \
    scrollzoomer.cpp \
    scrollbar.cpp \
    virtualmod.cpp \
    vmecontroller.cpp \
    caenusb.cpp \
    diagnostics.cpp \
    realtimedata.cpp \
    simulator.cpp

HEADERS  += \
         vmusb.h \
    mvmecontrol.h \
    mvme.h \
    histogram.h \
    datathread.h \
    datacruncher.h \
    mvmedefines.h \
    vmedevice.h \
    twodimwidget.h \
    twodimdisp.h \
    scrollbar.h \
    scrollzoomer.h \
    virtualmod.h \
    vmecontroller.h \
    CAENVMEtypes.h \
    CAENVMElib.h \
    caenusb.h \
    diagnostics.h \
    realtimedata.h \
    simulator.h

FORMS    += \
    mvmecontrol.ui \
    mvme.ui \
    twodimwidget.ui

unix:!macx:!symbian: LIBS += -L/usr/lib/ -lxx_usb
unix:!macx:!symbian: LIBS += -L/usr/lib/ -lCAENVME


INCLUDEPATH += /usr/include
DEPENDPATH += /usr/include

#unix:!macx:!symbian: LIBS += -L/usr/lib/ -lxx_usb

#INCLUDEPATH += $$PWD/../../../../usr/include
#DEPENDPATH += $$PWD/../../../../usr/include

unix:!macx:!symbian: LIBS += -lusb

unix:!macx:!symbian: LIBS += -L/usr/local/qwt-6.1.2/lib/ -lqwt

INCLUDEPATH += /usr/local/qwt-6.1.0-rc3/include /usr/local/qwt-6.1.2/include
DEPENDPATH += /usr/local/qwt-6.1.0-rc3/include /usr/local/qwt-6.1.2/include
