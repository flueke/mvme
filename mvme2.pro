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
    src/config_widgets.cpp \
    src/CVMUSBReadoutList.cpp \
    src/histogram.cpp \
    src/libxxusb.cc \
    src/main.cpp \
    src/mvme_config.cc \
    src/mvme_context.cc \
    src/mvme_context_widget.cc \
    src/mvme.cpp \
    src/mvme_event_processor.cc \
    src/mvme_listfile.cc \
    src/scrollbar.cpp \
    src/scrollzoomer.cpp \
    src/twodimwidget.cpp \
    src/util.cc \
    src/vmecommandlist.cc \
    src/vmecontroller.cpp \
    src/vmedevice.cpp \
    src/vmusb_buffer_processor.cc \
    src/vmusb_readout_worker.cc \
    src/vmusb_stack.cc \
    src/hist2ddialog.cc \
    src/hist2d.cpp \
    src/mvmecontrol.cpp \
    src/context_widget2.cpp


HEADERS  += \
    src/config_widgets.h \
    src/CVMUSBReadoutList.h \
    src/databuffer.h \
    src/histogram.h \
    src/mvme_config.h \
    src/mvme_context.h \
    src/mvme_context_widget.h \
    src/mvmedefines.h \
    src/mvme_event_processor.h \
    src/mvme.h \
    src/mvme_listfile.h \
    src/scrollbar.h \
    src/scrollzoomer.h \
    src/twodimwidget.h \
    src/util.h \
    src/vmecommandlist.h \
    src/vmecontroller.h \
    src/vmedevice.h \
    src/vme.h \
    src/vmusb_buffer_processor.h \
    src/vmusb_constants.h \
    src/vmusb_readout_worker.h \
    src/vmusb_stack.h \
    src/hist2ddialog.h \
    src/hist2d.h \
    src/mvmecontrol.h \
    src/globals.h \
    src/context_widget2.h


FORMS    += \
    src/mvme.ui \
    src/twodimwidget.ui \
    src/hist2ddialog.ui \
    src/hist2dwidget.ui \
    src/mvmecontrol.ui \
    src/event_config_dialog.ui \
    src/context_widget2.ui \
    src/vhs4030p.ui \
    src/module_config_widget.ui


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
