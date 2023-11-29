#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QMenu>
#include <QMetaObject>
#include <QMessageBox>
#include <QSplashScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeView>

#include <cassert>
#include <future>
#include <spdlog/spdlog.h>

#include "multi_crate_gui.h"
#include "multi_crate_mainwindow.h"
#include "mvlc_daq.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvme_session.h"
#include "mvme_workspace.h"
#include "util/mesy_nng.h"
#include "util/qt_fs.h"
#include "util/qt_logview.h"
#include "util/qt_model_view_util.h"
#include "vme_config_model_view.h"
#include "vme_config_util.h"
#include "vme_controller_factory.h"
#include "vme_controller_ui.h"
#include "vme_script_editor.h"
#include "widget_registry.h"

using namespace mesytec;
using namespace mesytec::mvme;

static const QString VMEConfigFileFilter = QSL("Config Files (*.vme *.mvmecfg);; All Files (*)");

struct MultiCrateGuiContext
{
    // states
    GlobalMode globalMode = GlobalMode::DAQ;
    DAQState daqState = DAQState::Idle;
    MVMEState mvmeState = MVMEState::Idle;

    // config, controllers, readout
    //std::shared_ptr<ConfigObject> vmeConfig; // FIXME: need to keep mainwindow and this config synced...

    std::vector<std::unique_ptr<mvme_mvlc::MVLC_VMEController>> controllers;
    nng_socket readoutProducerSocket = NNG_SOCKET_INITIALIZER;
    nng_socket readoutConsumerSocket = NNG_SOCKET_INITIALIZER;
    std::vector<std::unique_ptr<multi_crate::ReadoutProducerContext>> readoutContexts;
    std::vector<std::future<void>> readoutProducers;
    std::future<std::error_code> readoutConsumer;
    std::atomic<bool> quitReadoutProducers;
    std::atomic<bool> quitReadoutConsumer;

    // widgets and gui stuff
    MultiCrateMainWindow *mainWindow;
    LogHandler logHandler;
    WidgetRegistry widgetRegistry;
    std::unique_ptr<MultiLogWidget> logWidget;

    ~MultiCrateGuiContext()
    {
        nng_close(readoutProducerSocket);
        nng_close(readoutConsumerSocket);
    }

    void logMessage(const QString &msg, const QString &category = {}) { logHandler.logMessage(msg, category); }
};

// TODO: add autosave handling for vme and analysis configs at some point (see MVMEContext::openWorkspace())
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

// TODO: add error checking and reporting
std::pair<std::unique_ptr<ConfigObject>, QString> gui_open_vme_config()
{
    auto filename = QFileDialog::getOpenFileName(nullptr, "Open VME Config", {}, VMEConfigFileFilter);

    if (filename.isEmpty())
        return {};

    QFile inFile(filename);

    if (!inFile.open(QIODevice::ReadOnly))
        return {};

    auto data = inFile.readAll();
    auto doc = QJsonDocument::fromJson(data);
    return std::make_pair(vme_config::deserialize_object(doc.object()), QDir().relativeFilePath(filename));
}

bool edit_vme_controller_settings(VMEConfig *config)
{
    VMEControllerSettingsDialog dialog;
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setCurrentController(config->getControllerType(), config->getControllerSettings());

    if (dialog.exec() == QDialog::Accepted)
    {
        auto controllerType = dialog.getControllerType();
        auto controllerSettings = dialog.getControllerSettings();
        config->setVMEController(controllerType, controllerSettings);
        return true;
    }

    return false;
}

void handle_vme_tree_context_menu(MultiCrateGuiContext &ctx, const QPoint &pos)
{
    const bool isIdle = ctx.mvmeState == MVMEState::Idle;
    auto view = ctx.mainWindow->getVmeConfigTree();
    auto model = ctx.mainWindow->getVmeConfigModel();
    auto index = view->indexAt(pos);
    auto item  = model->itemFromIndex(index);
    auto obj = qobject_from_item<ConfigObject>(item);

    QMenu menu;
    QAction *action = nullptr;

    if (auto vmeConfig = qobject_cast<VMEConfig *>(obj); vmeConfig && isIdle)
    {
        action = menu.addAction(QIcon(":/gear.png"), "Edit VME Controller Settings",
            view, [vmeConfig, &ctx] {
                if (edit_vme_controller_settings(vmeConfig))
                {
                    // FIXME: this should be "update info text for vmeConfig. No
                    // need to reload the whole view just because the MVLC uri changed.
                    ctx.mainWindow->reloadView();
                }
            });
    }

    if (!menu.isEmpty())
        menu.exec(view->mapToGlobal(pos));
}

