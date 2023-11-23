#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QSplashScreen>
#include <QStandardPaths>
#include <QTimer>
#include <spdlog/spdlog.h>

#include "multi_crate_mainwindow.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvme_session.h"
#include "mvme_workspace.h"
#include "util/mesy_nng.h"
#include "util/qt_fs.h"
#include "vme_config_util.h"
#include "vme_script_editor.h"
#include "widget_registry.h"

using namespace mesytec;
using namespace mesytec::mvme;

static const QString VMEConfigFileFilter = QSL("Config Files (*.vme *.mvmecfg);; All Files (*)");


// TODO: add autosave handling at some point (see MVMEContext::openWorkspace())

void open_vme_config(const QString &path, MultiCrateMainWindow *mainWindow)
{
    auto doc = read_json_file(path);
    std::shared_ptr<ConfigObject> config(vme_config::deserialize_object(doc.object()));
    mainWindow->setConfig(config, path);

    if (config)
    {
        auto settings = make_workspace_settings();
        settings.setValue("LastVMEConfig", path);
    }
}

bool open_workspace(const QString &path, MultiCrateMainWindow *mainWindow)
{
    if (!QDir::setCurrent(path))
        return false;

    auto settings = make_workspace_settings();
    if (auto lastVMEConfig = settings.value(QSL("LastVMEConfig")).toString(); !lastVMEConfig.isEmpty())
    {
        open_vme_config(lastVMEConfig, mainWindow);
    }

    QSettings globalSettings;
    globalSettings.setValue("LastWorkspaceDirectory", path);

    return true;
}

bool create_new_or_open_existing_workspace(MultiCrateMainWindow *mainWindow)
{
    auto startDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    auto dirName  = QFileDialog::getExistingDirectory(mainWindow,
        QSL("Create a new or select an existing workspace directory"), startDir);

    if (dirName.isEmpty())
    {
        // Dialog was canceled
        return false;
    }

    return open_workspace(dirName, mainWindow);
}

bool save_vme_config_as(MultiCrateMainWindow *mainWindow)
{
    auto filename = mainWindow->getConfigFilename();
    if (filename.isEmpty())
        filename = "new.vme";
    QFileDialog fd(mainWindow, "Save Config As", filename, VMEConfigFileFilter);
    fd.setDefaultSuffix(".vme");
    fd.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);

    if (fd.exec() != QDialog::Accepted || fd.selectedFiles().isEmpty())
        return false;

    filename = fd.selectedFiles().front();

    if (auto config = mainWindow->getConfig())
    {
        auto json = vme_config::serialize_object(config.get());

        if (json.isNull())
            return false;

        QFile outFile(filename);

        if (!outFile.open(QIODevice::WriteOnly))
            return false;

        if (outFile.write(json.toJson()) > 0)
        {
            filename = QDir().relativeFilePath(filename);
            mainWindow->setConfigFilename(filename);
            config->setModified(false);
            make_workspace_settings().setValue("LastVMEConfig", filename);
            return true;
        }
    }

    return false;
}

