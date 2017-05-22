QT       += core gui concurrent widgets
CONFIG   += c++14
CONFIG   += object_parallel_to_source

TARGET = ../mvme
TEMPLATE = app

#CONFIG += sse2
#QMAKE_CXXFLAGS_RELEASE -= -O2
#QMAKE_CXXFLAGS_RELEASE += -O3 -msse -msse2

# When building with clang qmake puts -Wall after the contents of
# QMAKE_CXXFLAGS and clang thus turns all warnings on again. To circumvent this
# problem disable warnings via warn_off and then prepend -Wall to
# QMAKE_CXXFLAGS.
CONFIG   += warn_off
warning_flags = -Wall -Wno-unused-parameter -Wno-unused-function -Wno-format -Wno-unused-but-set-parameter
# Other flags: -Wall -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wno-format
QMAKE_CXXFLAGS += $$warning_flags
QMAKE_CFLAGS += $$warning_flags

CONFIG(release, debug|release) {
    QMAKE_CXXFLAGS += -march=nocona -mtune=core2
    QMAKE_CFLAGS += -march=nocona -mtune=core2
}

CONFIG(debug, debug|release) {
    QMAKE_CXXFLAGS += -O0 $$QMAKE_CXXFLAGS
    QMAKE_CFLAGS += -O0 $$QMAKE_CFLAGS
}

DEFINES += VME_CONTROLLER_WIENER WIENER_USE_LIBUSB0

# ASAN
asan {
    QMAKE_CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
    linux-clang {
        QMAKE_LFLAGS += -fsanitize=address # clang needs this
    }

    linux-g++ {
        LIBS += -lasan # gcc needs this
    }
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
    # qwt
    CONFIG += qwt
    LIBS += -L/usr/local/qwt-6.1.3/lib/ -lqwt
    INCLUDEPATH += /usr/local/qwt-6.1.3/include
    DEPENDPATH  += /usr/local/qwt-6.1.3/include

    # quazip
    INCLUDEPATH += /usr/local/include
    LIBS += -L/home/florian/local/lib/ -lquazip
}

win32 {
    include("C:\Qwt-6.1.3\features\qwt.prf")
    LIBS += -lquazip
}

unix {
    # suppress the default RPATH
    QMAKE_LFLAGS_RPATH=
    QMAKE_LFLAGS += "-Wl,-rpath,\'\$$ORIGIN\'"
}

#include($$shell_path($$PWD/../git_version.pri))
# When building under msys2 using shell_path() does not work.
include($$PWD/../git_version.pri)

# vim:ft=conf