// MVLC creation. what a mess...
std::unique_ptr<mvme_mvlc::MVLC_VMEController> make_mvlc(const VMEConfig *vmeConfig)
{
    VMEControllerFactory f(vmeConfig->getControllerType());
    auto controller = f.makeController(vmeConfig->getControllerSettings());
    auto mvlcRaw = qobject_cast<mvme_mvlc::MVLC_VMEController *>(controller);
    if (!mvlcRaw)
    {
        delete controller;
        return {};
    }
    std::unique_ptr<mvme_mvlc::MVLC_VMEController> mvlc(mvlcRaw);
    return mvlc;
}

void start_daq(MultiCrateGuiContext &ctx)
{
    if (ctx.mvmeState != MVMEState::Idle)
        return;

    // TODO: need to implement this for VMEConfig and for MulticrateVMEConfig or find another way to support both.
    // TODO: need to figure out which if any of the vme controllers were changed in the vme configs and need to recreate these.
    //       Unmodified controllers could remain.
    //
    // *) connect vme controllers
    // *) setup mvlcs, upload stacks, triggerio, etc.
    // *) run the daq init sequence for each of the crates
    // *) create output listfile, write magic and preamble
    // *) create and start readout threads and a listfile writer thread using the nng sockets
    // *) start the readout and listfile writer threads
    // *) send the global daq start scripts (soft triggers kick of the multi crate daq)

    // FIXME: this is a hack. MVLCs that did not change should be kept to avoid
    // useless reconnecting all the time. Right now there's no way to compare
    // connection info with an existing mvlc instance to determine if there was a change.
    ctx.controllers.clear();
    ctx.readoutProducers.clear();

    auto config = qobject_cast<multi_crate::MulticrateVMEConfig *>(ctx.mainWindow->getConfig().get());

    if (!config)
        return;

    // *) connect vme controllers
    auto crateConfigs = config->getCrateConfigs();
    const auto crateCount = crateConfigs.size();

    for (size_t crateId = 0; crateId < crateCount; ++crateId)
    {
        auto vmeConfig = crateConfigs[crateId];
        auto mvlc = make_mvlc(vmeConfig);

        if (!mvlc)
        {
            ctx.logMessage(QSL("Error: could not create an MVLC instance for crate%1").arg(crateId));
            return;
        }

        if (auto err = mvlc->open(); err.isError())
        {
            ctx.logMessage((QSL("Error connecting to %1: %2")
                .arg(mvlc->getMVLCObject()->connectionInfo().c_str())
                .arg(err.toString())));
            return;
        }

        ctx.controllers.emplace_back(std::move(mvlc));
    }

    // *) setup mvlcs, upload stacks, triggerio, etc.
    for (size_t crateId = 0; crateId < crateCount; ++crateId)
    {
        auto crateConfig = config->getCrateConfig(crateId);
        auto mvlcCtrl = ctx.controllers[crateId].get();
        auto mvlc = mvlcCtrl->getMVLC();

        auto logger = [&] (const QString &msg) { ctx.logMessage(msg, QSL("crate%1").arg(crateId)); };

        logger(QSL("  Setting crate id = %1").arg(crateId));
        if (auto ec = mvlc.writeRegister(mvlc::registers::controller_id, crateId))
        {
            logger(QSL("Error setting crate id: %2").arg(ec.message().c_str()));
            return;
        }

        // TODO: use QtConcurrent::run() to execute this non-blocking. Use a
        // future watcher and a progress dialog to block the gui while this is ongoing.
        auto res = mvme_mvlc::run_daq_start_sequence(mvlcCtrl, *crateConfig, false, logger, logger);

        if (!res)
            return;
    }

    // *) create and connect nng sockets

    if (int res = nng_pull0_open(&ctx.readoutConsumerSocket))
    {
        ctx.logMessage(QSL("Internal error: %1").arg(nng_strerror(res)));
        return;
    }

    if (int res = nng_push0_open(&ctx.readoutProducerSocket))
    {
        ctx.logMessage(QSL("Internal error: %1").arg(nng_strerror(res)));
        return;
    }

    if (int res = nng_listen(ctx.readoutConsumerSocket, "inproc://mvlc_readout", nullptr, 0))
    {
        ctx.logMessage(QSL("Internal error: %1").arg(nng_strerror(res)));
        return;
    }

    if (int res = nng_dial(ctx.readoutProducerSocket, "inproc://mvlc_readout", nullptr, 0))
    {
        ctx.logMessage(QSL("Internal error: %1").arg(nng_strerror(res)));
        return;
    }

    // TODO *) create output listfile, write magic and preamble

    // *) create and start readout threads and a listfile writer thread using the nng sockets

    for (size_t crateId = 0; crateId < crateCount; ++crateId)
    {
        auto mvlcCtrl = ctx.controllers[crateId].get();
        auto mvlc = mvlcCtrl->getMVLC();

        auto readoutContext = std::make_unique<multi_crate::ReadoutProducerContext>();
        readoutContext->crateId = crateId;
        readoutContext->mvlc = mvlc;
        readoutContext->outputSocket = ctx.readoutProducerSocket;

        auto producerFuture = std::async(std::launch::async, multi_crate::mvlc_readout_loop,
            std::ref(*readoutContext), std::ref(ctx.quitReadoutProducers));
        ctx.readoutProducers.emplace_back(std::move(producerFuture));
    }

    // *) start the readout and listfile writer threads
    // *) send the global daq start scripts (soft triggers kick of the multi crate daq)
}

