#include <memory>
#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QGroupBox>
#include <QTableWidget>
#include <QWidget>
#include <QCheckBox>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QSplitter>
#include <QDebug>

#include "mvlc/mvlc_trigger_io_editor.h"
#include "mvlc/mvlc_trigger_io.h"
#include "mvlc/mvlc_trigger_io_2.h"
#include "mvme_session.h"
#include "qt_util.h"

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::trigger_io_config;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Among other things this makes the program use its own QSettings
    // file/registry key so we don't mess with other programs settings.
    mvme_init("dev_mvlc_trigger_gui");

    auto scriptConfig = std::make_unique<VMEScriptConfig>();
    auto trigger_io_editor = new mesytec::MVLCTriggerIOEditor(scriptConfig.get());

    auto mainLayout = make_hbox<0, 0>();
    mainLayout->addWidget(trigger_io_editor);

    auto mainWindow = new QWidget;
    mainWindow->setLayout(mainLayout);
    mainWindow->setAttribute(Qt::WA_DeleteOnClose);
    mainWindow->resize(1400, 980);
    mainWindow->show();

    // Quit using Ctrl+Q
    auto actionQuit = new QAction("&Quit", mainWindow);
    actionQuit->setShortcut(QSL("Ctrl+Q"));
    actionQuit->setShortcutContext(Qt::ApplicationShortcut);
    QObject::connect(actionQuit, &QAction::triggered,
                     mainWindow, [mainWindow] () {
                         mainWindow->close();
                         QApplication::quit();
                     });
    mainWindow->addAction(actionQuit);

    // Save and restore window geometries
    WidgetGeometrySaver widgetGeometrySaver;
    widgetGeometrySaver.addAndRestore(mainWindow, "MainWindow");

    int ret =  app.exec();
    return ret;
}
