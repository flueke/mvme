QT       += core gui concurrent widgets
CONFIG   += c++14
CONFIG   += object_parallel_to_source

TARGET = ../mvme
TEMPLATE = app

# When building with clang qmake puts -Wall after the contents of
# QMAKE_CXXFLAGS and clang thus turns all warnings on again. To circumvent this
# problem disable warnings via warn_off and then prepend -Wall to
# QMAKE_CXXFLAGS.
CONFIG   += warn_off
QMAKE_CXXFLAGS += -O0 -Wall -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wno-format
QMAKE_CFLAGS += -O0 -Wall -Wno-unused -Wno-format
DEFINES += VME_CONTROLLER_WIENER
#DEFINES += VME_CONTROLLER_CAEN

#CONFIG   += sse2
#QMAKE_CXXFLAGS += -msse -msse2

# ASAN
asan {
    QMAKE_CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
    QMAKE_LFLAGS += -fsanitize=address # clang needs this
    LIBS += -lasan # gcc needs this
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

include($$shell_path($$PWD/../git_version.pri))

# vim:ft=conf