bool save_vme_config(MultiCrateMainWindow *mainWindow)
{
    auto filename = mainWindow->getConfigFilename();

    if (filename.isEmpty())
        return save_vme_config_as(mainWindow);

    if (auto config = mainWindow->getConfig())
    {
        auto json = vme_config::serialize_object(config.get());

        if (json.isNull())
            return false;

        QFile outFile(filename);

        if (!outFile.open(QIODevice::WriteOnly))
            return false;

        if (outFile.write(json.toJson()) > 0)
        {
            mainWindow->setConfigFilename(filename);
            config->setModified(false);
            return true;
        }
    }

    return false;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    mvme_init("mvme_multi_crate");
    app.setWindowIcon(QIcon(":/window_icon.png"));

    auto args = app.arguments();

    if (args.contains("--debug"))
        spdlog::set_level(spdlog::level::debug);

    if (args.contains("--trace"))
        spdlog::set_level(spdlog::level::trace);

#ifdef QT_NO_DEBUG
    QSplashScreen splash(QPixmap(":/splash-screen.png"),
                         Qt::CustomizeWindowHint | Qt::Window | Qt::WindowStaysOnTopHint);
    auto font = splash.font();
    font.setPixelSize(22);
    splash.setFont(font);
    splash.showMessage(QSL(
            "mvme - VME Data Acquisition\n"
            "© 2015-2023 mesytec GmbH & Co. KG"
            ), Qt::AlignHCenter);
    splash.show();

    const int splashMaxTime = 3000;
    QTimer splashTimer;
    splashTimer.setInterval(splashMaxTime);
    splashTimer.setSingleShot(true);
    splashTimer.start();
    QObject::connect(&splashTimer, &QTimer::timeout, &splash, &QWidget::close);
#endif

    // create and show the gui
    MultiCrateMainWindow w;

    struct MultiCrateGuiContext
    {
        // states
        GlobalMode globalMode = GlobalMode::DAQ;
        DAQState daqState = DAQState::Idle;
        MVMEState mvmeState = MVMEState::Idle;

        // config, controllers, readout
        std::shared_ptr<ConfigObject> vmeConfig;
        std::vector<std::unique_ptr<mvme_mvlc::MVLC_VMEController>> controllers;
        nng_socket readoutProducerSocket;
        nng_socket readoutConsumerSocket;

        // widgets and gui stuff
        WidgetRegistry widgetRegistry;
    };

    MultiCrateGuiContext context{};

    QObject::connect(&w, &MultiCrateMainWindow::newVmeConfig, &w, [&] {
        // TODO: multicrate or single crate? how many crates? mvlc urls?
        std::shared_ptr<ConfigObject> config(multi_crate::make_multicrate_config());
        w.setConfig(config, {});
    });

    QObject::connect(&w, &MultiCrateMainWindow::openVmeConfig, &w, [&] {
        // TODO: check for modified state of current config. ask to discard.
        //QString fileName = QFileDialog::getOpenFileName(&w, "Open VME Config", {},

    });

    QObject::connect(&w, &MultiCrateMainWindow::saveVmeConfig, &w, [&] {
        save_vme_config(&w);
    });

    QObject::connect(&w, &MultiCrateMainWindow::saveVmeConfigAs, &w,[&] {
        save_vme_config_as(&w);
    });

    QObject::connect(&w, &MultiCrateMainWindow::editVmeScript,
        &w, [&] (VMEScriptConfig *config) {
            if (context.widgetRegistry.hasObjectWidget(config))
            {
                context.widgetRegistry.activateObjectWidget(config);
            }
            else
            {
                // TODO: parse the script, determine meta tags, start the appropriate editor
                // (MVMEMainWindow::editVMEScript, VMEConfigTreeWidget::editScript()).
                auto widget = new VMEScriptEditor(config);
                context.widgetRegistry.addObjectWidget(widget, config, config->getId().toString());
                // TODO: signal from editor widget: logMessage(), runScript, addApplicationWidget
            }
    });

    w.show();

    {
        QSettings settings;
        w.restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
        w.restoreState(settings.value("mainWindowState").toByteArray());
    }

#ifdef QT_NO_DEBUG
    splash.raise();
#endif

    // Code to run on entering the event loop for the first time.
    QTimer::singleShot(0, [&w]() {

        QSettings settings;

        if (settings.contains(QSL("LastWorkspaceDirectory")))
        {
            auto wsDir = settings.value(QSL("LastWorkspaceDirectory")).toString();

            if (open_workspace(wsDir, &w))
            {
                w.statusBar()->showMessage(QSL("Opened workspace directory %1").arg(wsDir), 5);
            }
            else if (!create_new_or_open_existing_workspace(&w))
            {
                    w.close();
            }
        }
        else if (!create_new_or_open_existing_workspace(&w))
        {
                w.close();
        }
    });

    int ret = app.exec();
    mvme_shutdown();
    return ret;
}