void stop_daq(MultiCrateGuiContext &ctx)
{
    // *) send the master global daq stop scripts (soft trigger to trigger the stop_event)
    // *) tell readouts to quit
    // *) quit the listfile writer
    // *) close the listfile, add logs and analysis to output archive
    // *) drink more coffee
}

void pause_daq(MultiCrateGuiContext &ctx)
{
    // *) send the master global daq stop scripts (TODO: what to do on error here? state is kinda broken then)
}

void resume_daq(MultiCrateGuiContext &ctx)
{
    // *) send the master global daq start scripts (TODO: what to do on error here? state is kinda broken then)
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

    MultiCrateGuiContext context{};
    context.mainWindow = &w;
    context.logWidget = std::make_unique<MultiLogWidget>();

    auto on_message_logged = [w=context.logWidget.get()] (const QString &msg, const QString &category)
    {
        w->appendMessage(msg, category);
    };

    QObject::connect(&context.logHandler, &LogHandler::messageLogged,
        context.logWidget.get(), on_message_logged, Qt::QueuedConnection);

    QObject::connect(&w, &MultiCrateMainWindow::newVmeConfig, &w, [&] {
        // TODO: multicrate or single crate? how many crates? mvlc urls?
        std::shared_ptr<ConfigObject> config(multi_crate::make_multicrate_config());
        w.setConfig(config, {});
    });

    QObject::connect(&w, &MultiCrateMainWindow::openVmeConfig, &w, [&] {
        // TODO: check for modified state of current config. ask to discard.
        //QString fileName = QFileDialog::getOpenFileName(&w, "Open VME Config", {},

        auto result = gui_open_vme_config();

        if (result.first)
        {
            std::shared_ptr<ConfigObject> config(result.first.release());
            auto filename = result.second;
            w.setConfig(config, filename);
        }
    });

    QObject::connect(&w, &MultiCrateMainWindow::saveVmeConfig, &w, [&] {
        save_vme_config(&w);
    });

    QObject::connect(&w, &MultiCrateMainWindow::saveVmeConfigAs, &w,[&] {
        save_vme_config_as(&w);
    });

    QObject::connect(&w, &MultiCrateMainWindow::exploreWorkspace, &w,[&] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir().absolutePath()));
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
                // TODO: signals from editor widget: logMessage(), runScript, addApplicationWidget
            }
    });

    QObject::connect(&w, &MultiCrateMainWindow::editVmeScriptAsText,
        &w, [&] (VMEScriptConfig *config) {
            if (context.widgetRegistry.hasObjectWidget(config))
            {
                context.widgetRegistry.activateObjectWidget(config);
            }
            else
            {
                auto widget = new VMEScriptEditor(config);
                context.widgetRegistry.addObjectWidget(widget, config, config->getId().toString());
                // TODO: signals from editor widget: logMessage(), runScript, addApplicationWidget
            }
    });

    QObject::connect(&w, &MultiCrateMainWindow::vmeTreeContextMenuRequested,
        &w, [&] (const QPoint &pos) { handle_vme_tree_context_menu(context, pos); });

    w.show();
    context.logWidget->show();

    {
        QSettings settings;
        w.restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
        w.restoreState(settings.value("mainWindowState").toByteArray());
    }

    QObject::connect(&w, &MultiCrateMainWindow::startDaq,
        &w, [&] { start_daq(context); });

    QObject::connect(&w, &MultiCrateMainWindow::stopDaq,
        &w, [&] { stop_daq(context); });

    QObject::connect(&w, &MultiCrateMainWindow::pauseDaq,
        &w, [&] { pause_daq(context); });

    QObject::connect(&w, &MultiCrateMainWindow::resumeDaq,
        &w, [&] { resume_daq(context); });

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
