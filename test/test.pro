TEMPLATE = app
QT       += core gui concurrent widgets testlib
CONFIG   += c++11 testcase
TARGET   = ../testrunner

QMAKE_CXXFLAGS += -Wno-unused -Wno-format
QMAKE_CFLAGS += -Wno-unused -Wno-format
DEFINES += VME_CONTROLLER_WIENER

include(../src/src.pri)

HEADERS += \
    tests.h \


SOURCES += \
    testmain.cpp \
    test_mvme_config.cpp \


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

