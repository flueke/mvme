QT       += core gui concurrent widgets
CONFIG   += c++11

TARGET = ../mvme2
TEMPLATE = app

QMAKE_CXXFLAGS += -Wno-unused -Wno-format
QMAKE_CFLAGS += -Wno-unused -Wno-format
DEFINES += VME_CONTROLLER_WIENER
#DEFINES += VME_CONTROLLER_CAEN

include(src.pri)

SOURCES += main.cpp

RESOURCES += resources.qrc

unix:!macx:!symbian {
    CONFIG += qwt
    LIBS += -L/usr/local/qwt-6.1.3/lib/ -lqwt
    INCLUDEPATH += /usr/local/qwt-6.1.3/include
    DEPENDPATH  += /usr/local/qwt-6.1.3/include
}

win32 {
    include("C:\Qwt-6.1.3\features\qwt.prf")
}

unix {
    # suppress the default RPATH
    QMAKE_LFLAGS_RPATH=
    QMAKE_LFLAGS += "-Wl,-rpath,\'\$$ORIGIN\'"
}

FORMS += \
    vme_script_editor.ui \

