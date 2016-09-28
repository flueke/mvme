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

include(src/src.pri)

FORMS    += \
    src/context_widget2.ui \
    src/event_config_dialog.ui \
    src/hist2ddialog.ui \
    src/hist2dwidget.ui \
    src/module_config_widget.ui \
    src/mvmecontrol.ui \
    src/mvme.ui \
    src/twodimwidget.ui \
    src/vhs4030p.ui \


DEFINES += VME_CONTROLLER_WIENER
#DEFINES += VME_CONTROLLER_CAEN

contains(DEFINES, "VME_CONTROLLER_WIENER") {
    message("Building with WIENER VM_USB support")

    unix:!macx:!symbian: LIBS += -L/usr/lib/ -lusb

    win32 {
        INCLUDEPATH += "C:\libusb-win32-bin-1.2.6.0\include"
        LIBS += -L"C:\libusb-win32-bin-1.2.6.0\lib\gcc" -lusb
    }


    HEADERS += \
        src/libxxusb.h \
        src/vmusb.h \

    SOURCES += \
        src/vmusb.cpp \
}

contains(DEFINES, "VME_CONTROLLER_CAEN") {
    message("Building with CAEN VM support")

    unix:!macx:!symbian: LIBS += -L/usr/lib/ -lCAENVME

    HEADERS += \
        src/CAENVMEtypes.h \
        src/CAENVMElib.h \
        src/caenusb.h \

    SOURCES += \
        src/caenusb.cpp \
}

unix:!macx:!symbian {
    CONFIG += qwt
    LIBS += -L/usr/local/qwt-6.1.3/lib/ -lqwt
    INCLUDEPATH += /usr/local/qwt-6.1.3/include
    DEPENDPATH  += /usr/local/qwt-6.1.3/include
}

unix {
    # suppress the default RPATH
    QMAKE_LFLAGS_RPATH=
    QMAKE_LFLAGS += "-Wl,-rpath,\'\$$ORIGIN\'"
}

win32 {
    include("C:\Qwt-6.1.3\features\qwt.prf")
}

copytemplates.commands = $(COPY_DIR) $$shell_path($$PWD/templates) $$shell_path($$OUT_PWD)
copyfiles.commands = $(COPY) $$shell_path($$PWD/default.mvmecfg) $$shell_path($$OUT_PWD)
first.depends = $(first) copytemplates copyfiles
export(first.depends)
export(copytemplates.commands)
export(copyfiles.commands)
QMAKE_EXTRA_TARGETS += first copytemplates copyfiles
