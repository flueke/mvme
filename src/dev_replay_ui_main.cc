#include <QApplication>
#include <QFileDialog>
#include <QThread>
#include <spdlog/spdlog.h>
#include <thread>
#include <memory>

//#include "mvlc_listfile_worker.h"
//#include "mvme_listfile_worker.h"
#include <mesytec-mvlc/mesytec-mvlc.h>
#include "mvme_session.h"
#include "replay_ui.h"

using namespace mesytec;

struct ReplayContext
{
    enum State
    {
        Idle,
        Running
    };

    //struct MvmeQueues
    //{
    //    ThreadSafeDataBufferQueue m_freeBuffers;
    //    ThreadSafeDataBufferQueue m_fullBuffers;
    //};
    //std::unique_ptr<MvmeQueues> mvmeQueues;

    //std::unique_ptr<ListfileReplayWorker> replayWorker;
    //std::unique_ptr<mesytec::mvlc::ReadoutBufferQueues> mvlcQueues;

    State state;
    mvme::replay::CommandHolder cmd;

    std::unique_ptr<mvlc::ReplayWorker> replayWorker;
    std::thread anaThread;
};

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::info);
    QApplication app(argc, argv);
    mvme_init("dev_replay_ui");

    auto browsePath = QSettings().value("LastBrowsePath", QSL(".")).toString();

    if (auto args = app.arguments(); args.size() > 1)
        browsePath = args.at(1);

    mvme::ReplayWidget replayWidget;
    replayWidget.browsePath(browsePath);

    QWidget toolsWidget;
    auto tb = make_toolbar(&toolsWidget);
    auto l = make_hbox(&toolsWidget);
    l->addWidget(tb);

    add_widget_close_action(&replayWidget);
    add_widget_close_action(&toolsWidget);
    replayWidget.show();
    toolsWidget.show();

    WidgetGeometrySaver geoSaver;
    geoSaver.addAndRestore(&replayWidget, "ReplayWidget");
    geoSaver.addAndRestore(&toolsWidget, "ToolsWidget");

    auto actionBrowse = tb->addAction("browse");
    auto actionGetQueue = tb->addAction("get_queue");
    auto actionClearCache = tb->addAction("clear cache");
    auto actionQuit = tb->addAction("quit");

    QObject::connect(actionBrowse, &QAction::triggered,
        &replayWidget, [&]
        {
            if (auto path = QFileDialog::getExistingDirectory(); !path.isEmpty())
            {
                replayWidget.browsePath(path);
                QSettings().setValue("LastBrowsePath", path);
            }
        });

    QObject::connect(actionGetQueue, &QAction::triggered,
        &replayWidget, [&]
        {
            qDebug() << replayWidget.getQueueContents();
        });

    QObject::connect(actionClearCache, &QAction::triggered,
        &replayWidget, [&]
        {
            replayWidget.clearFileInfoCache();
        });

    actionQuit->setShortcut(QSL("Ctrl+Q"));
    actionQuit->setShortcutContext(Qt::ApplicationShortcut);

    QObject::connect(actionQuit, &QAction::triggered,
        &app, QApplication::quit);

    mvme::replay::ListfileCommandExecutor cmdExecutor;

    QObject::connect(&cmdExecutor, &mvme::replay::ListfileCommandExecutor::started,
                     &replayWidget, &mvme::ReplayWidget::setRunning);

    QObject::connect(&cmdExecutor, &mvme::replay::ListfileCommandExecutor::finished,
                     &replayWidget, &mvme::ReplayWidget::setIdle);

    QObject::connect(&cmdExecutor, &mvme::replay::ListfileCommandExecutor::canceled,
                     &replayWidget, &mvme::ReplayWidget::setIdle);

    QObject::connect(&cmdExecutor, &mvme::replay::ListfileCommandExecutor::paused,
                     &replayWidget, &mvme::ReplayWidget::setPaused);

    QObject::connect(&cmdExecutor, &mvme::replay::ListfileCommandExecutor::resumed,
                     &replayWidget, &mvme::ReplayWidget::setRunning);

    QObject::connect(&replayWidget, &mvme::ReplayWidget::start,
        &replayWidget, [&]
        {
            auto cmd = replayWidget.getCommand();
            if (cmdExecutor.setCommand(cmd))
                cmdExecutor.start();
        });

    QObject::connect(&replayWidget, &mvme::ReplayWidget::stop,
        &cmdExecutor, &mvme::replay::ListfileCommandExecutor::cancel);

    QObject::connect(&replayWidget, &mvme::ReplayWidget::pause,
        &cmdExecutor, &mvme::replay::ListfileCommandExecutor::pause);

    QObject::connect(&replayWidget, &mvme::ReplayWidget::resume,
        &cmdExecutor, &mvme::replay::ListfileCommandExecutor::resume);

    QObject::connect(&replayWidget, &mvme::ReplayWidget::skip,
        &cmdExecutor, &mvme::replay::ListfileCommandExecutor::skip);

    return app.exec();
}
