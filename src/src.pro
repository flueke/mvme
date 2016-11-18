QT       += core gui concurrent widgets
CONFIG   += c++11

TARGET = ../mvme
TEMPLATE = app

QMAKE_CXXFLAGS += -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wno-format
QMAKE_CFLAGS += -Wno-unused -Wno-format
DEFINES += VME_CONTROLLER_WIENER
#DEFINES += VME_CONTROLLER_CAEN

# ASAN
asan {
    QMAKE_CXXFLAGS += -fsanitize=address
    LIBS += -lasan
}

# profiling
profiling {
    QMAKE_CXXFLAGS += -pg
    QMAKE_CFLAGS += -pg
    QMAKE_LFLAGS += -pg
}

include(src.pri)

SOURCES += main.cpp
RESOURCES += resources.qrc
RC_FILE = mvme.rc

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

include($$PWD/../git_version.pri)

# vim:ft=conf
