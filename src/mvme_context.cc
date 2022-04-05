/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

// Includes winsock2.h so it's placed at the top to avoid warnings.
#include "mvme_context.h"

#include "analysis/a2_adapter.h"
#include "analysis/a2/memory.h"
#include "analysis/analysis.h"
#include "analysis/analysis_session.h"
#include "analysis/analysis_ui.h"
#include "event_server/server/event_server.h"
#include "file_autosaver.h"
#include "logfile_helper.h"
#include <mesytec-mvlc/mesytec-mvlc.h>
#include "mvme_mvlc_listfile.h"
#include "mvlc_listfile_worker.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvlc_readout_worker.h"
#include "mvlc_stream_worker.h"
#include "mvme_context_lib.h"
#include "mvmecontext_analysis_service_provider.h"
#include "mvme.h"
#include "mvme_listfile_worker.h"
#include "mvme_stream_worker.h"
#include "mvme_workspace.h"
#include "remote_control.h"
#include "sis3153.h"
#include "util/ticketmutex.h"
#include "vme_analysis_common.h"
#include "vme_config_ui.h"
#include "vme_config_util.h"
#include "vme_controller_factory.h"
#include "vme_script.h"
#include "vmusb_buffer_processor.h"
#include "vmusb.h"
#include "vmusb_readout_worker.h"

#include "git_sha1.h"

#include <QHostAddress>
#include <QMessageBox>
#include <QProgressDialog>
#include <QtConcurrent>
#include <QThread>
#include <qnamespace.h>
#include <tuple>

namespace
{

/* Buffers to pass between DAQ/replay and the analysis. */
static const size_t ReadoutBufferCount = 4;
static const size_t ReadoutBufferSize = Megabytes(1);

static const int TryOpenControllerInterval_ms = 1000;
static const int PeriodicLoggingInterval_ms = 5000;

static const int DefaultListFileCompression = 1;

static const QString RunNotesFilename = QSL("mvme_run_notes.txt");

static const QString VMEConfigAutoSaveFilename = QSL(".vme_autosave.vme");
static const QString AnalysisAutoSaveFilename  = QSL(".analysis_autosave.analysis");
static const int DefaultConfigFileAutosaveInterval_ms = 1 * 60 * 1000;

/* Maximum number of connection attempts to the current VMEController before
 * giving up. */
static const int VMECtrlConnectMaxRetryCount = 3;

/* Maximum number of entries to keep in the logbuffer. Once this is exceeded
 * the oldest entries will be removed. */
static const s64 LogBufferMaxEntries = 100 * 1000;

static const int JSON_RPC_DefaultListenPort = 13800;
static const int EventServer_DefaultListenPort = 13801;

// The number of DAQ run logfiles to keep in run_logs/
static const unsigned Default_RunLogsMaxCount = 50;
static const QString RunLogsWorkspaceDirectory = QSL("run_logs");

static const QString LogsWorkspaceDirectory = QSL("logs");

class VMEConfigSerializer
{
    public:
        explicit VMEConfigSerializer(MVMEContext *context)
            : m_context(context)
        { }

        QByteArray operator()()
        {
            auto doc = mvme::vme_config::serialize_vme_config_to_json_document(
                *m_context->getVMEConfig());

            return doc.toJson();
        }

    private:
        MVMEContext *m_context;
};

class AnalysisSerializer
{
    public:
        explicit AnalysisSerializer(MVMEContext *context)
            : m_context(context)
        { }

        QByteArray operator()()
        {
            auto vmeConfig = m_context->getVMEConfig();
            auto analysis = m_context->getAnalysis();

            vme_analysis_common::update_analysis_vme_properties(vmeConfig, analysis);

            auto doc = analysis::serialize_analysis_to_json_document(*analysis);

            return doc.toJson();
        }

    private:
        MVMEContext *m_context;
};

} // end anon namespace

using remote_control::RemoteControl;
using mesytec::mvme::LogfileCountLimiter;
using mesytec::mvme::LastlogHelper;

struct MVMEContextPrivate
{
    MVMEContext *m_q;
    MVMEOptions m_options;
    QStringList m_logBuffer;
    QMutex m_logBufferMutex;
    ListFileOutputInfo m_listfileOutputInfo = {};
    RunInfo m_runInfo;
    u32 m_ctrlOpenRetryCount = 0;
    bool m_isFirstConnectionAttempt = false;
    mesytec::mvme::TicketMutex tryOpenControllerMutex;

    std::unique_ptr<FileAutoSaver> m_vmeConfigAutoSaver;
    std::unique_ptr<FileAutoSaver> m_analysisAutoSaver;

    std::unique_ptr<RemoteControl> m_remoteControl;
    // owned by the MVMEStreamWorker
    EventServer *m_eventServer = nullptr;

    ListfileReplayHandle listfileReplayHandle;
    std::unique_ptr<ListfileReplayWorker> listfileReplayWorker;
    mesytec::mvlc::ReadoutBufferQueues mvlcSnoopQueues;
    mutable mesytec::mvlc::Protected<QString> runNotes;

    std::unique_ptr<mesytec::mvme::LogfileCountLimiter> daqRunLogfileLimiter;
    std::unique_ptr<LastlogHelper> lastLogfileHelper;

    mesytec::mvlc::WaitableProtected<MVMEState> m_mvmeState;

    AnalysisServiceProvider *analysisServiceProvider = nullptr;

    MVMEContextPrivate(MVMEContext *q, const MVMEOptions &options)
        : m_q(q)
        , m_options(options)
        , mvlcSnoopQueues(ReadoutBufferSize, ReadoutBufferCount)
        , m_mvmeState(MVMEState::Idle)
    {}

    void stopDAQ();
    void pauseDAQ();
    void resumeDAQ(u32 nEvents);

    // DAQ - Readout
    void stopDAQReadout();
    void pauseDAQReadout();
    void resumeDAQReadout(u32 nEvents);

    // DAQ - Replay
    void stopDAQReplay();
    void pauseDAQReplay();
    void resumeDAQReplay(u32 nEvents);

    // Analysis
    void stopAnalysis();
    void resumeAnalysis(analysis::Analysis::BeginRunOption option);

    void updateMVMEState();

    void clearLog();

    void maybeSaveDAQNotes();
    void workspaceClosingCleanup();
};

void MVMEContextPrivate::stopDAQ()
{
    switch (m_q->m_mode)
    {
        case GlobalMode::DAQ:       stopDAQReadout(); break;
        case GlobalMode::ListFile:  stopDAQReplay(); break;
    }
}

void MVMEContextPrivate::pauseDAQ()
{
    switch (m_q->m_mode)
    {
        case GlobalMode::DAQ:       pauseDAQReadout(); break;
        case GlobalMode::ListFile:  pauseDAQReplay(); break;
    }
}

void MVMEContextPrivate::resumeDAQ(u32 nEvents)
{
    switch (m_q->m_mode)
    {
        case GlobalMode::DAQ:       resumeDAQReadout(nEvents); break;
        case GlobalMode::ListFile:  resumeDAQReplay(nEvents); break;
    }
}

/* Stops both the readout and the analysis workers. */
void MVMEContextPrivate::stopDAQReadout()
{
    QProgressDialog progressDialog("Stopping Data Acquisition", QString(), 0, 0);
    progressDialog.setWindowModality(Qt::ApplicationModal);
    progressDialog.setCancelButton(nullptr);
    progressDialog.show();


    if (m_q->m_readoutWorker->isRunning())
    {
        QEventLoop localLoop;
        QObject::connect(m_q->m_readoutWorker, &VMEReadoutWorker::daqStopped,
                         &localLoop, &QEventLoop::quit);

        QMetaObject::invokeMethod(
            m_q, [this] () { m_q->m_readoutWorker->stop(); }, Qt::QueuedConnection);

        localLoop.exec();

        qDebug() << __PRETTY_FUNCTION__ << "readout worker stopped";
    }


    if (m_q->m_streamWorker->getState() != AnalysisWorkerState::Idle)
    {

        QEventLoop localLoop;
        QObject::connect(m_q->m_streamWorker.get(), &MVMEStreamWorker::stopped,
                         &localLoop, &QEventLoop::quit);

        qDebug() << __PRETTY_FUNCTION__ << QDateTime::currentDateTime()
            << "pre streamWorker->stop";

        m_q->m_streamWorker->stop(false);

        localLoop.exec();

        qDebug() << __PRETTY_FUNCTION__ << QDateTime::currentDateTime()
            << "post streamWorker->stop";
    }

    qDebug() << __PRETTY_FUNCTION__ << "stream worker stopped";

    Q_ASSERT(m_q->m_readoutWorker->getState() == DAQState::Idle);
    m_q->onDAQStateChanged(DAQState::Idle);
}

void MVMEContextPrivate::pauseDAQReadout()
{
    QProgressDialog progressDialog("Pausing Data Acquisition", QString(), 0, 0);
    progressDialog.setWindowModality(Qt::ApplicationModal);
    progressDialog.setCancelButton(nullptr);
    progressDialog.show();

    QEventLoop localLoop;

    if (m_q->m_readoutWorker->getState() == DAQState::Running)
    {
        auto con = QObject::connect(m_q->m_readoutWorker, &VMEReadoutWorker::daqPaused,
                                    &localLoop, &QEventLoop::quit);
        QMetaObject::invokeMethod(
            m_q, [this] () { m_q->m_readoutWorker->pause(); }, Qt::QueuedConnection);

        qDebug() << __PRETTY_FUNCTION__ << "entering localLoop";
        localLoop.exec();
        qDebug() << __PRETTY_FUNCTION__ << "left localLoop";

        QObject::disconnect(con);
    }

    Q_ASSERT(m_q->m_readoutWorker->getState() == DAQState::Paused
             || m_q->m_readoutWorker->getState() == DAQState::Idle);
}

void MVMEContextPrivate::resumeDAQReadout(u32 /*nEvents*/)
{
    m_q->m_readoutWorker->resume();
}

void MVMEContextPrivate::stopDAQReplay()
{
    QProgressDialog progressDialog("Stopping Replay", QString(), 0, 0);
    progressDialog.setWindowModality(Qt::ApplicationModal);
    progressDialog.setCancelButton(nullptr);
    progressDialog.show();

    QEventLoop localLoop;

    // First stop the ListfileReplayWorker

    // The timer is used to avoid a race between the worker stopping and the
    // progress dialog entering its eventloop. (Probably not needed, see the
    // explanation about not having a race condition below.)

    if (listfileReplayWorker->getState() == DAQState::Running
        || listfileReplayWorker->getState() == DAQState::Paused)
    {
        auto con = QObject::connect(
            listfileReplayWorker.get(), &ListfileReplayWorker::replayStopped,
            &localLoop, &QEventLoop::quit);

        QMetaObject::invokeMethod(
            m_q, [this] () { listfileReplayWorker->stop(); }, Qt::QueuedConnection);

        localLoop.exec();
        QObject::disconnect(con);
    }

    // At this point the ListfileReplayWorker is stopped and will not produce any
    // more buffers. Now tell the MVMEStreamWorker to stop after finishing
    // the current queue.

    // There should be no race here. If the analysis is running we will stop it
    // and receive the stopped() signal.  If it just now stopped on its own
    // (e.g. end of replay) the signal is pending and will be delivered as soon
    // as we enter the event loop.
    if (m_q->m_streamWorker->getState() != AnalysisWorkerState::Idle)
    {
        auto con = QObject::connect(m_q->m_streamWorker.get(), &MVMEStreamWorker::stopped,
                                    &localLoop, &QEventLoop::quit);

        QMetaObject::invokeMethod(
            m_q, [this] () { m_q->m_streamWorker->stop(); }, Qt::QueuedConnection);

        localLoop.exec();
        QObject::disconnect(con);
    }

    Q_ASSERT(m_q->m_readoutWorker->getState() == DAQState::Idle);

    if (auto mvmeStreamWorker = qobject_cast<MVMEStreamWorker *>(m_q->m_streamWorker.get()))
    {
        mvmeStreamWorker->setListFileVersion(CurrentListfileVersion);
    }
    m_q->onDAQStateChanged(DAQState::Idle);
}

void MVMEContextPrivate::pauseDAQReplay()
{
    QProgressDialog progressDialog("Stopping Replay", QString(), 0, 0);
    progressDialog.setWindowModality(Qt::ApplicationModal);
    progressDialog.setCancelButton(nullptr);
    progressDialog.show();

    QEventLoop localLoop;

    if (listfileReplayWorker->getState() == DAQState::Running)
    {
        auto con = QObject::connect(
            listfileReplayWorker.get(), &ListfileReplayWorker::replayPaused,
            &localLoop, &QEventLoop::quit);

        QMetaObject::invokeMethod(
            m_q, [this] () { listfileReplayWorker->pause(); }, Qt::QueuedConnection);

        localLoop.exec();

        QObject::disconnect(con);

        Q_ASSERT(listfileReplayWorker->getState() == DAQState::Paused);
    }

    Q_ASSERT(m_q->m_readoutWorker->getState() == DAQState::Paused
             || m_q->m_readoutWorker->getState() == DAQState::Idle);
}

void MVMEContextPrivate::resumeDAQReplay(u32 nEvents)
{
    listfileReplayWorker->setEventsToRead(nEvents);
    listfileReplayWorker->resume();
}

void MVMEContextPrivate::stopAnalysis()
{
    QProgressDialog progressDialog("Stopping Analysis", QString(), 0, 0);
    progressDialog.setWindowModality(Qt::ApplicationModal);
    progressDialog.setCancelButton(nullptr);
    progressDialog.show();

    QEventLoop localLoop;

    if (m_q->m_streamWorker->getState() != AnalysisWorkerState::Idle)
    {
        // Tell the analysis to stop immediately
        m_q->m_streamWorker->stop(false);
        QObject::connect(m_q->m_streamWorker.get(), &MVMEStreamWorker::stopped,
                         &localLoop, &QEventLoop::quit);
        localLoop.exec();
    }

    qDebug() << __PRETTY_FUNCTION__ << "analysis stopped";
}

void MVMEContextPrivate::resumeAnalysis(analysis::Analysis::BeginRunOption runOption)
{
    assert(m_q->m_streamWorker->getState() == AnalysisWorkerState::Idle);

    if (m_q->m_streamWorker->getState() == AnalysisWorkerState::Idle)
    {
        // TODO: merge with the build  code in prepareStart().
        auto analysis = m_q->getAnalysis();
        analysis->beginRun(
            runOption, m_q->getVMEConfig(),
            [this] (const QString &msg) { m_q->logMessage(msg); });

        bool invoked = QMetaObject::invokeMethod(m_q->m_streamWorker.get(), "start",
                                                 Qt::QueuedConnection);

        assert(invoked);

        qDebug() << __PRETTY_FUNCTION__ << "analysis resumed";
    }
}

void MVMEContextPrivate::updateMVMEState()
{
    // Keep access() object alive and state locked during the whole function.
    auto sa = m_mvmeState.access();
    auto curState = sa.copy();
    MVMEState newState = curState;

    auto daqState = m_q->getDAQState();
    auto analysisState = m_q->getMVMEStreamWorkerState();

    // FIXME: this can transition from Running to Starting by first hitting the
    // last 'else' case (the analysis side is running) then hitting the
    // 'Starting' case by the replay side starting up.
    if (daqState == DAQState::Idle && analysisState == AnalysisWorkerState::Idle)
        newState = MVMEState::Idle;
    else if (daqState == DAQState::Starting)
        newState = MVMEState::Starting;
    else if (daqState == DAQState::Running)
        newState = MVMEState::Running;
    else if (daqState == DAQState::Stopping)
        newState = MVMEState::Stopping;
    else
        newState = MVMEState::Running;

    if (curState != newState)
    {
        qDebug() << __PRETTY_FUNCTION__ <<
            "oldState =" << to_string(curState)
            << ", newState =" << to_string(newState);

        sa.ref() = newState;
        emit m_q->mvmeStateChanged(newState);
    }
}

void MVMEContextPrivate::clearLog()
{
    QMutexLocker lock(&m_logBufferMutex);
    m_logBuffer.clear();

    if (m_q->m_mainwin)
    {
        m_q->m_mainwin->clearLog();
    }
}

static void writeToSettings(const ListFileOutputInfo &info, QSettings &settings)
{
    settings.setValue(QSL("WriteListFile"),             info.enabled);
    settings.setValue(QSL("ListFileFormat"),            toString(info.format));
    if (!info.directory.isEmpty())
        settings.setValue(QSL("ListFileDirectory"),         info.directory);
    settings.setValue(QSL("ListFileCompressionLevel"),  info.compressionLevel);
    settings.setValue(QSL("ListFilePrefix"),            info.prefix);
    settings.setValue(QSL("ListFileRunNumber"),         info.runNumber);
    settings.setValue(QSL("ListFileOutputFlags"),       info.flags);
    settings.setValue(QSL("ListFileSplitSize"), static_cast<quint64>(info.splitSize));
    settings.setValue(QSL("ListFileSplitTime"), static_cast<quint64>(info.splitTime.count()));
}

static ListFileOutputInfo readFromSettings(QSettings &settings)
{
    ListFileOutputInfo result;
    result.enabled          = settings.value(QSL("WriteListFile"), QSL("true")).toBool();
    result.format           = listFileFormat_fromString(settings.value(QSL("ListFileFormat")).toString());
    if (result.format == ListFileFormat::Invalid)
        result.format = ListFileFormat::ZIP;
    result.directory        = settings.value(QSL("ListFileDirectory"), QSL("listfiles")).toString();
    result.compressionLevel = settings.value(QSL("ListFileCompressionLevel"), DefaultListFileCompression).toInt();
    result.prefix           = settings.value(QSL("ListFilePrefix"), QSL("mvmelst")).toString();
    result.runNumber        = settings.value(QSL("ListFileRunNumber"), 1u).toUInt();
    result.flags            = settings.value(QSL("ListFileOutputFlags"), ListFileOutputInfo::UseRunNumber).toUInt();
    result.splitSize        = settings.value(QSL("ListFileSplitSize"), static_cast<quint64>(result.splitSize)).toUInt();
    result.splitTime        = std::chrono::seconds(
        settings.value(QSL("ListFileSplitTime"), static_cast<quint64>(result.splitTime.count())).toUInt());

    return result;
}

MVMEContext::MVMEContext(MVMEMainWindow *mainwin, QObject *parent, const MVMEOptions &options)
    : QObject(parent)
    , m_d(new MVMEContextPrivate(this, options))
    , m_listFileFormat(ListFileFormat::ZIP)
    , m_ctrlOpenTimer(new QTimer(this))
    , m_logTimer(new QTimer(this))
    , m_readoutThread(new QThread(this))
    , m_analysisThread(new QThread(this))
    , m_mainwin(mainwin)

    , m_mode(GlobalMode::DAQ)
    , m_daqState(DAQState::Idle)
    , m_analysis(std::make_unique<analysis::Analysis>())
{
    m_d->m_remoteControl = std::make_unique<RemoteControl>(this);
    m_d->analysisServiceProvider = new MVMEContextServiceProvider(this, this);

    for (size_t i=0; i<ReadoutBufferCount; ++i)
    {
        enqueue(&m_freeBuffers, new DataBuffer(ReadoutBufferSize));
    }

#if 0
    auto bufferQueueDebugTimer = new QTimer(this);
    bufferQueueDebugTimer->start(5000);
    connect(bufferQueueDebugTimer, &QTimer::timeout, this, [this] () {
        qDebug() << "MVMEContext:"
            << "free buffers:" << queue_size(&m_freeBuffers)
            << "filled buffers:" << queue_size(&m_fullBuffers);
    });
#endif

    connect(m_ctrlOpenTimer, &QTimer::timeout, this, &MVMEContext::tryOpenController);
    m_ctrlOpenTimer->setInterval(TryOpenControllerInterval_ms);
    m_ctrlOpenTimer->start();

    connect(&m_ctrlOpenWatcher, &QFutureWatcher<VMEError>::finished,
            this, &MVMEContext::onControllerOpenFinished);

    connect(m_logTimer, &QTimer::timeout, this, &MVMEContext::logModuleCounters);
    m_logTimer->setInterval(PeriodicLoggingInterval_ms);


    // Setup the readout side: readout thread and listfile reader.
    // The vme controller specific readout worker is created and setup in
    // setVMEController().
    m_readoutThread->setObjectName("readout");
    m_readoutThread->start();

    // Setup the analysis/data processing side.
    m_analysisThread->setObjectName("analysis");
    m_analysisThread->start();

    qDebug() << __PRETTY_FUNCTION__ << "startup: using a default constructed VMEConfig";

    setMode(GlobalMode::DAQ);
    setVMEConfig(new VMEConfig(this));

    qDebug() << __PRETTY_FUNCTION__ << "startup done, contents of logbuffer:";
    qDebug() << getLogBuffer();
}

MVMEContext::~MVMEContext()
{
    qDebug() << "Entering" << __PRETTY_FUNCTION__;

    if (getDAQState() != DAQState::Idle)
    {
        qDebug() << __PRETTY_FUNCTION__ << "waiting for DAQ/Replay to stop";

        if (getMode() == GlobalMode::DAQ)
        {
            m_readoutWorker->stop();
        }
        else if (getMode() == GlobalMode::ListFile)
        {
            m_d->listfileReplayWorker->stop();
        }

        while ((getDAQState() != DAQState::Idle))
        {
            processQtEvents();
            QThread::msleep(50);
        }
    }

    if (m_streamWorker->getState() != AnalysisWorkerState::Idle)
    {
        qDebug() << __PRETTY_FUNCTION__ << "waiting for event processing to stop";

        m_streamWorker->stop(false);

        while (getMVMEStreamWorkerState() != AnalysisWorkerState::Idle)
        {
            processQtEvents();
            QThread::msleep(50);
        }
    }

    m_readoutThread->quit();
    m_readoutThread->wait();
    m_analysisThread->quit();
    m_analysisThread->wait();

    // Wait for possibly active VMEController::open() to return before deleting
    // the controller object.
    m_ctrlOpenFuture.waitForFinished();

    // Disconnect controller signals so that we're not emitting our own
    // controllerStateChanged anymore.
    disconnect(m_controller, &VMEController::controllerStateChanged,
               this, &MVMEContext::controllerStateChanged);

    // Same for daqStateChanged() and mvmeStreamWorkerStateChanged
    disconnect(m_readoutWorker, &VMEReadoutWorker::stateChanged,
               this, &MVMEContext::onDAQStateChanged);

    disconnect(m_d->listfileReplayWorker.get(), &ListfileReplayWorker::stateChanged,
               this, &MVMEContext::onDAQStateChanged);

    disconnect(m_streamWorker.get(), &MVMEStreamWorker::stateChanged,
               this, &MVMEContext::onAnalysisWorkerStateChanged);

    delete m_controller;
    delete m_readoutWorker;

    Q_ASSERT(queue_size(&m_freeBuffers) + queue_size(&m_fullBuffers) == ReadoutBufferCount);
    qDeleteAll(m_freeBuffers.queue);
    qDeleteAll(m_fullBuffers.queue);

    m_d->workspaceClosingCleanup();

    delete m_d;

    qDebug() << __PRETTY_FUNCTION__ << "Leaving" << __PRETTY_FUNCTION__;
}

void MVMEContext::setVMEConfig(VMEConfig *config)
{
    emit vmeConfigAboutToBeSet(m_vmeConfig, config);

    if (m_vmeConfig)
    {
        for (auto eventConfig: m_vmeConfig->getEventConfigs())
            onEventAboutToBeRemoved(eventConfig);

        auto scripts = m_vmeConfig->getGlobalObjectRoot().findChildren<VMEScriptConfig *>();

        for (auto script: scripts)
            emit objectAboutToBeRemoved(script);

        m_vmeConfig->deleteLater();
    }

    m_vmeConfig = config;
    config->setParent(this);

    for (auto event: config->getEventConfigs())
        onEventAdded(event);

    connect(m_vmeConfig, &VMEConfig::eventAdded,
            this, &MVMEContext::onEventAdded);

    connect(m_vmeConfig, &VMEConfig::eventAboutToBeRemoved,
            this, &MVMEContext::onEventAboutToBeRemoved);

    connect(m_vmeConfig, &VMEConfig::globalChildAboutToBeRemoved,
            this, &MVMEContext::onGlobalChildAboutToBeRemoved);

    if (m_readoutWorker)
    {
        VMEReadoutWorkerContext workerContext = m_readoutWorker->getContext();
        workerContext.vmeConfig = m_vmeConfig;
        m_readoutWorker->setContext(workerContext);
    }

    setVMEController(config->getControllerType(), config->getControllerSettings());

    if (m_d->m_vmeConfigAutoSaver)
    {
        // (re)start the autosaver
        m_d->m_vmeConfigAutoSaver->start();
    }

    emit vmeConfigChanged(config);
}

// Return value is (enabled, listen_hostinfo, listen_port)
static std::tuple<bool, QHostInfo, int> get_event_server_listen_info(const QSettings &workspaceSettings)
{
    if (!workspaceSettings.value(QSL("EventServer/Enabled")).toBool())
        return std::make_tuple(false, QHostInfo(), 0);

    auto addressString = workspaceSettings.value(QSL("EventServer/ListenAddress")).toString();
    auto port = workspaceSettings.value(QSL("EventServer/ListenPort")).toInt();

    if (!addressString.isEmpty())
    {
        return std::make_tuple(true, QHostInfo::fromName(addressString), port);
    }

    return std::make_tuple(true, QHostInfo::fromName("127.0.0.1"), port);
}

bool MVMEContext::setVMEController(VMEController *controller, const QVariantMap &settings)
{
    //qDebug() << __PRETTY_FUNCTION__ << "begin";
    Q_ASSERT(getDAQState() == DAQState::Idle);
    Q_ASSERT(getMVMEStreamWorkerState() == AnalysisWorkerState::Idle);

    if (getDAQState() != DAQState::Idle
        || getMVMEStreamWorkerState() != AnalysisWorkerState::Idle)
    {
        return false;
    }

    auto currentControllerType = (m_controller
                                  ? m_controller->getType()
                                  : VMEControllerType::MVLC_ETH);

    auto newControllerType = (controller
                              ?  controller->getType()
                              : VMEControllerType::MVLC_ETH);

    qDebug() << __PRETTY_FUNCTION__
        << "current type =" << (m_controller ? to_string(currentControllerType) : QSL("none"))
        << ", new type   =" << (controller ? to_string(newControllerType) : QSL("none"))
        ;

    /* Wait for possibly active VMEController::open() to return before deleting
     * the controller object. This can take a long time if e.g. DNS lookups are
     * performed when trying to open the current controller. This is the reason
     * for using an event loop instead of directly calling
     * m_ctrlOpenFuture.waitForFinished(). */
    //qDebug() << __PRETTY_FUNCTION__ << "before waitForFinished";

    if (m_ctrlOpenFuture.isRunning())
    {
        QProgressDialog progressDialog("Changing VME Controller", QString(), 0, 0);
        progressDialog.setWindowModality(Qt::ApplicationModal);
        progressDialog.setCancelButton(nullptr);
        progressDialog.show();

        QEventLoop localLoop;
        connect(&m_ctrlOpenWatcher, &QFutureWatcher<VMEError>::finished,
                &localLoop, &QEventLoop::quit);
        localLoop.exec();
    }
    //qDebug() << __PRETTY_FUNCTION__ << "after waitForFinished";

    emit vmeControllerAboutToBeChanged();

    //qDebug() << __PRETTY_FUNCTION__ << "after emit vmeControllerAboutToBeChanged";

    // Delete objects inside the event loop via deleteLater() because Qt events
    // may still be scheduled which may make use of the objects.
    m_readoutWorker->deleteLater();
    m_readoutWorker = nullptr;

    m_controller->deleteLater();
    m_controller = controller;

    if (m_vmeConfig->getControllerType() != controller->getType()
        || m_vmeConfig->getControllerSettings() != settings)
    {
        m_vmeConfig->setVMEController(controller->getType(), settings);
    }

    m_d->m_ctrlOpenRetryCount = 0;

    connect(controller, &VMEController::controllerStateChanged,
            this, &MVMEContext::controllerStateChanged);
    connect(controller, &VMEController::controllerStateChanged,
            this, &MVMEContext::onControllerStateChanged);

    // readout worker (readout side)
    VMEControllerFactory factory(controller->getType());
    m_readoutWorker = factory.makeReadoutWorker();
    m_readoutWorker->moveToThread(m_readoutThread);
    connect(m_readoutWorker, &VMEReadoutWorker::stateChanged,
            this, &MVMEContext::onDAQStateChanged);
    connect(m_readoutWorker, &VMEReadoutWorker::daqStopped,
            this, &MVMEContext::onDAQDone);

    VMEReadoutWorkerContext readoutWorkerContext;

    readoutWorkerContext.controller         = controller;
    readoutWorkerContext.daqStats           = {};
    readoutWorkerContext.vmeConfig          = m_vmeConfig;
    readoutWorkerContext.freeBuffers        = &m_freeBuffers;
    readoutWorkerContext.fullBuffers        = &m_fullBuffers;
    readoutWorkerContext.listfileOutputInfo = &m_d->m_listfileOutputInfo;
    readoutWorkerContext.runInfo            = &m_d->m_runInfo;

    readoutWorkerContext.logger             = [this](const QString &msg) { logMessage(msg); };
    readoutWorkerContext.errorLogger        = [this](const QString &msg) { logError(msg); };
    readoutWorkerContext.getLogBuffer       = [this]() { return getLogBuffer(); }; // this is thread-safe
    readoutWorkerContext.getAnalysisJson    = [this]() { return getAnalysisJsonDocument(); }; // this is NOT thread-safe
    readoutWorkerContext.getRunNotes        = [this]() { return getRunNotes(); }; // this is thread-safe

    m_readoutWorker->setContext(readoutWorkerContext);

    if (auto mvlcReadoutWorker = qobject_cast<MVLCReadoutWorker *>(m_readoutWorker))
    {
        connect(mvlcReadoutWorker, &MVLCReadoutWorker::debugInfoReady,
                this, &MVMEContext::sniffedReadoutBufferReady);

        mvlcReadoutWorker->setSnoopQueues(&m_d->mvlcSnoopQueues);
    }

    // replay worker (running on the readout thread)
    m_d->listfileReplayWorker = std::unique_ptr<ListfileReplayWorker>(
        factory.makeReplayWorker(&m_freeBuffers, &m_fullBuffers));

    m_d->listfileReplayWorker->setLogger([this](const QString &msg) { this->logMessage(msg); });
    m_d->listfileReplayWorker->moveToThread(m_readoutThread);

    connect(m_d->listfileReplayWorker.get(), &ListfileReplayWorker::stateChanged,
            this, &MVMEContext::onDAQStateChanged);

    connect(m_d->listfileReplayWorker.get(), &ListfileReplayWorker::replayStopped,
            this, &MVMEContext::onReplayDone);

    if (auto mvlcListfileWorker = qobject_cast<MVLCListfileWorker *>(m_d->listfileReplayWorker.get()))
    {
        mvlcListfileWorker->setSnoopQueues(&m_d->mvlcSnoopQueues);
    }

    // TODO: Add the buffer sniffing connection for the MVLCListfileWorker once
    // the support is there.

    // Create a stream worker (analysis side). The concrete type depends on the
    // VME controller type.
    //
    // Delete the streamWorker in the event loop. This will also delete the
    // EventServer child.
    if (m_streamWorker)
    {
        auto streamWorker = m_streamWorker.release();
        streamWorker->deleteLater(); // Will also delete the current EventServer instance
        m_d->m_eventServer = nullptr;
        processQtEvents();
    }

    switch (controller->getType())
    {
        case VMEControllerType::SIS3153:
        case VMEControllerType::VMUSB:
            m_streamWorker = std::make_unique<MVMEStreamWorker>(
                this, &m_freeBuffers, &m_fullBuffers);
            break;

        case VMEControllerType::MVLC_ETH:
        case VMEControllerType::MVLC_USB:
            m_streamWorker = std::make_unique<MVLC_StreamWorker>(
                this, m_d->mvlcSnoopQueues);
            break;
    }

    assert(m_streamWorker);


    // EventServer setup
    {
        m_d->m_eventServer = new EventServer(m_streamWorker.get());; // Note: this is a non-owning pointer
        m_streamWorker->attachModuleConsumer(m_d->m_eventServer);

        auto eventServer = m_d->m_eventServer;

        eventServer->setLogger([this](const QString &msg) { this->logMessage(msg); });

        auto settings = makeWorkspaceSettings();
        bool enabled = false;
        QHostInfo hostInfo;
        int port = 0;

        if (settings)
            std::tie(enabled, hostInfo, port) = get_event_server_listen_info(*settings);

        if (enabled && (hostInfo.error() || hostInfo.addresses().isEmpty()))
        {
            logMessage(QSL("EventServer error: could not resolve listening address ")
                       + hostInfo.hostName() + ": " + hostInfo.errorString());
        }
        else if (enabled && !hostInfo.addresses().isEmpty())
        {
            eventServer->setListeningInfo(hostInfo.addresses().first(), port);
        }

        bool invoked = QMetaObject::invokeMethod(m_d->m_eventServer,
                                                 "setEnabled",
                                                 Qt::QueuedConnection,
                                                 Q_ARG(bool, enabled));
        assert(invoked);
        (void) invoked;
    }

    // Moves the StreamWorker and its EventServer child
    m_streamWorker->moveToThread(m_analysisThread);

    // Run the StreamWorkerBase::startupConsumers in the worker thread. This is
    // used to e.g. get the EventServer to accept client connections while the
    // system is idle. Starting to accept connections at the point when a run
    // is starting does not work because clients would have no time window to
    // connect to the service.
    // TODO: add a way to wait for completion of the startup.
    {
        bool invoked = QMetaObject::invokeMethod(
            m_streamWorker.get(), "startupConsumers", Qt::QueuedConnection);
        assert(invoked);
    }

    connect(m_streamWorker.get(), &StreamWorkerBase::stateChanged,
            this, &MVMEContext::onAnalysisWorkerStateChanged);

    connect(m_streamWorker.get(), &StreamWorkerBase::sigLogMessage,
            this, &MVMEContext::logMessage);

    emit vmeControllerSet(controller);

    if ((currentControllerType != newControllerType)
        && !(is_mvlc_controller(currentControllerType)
             && is_mvlc_controller(newControllerType)))
    {
        auto predicate = [] (const EventConfig *event)
        {
            return event->triggerCondition == TriggerCondition::Periodic;
        };

        auto events = m_vmeConfig->getEventConfigs();

        if (std::find_if(std::begin(events), std::end(events), predicate) != std::end(events))
        {
            logError("Warning: the timer period for periodic events is reset"
                     " when switching VME controller types. Please re-apply the"
                     " timer period in the Event Settings.");
        }
    }

    //qDebug() << __PRETTY_FUNCTION__ << "end";
    return true;
}

bool MVMEContext::setVMEController(VMEControllerType type, const QVariantMap &settings)
{
    VMEControllerFactory factory(type);

    auto controller = std::unique_ptr<VMEController>(factory.makeController(settings));

    if (setVMEController(controller.get(), settings))
    {
        controller.release();
        return true;
    }

    return false;
}

ControllerState MVMEContext::getControllerState() const
{
    auto result = ControllerState::Disconnected;
    if (m_controller)
        result = m_controller->getState();
    return result;
}

void MVMEContext::onControllerStateChanged(ControllerState state)
{
    qDebug() << __PRETTY_FUNCTION__ << to_string(state) << (u32) state;

    if (state == ControllerState::Connected)
    {
        m_d->m_ctrlOpenRetryCount = 0;
    }
}

// Slot invoked by QFutureWatcher::finished from the m_ctrlOpenWatcher
void MVMEContext::onControllerOpenFinished()
{
    auto result = m_ctrlOpenWatcher.result();

    //qDebug() << __PRETTY_FUNCTION__ << "result =" << result.toString();

    if (!result.isError())
    {
        if (auto vmusb = dynamic_cast<VMUSB *>(m_controller))
        {
            vmusb->readAllRegisters();

            u32 fwReg = vmusb->getFirmwareId();
            u32 fwMajor = (fwReg & 0xFFFF);
            u32 fwMinor = ((fwReg >> 16) &  0xFFFF);


            logMessage(QString("Opened VME controller %1 - Firmware Version %2_%3")
                       .arg(m_controller->getIdentifyingString())
                       .arg(fwMajor, 4, 16, QLatin1Char('0'))
                       .arg(fwMinor, 4, 16, QLatin1Char('0'))
                      );
        }
        else if (auto sis = dynamic_cast<SIS3153 *>(m_controller))
        {
            u32 moduleIdAndFirmware;
            auto error = sis->readRegister(SIS3153Registers::ModuleIdAndFirmware,
                                           &moduleIdAndFirmware);

            if (!error.isError())
            {
                logMessage(QString("Opened VME controller %1 - Firmware 0x%2")
                           .arg(m_controller->getIdentifyingString())
                           .arg(moduleIdAndFirmware & 0xffff, 4, 16, QLatin1Char('0'))
                          );

                QSettings appSettings;
                appSettings.setValue("VME/LastConnectedSIS3153", sis->getAddress());
            }
            else
            {
                logMessage(QString("Error reading firmware from VME controller %1: %2")
                           .arg(m_controller->getIdentifyingString())
                           .arg(error.toString())
                          );
            }
        }
        else if (auto mvlc = dynamic_cast<mesytec::mvme_mvlc::MVLC_VMEController *>(m_controller))
        {
            using namespace mesytec::mvme_mvlc;

            logMessage(QString("Opened VME Controller %1 (%2)")
                       .arg(mvlc->getIdentifyingString())
                       .arg(mvlc->getMVLCObject()->getConnectionInfo()));
        }
        else // generic case
        {
            logMessage(QString("Opened VME controller %1")
                       .arg(m_controller->getIdentifyingString()));
        }

        return;
    }
    else if (result.error() != VMEError::DeviceIsOpen)
    {
        assert(result.isError());

        /* Could not connect. Inc retry count and log a hopefully user
         * friendly message about what went wrong. */

        m_d->m_ctrlOpenRetryCount++;

        // The result indicates an error. First handle the MVLCs "InUse" case.
        if (result.getStdErrorCode() == mesytec::mvlc::MVLCErrorCode::InUse)
        {
            if (m_d->m_ctrlOpenRetryCount >= VMECtrlConnectMaxRetryCount)
            {
                //if (!m_d->m_isFirstConnectionAttempt)
                {
                    auto msg = QSL(
                        "The MVLC controller seems to be in use (at least one of the"
                        " triggers is enabled).\n"
                        "Use the \"Force Reset\" button to disable the triggers on the next"
                        " connection attempt.");

                    logMessage(msg);
                }
            }
        }

        if (m_d->m_ctrlOpenRetryCount >= VMECtrlConnectMaxRetryCount)
        {

            //if (!m_d->m_isFirstConnectionAttempt)
            {
                logMessage(QString("Could not open VME controller %1: %2")
                           .arg(m_controller->getIdentifyingString())
                           .arg(result.toString())
                          );
            }
            m_d->m_isFirstConnectionAttempt = false;
        }
    }
}

void MVMEContext::reconnectVMEController()
{
    if (!m_controller || m_d->m_options.offlineMode)
        return;

    /* VMEController::close() should lock the controllers mutex just as
     * VMEController::open() should. This means if a long lasting open()
     * operation is in progress (e.g. due to DNS lookup) the call will block.
     * Solution: wait for any open() calls to finish, then call close on the
     * controller. */

    qDebug() << __PRETTY_FUNCTION__ << "before m_controller->close()";
    if (m_ctrlOpenFuture.isRunning())
    {
        QProgressDialog progressDialog("Reconnecting to VME Controller", QString(), 0, 0);
        progressDialog.setWindowModality(Qt::ApplicationModal);
        progressDialog.setCancelButton(nullptr);
        progressDialog.show();

        // XXX: Qt under windows warns that this connect likely leads to a
        // race. I think it's ok because between the isRunning() test above and
        // the connect no signal processing can happen because we do not enter
        // an event loop. Even if the future finishes between isRunning() and
        // loop.exec() below the finished() signal will still be queued up and
        // delivered immediately upon entering the loop.
        QEventLoop localLoop;
        connect(&m_ctrlOpenWatcher, &QFutureWatcher<VMEError>::finished,
                &localLoop, &QEventLoop::quit);
        localLoop.exec();
    }

    m_controller->close();
    m_d->m_ctrlOpenRetryCount = 0;
    m_d->m_isFirstConnectionAttempt = true; // FIXME: add a note on why this is done

    qDebug() << __PRETTY_FUNCTION__ << "after m_controller->close()";
}

void MVMEContext::forceResetVMEController()
{
    if (auto sis = qobject_cast<SIS3153 *>(getVMEController()))
    {
        sis->setResetOnConnect(true);
    }
    else if (auto mvlc = qobject_cast<mesytec::mvme_mvlc::MVLC_VMEController *>(getVMEController()))
    {
        if (mvlc->connectionType() == mesytec::mvlc::ConnectionType::ETH)
        {
            auto mvlc_eth = reinterpret_cast<mesytec::mvlc::eth::Impl *>(mvlc->getImpl());
            mvlc_eth->setDisableTriggersOnConnect(true);
        }
    }

    reconnectVMEController();
}

void MVMEContext::dumpVMEControllerRegisters()
{
    // XXX: only VMUSB right now
    if (auto vmusb = qobject_cast<VMUSB *>(getVMEController()))
    {
        if (getDAQState() == DAQState::Idle)
        {
            dump_registers(vmusb, [this] (const QString &line)
                           {
                               logMessage(line);
                           });
        }
    }
    else
    {
        logMessage("'Dump Registers' not implemented for VME controller type " +
                   to_string(getVMEController()->getType()));
    }
}

QString MVMEContext::getUniqueModuleName(const QString &prefix) const
{
    return make_unique_module_name(prefix, m_vmeConfig);
}

void MVMEContext::tryOpenController()
{
    // Mutex to avoid entering and possibly starting the opening code multiple
    // times.
    std::unique_lock<mesytec::mvme::TicketMutex> guard(m_d->tryOpenControllerMutex);

    if (!m_d->m_options.offlineMode
        && m_controller
        && !m_ctrlOpenFuture.isRunning()
        && m_d->m_ctrlOpenRetryCount < VMECtrlConnectMaxRetryCount)
    {
        auto try_open = [] (VMEController *ctrl)
        {
            if (!ctrl->isOpen())
            {
                qDebug() << "tryOpenController" << QThread::currentThread() << "calling open()";
                auto result = ctrl->open();
                qDebug() << "tryOpenController: result from open:" << result.getStdErrorCode().message().c_str();
                return result;
            }

            //qDebug() << "tryOpenController" << QThread::currentThread() << "ctrl is open, returning";
            return VMEError(VMEError::DeviceIsOpen);
        };

        m_ctrlOpenFuture = QtConcurrent::run(try_open, m_controller);
        m_ctrlOpenWatcher.setFuture(m_ctrlOpenFuture);
    }
}

void MVMEContext::logModuleCounters()
{
#if 0 // TODO: rebuild this once stats tracks these numbers again

    QString buffer;
    QTextStream stream(&buffer);

    stream << endl;
    stream << "Buffers: " << m_daqStats.totalBuffersRead << endl;
    stream << "Events:  " << m_daqStats.totalEventsRead << endl;
    stream << "MVME format buffers seen: " << m_daqStats.mvmeBuffersSeen
        << ", errors: " << m_daqStats.mvmeBuffersWithErrors
        << endl;

    const auto &counters = m_daqStats.eventCounters;

    //stream << "Got " << m_daqStats.eventCounters.size() << " event counters" << endl;

    for (auto it = counters.begin();
         it != counters.end();
         ++it)
    {
        auto mod = qobject_cast<ModuleConfig *>(it.key());
        auto counters = it.value();

        if (mod)
        {
            stream << mod->objectName() << endl;
            stream << "  Events:  " << counters.events << endl;
            stream << "  Headers: " << counters.headerWords << endl;
            stream << "  Data:    " << counters.dataWords << endl;
            stream << "  EOE:     " << counters.eoeWords << endl;
            stream << "  avg event size: " << ((float)counters.dataWords / (float)counters.events) << endl;
            stream << "  data/headers: " << ((float)counters.dataWords / (float)counters.headerWords) << endl;
        }
    }

    logMessage(buffer);
#endif
}

void MVMEContext::onDAQStateChanged(DAQState state)
{
    m_daqState = state;
    emit daqStateChanged(state);
    m_d->updateMVMEState();
}

void MVMEContext::onAnalysisWorkerStateChanged(AnalysisWorkerState state)
{
    emit mvmeStreamWorkerStateChanged(state);
    m_d->updateMVMEState();
}

// Called on VMEReadoutWorker::daqStopped()
void MVMEContext::onDAQDone()
{
    assert(m_d->daqRunLogfileLimiter);
    m_d->daqRunLogfileLimiter->closeCurrentFile();

    // stops the analysis side thread
    m_streamWorker->stop();

    // The readout worker might have modified the ListFileOutputInfo structure.
    // Write it out to the workspace.
    qDebug() << __PRETTY_FUNCTION__ << "writing listfile output info to workspace";
    writeToSettings(m_d->m_listfileOutputInfo, *makeWorkspaceSettings());
}

// Called on ListfileReplayWorker::replayStopped()
void MVMEContext::onReplayDone()
{
    // Tell the analysis side to stop once the filled buffer queue is empty.
    m_streamWorker->stop();

    auto stats = getDAQStats();

    qDebug() << __PRETTY_FUNCTION__
        << stats.startTime
        << stats.endTime
        << stats.totalBuffersRead
        << stats.totalBytesRead;

    double secondsElapsed = m_replayTime.elapsed() / 1000.0;
    u64 replayBytes = getDAQStats().totalBytesRead;
    double replayMB = (double)replayBytes / (1024.0 * 1024.0);
    double mbPerSecond = 0.0;
    if (secondsElapsed > 0)
    {
        mbPerSecond = replayMB / secondsElapsed;
    }

    QString msg = QString("Replay finished: Read %1 MB in %2 s, %3 MB/s\n")
        .arg(replayMB)
        .arg(secondsElapsed)
        .arg(mbPerSecond)
        ;

    logMessage(msg);
    qDebug().noquote() << msg;
}

DAQState MVMEContext::getDAQState() const
{
    return m_daqState;
}

AnalysisWorkerState MVMEContext::getMVMEStreamWorkerState() const
{
    if (m_streamWorker)
        return m_streamWorker->getState();

    return AnalysisWorkerState::Idle;
}

MVMEState MVMEContext::getMVMEState() const
{
    return m_d->m_mvmeState.copy();
}

DAQStats MVMEContext::getDAQStats() const
{
    switch (getMode())
    {
        case GlobalMode::DAQ:
            return m_readoutWorker->getDAQStats();

        case GlobalMode::ListFile:
            return m_d->listfileReplayWorker->getStats();
    }

    return {};
}

static void handle_vme_analysis_assignment(
    const VMEConfig *vmeConfig,
    analysis::Analysis *analysis,
    analysis::ui::AnalysisWidget *analysisUi = nullptr)
{
    using namespace analysis;
    using namespace vme_analysis_common;

    auto_assign_vme_modules(vmeConfig, analysis);

    if (analysisUi)
        analysisUi->repopulate();
}

bool MVMEContext::setReplayFileHandle(ListfileReplayHandle handle, OpenListfileOptions options)
{
    using namespace vme_analysis_common;

    if (handle.format == ListfileBufferFormat::MVMELST && !handle.listfile)
        return false;

    if (getDAQState() != DAQState::Idle)
        stopDAQ();

    std::unique_ptr<VMEConfig> vmeConfig;
    std::error_code ec;

    try
    {
        std::tie(vmeConfig, ec) = read_vme_config_from_listfile(handle);

        if (ec)
        {
            QMessageBox::critical(
                nullptr,
                QSL("Error loading VME config"),
                QSL("Error loading VME config from %1: %2")
                .arg(handle.inputFilename)
                .arg(ec.message().c_str()));

            return false;
        }
    }
    catch (const std::runtime_error &e)
    {
        QMessageBox::critical(
            nullptr,
            QSL("Error loading VME config"),
            QSL("Error loading VME config from %1: %2")
            .arg(handle.inputFilename)
            .arg(e.what()));

        return false;
    }

    // save the current run notes to disk if in DAQ mode
    m_d->maybeSaveDAQNotes();

    m_d->m_isFirstConnectionAttempt = true;
    setVMEConfig(vmeConfig.release());

    m_d->listfileReplayHandle = std::move(handle);
    m_d->listfileReplayWorker->setListfile(&m_d->listfileReplayHandle);

    setVMEConfigFilename(QString(), false);
    setRunNotes(m_d->listfileReplayHandle.runNotes);
    setMode(GlobalMode::ListFile);

    // Write the vme config loaded from the listfile to disk and update the
    // LastVMEConfig entry in the workspace ini. This way we'll be able to keep
    // the same vme config on listfile close.
    // Update 210928: When running a DAQ but replaying from listfiles in between
    // to test things this behavior is bad: instead of going back to the
    // previous config used for your DAQ run you now have to reopen that VME
    // config file.
    //if (write_vme_config_to_file(ListfileTempVMEConfigFilename, getVMEConfig()))
    //    makeWorkspaceSettings()->setValue(QSL("LastVMEConfig"), ListfileTempVMEConfigFilename);

    // optionally load the analysis from the listfile
    if (options.loadAnalysis && !m_d->listfileReplayHandle.analysisBlob.isEmpty())
    {
        if (loadAnalysisConfig(m_d->listfileReplayHandle.analysisBlob, QSL("ZIP Archive")))
        {
            if (write_analysis_to_file(ListfileTempAnalysisConfigFilename, getAnalysis()))
                makeWorkspaceSettings()->setValue(QSL("LastAnalysisConfig"), ListfileTempAnalysisConfigFilename);

            setAnalysisConfigFilename(QString(), false);
        }
    }

    handle_vme_analysis_assignment(
        getVMEConfig(), getAnalysis(),
        getAnalysisUi());

    return true;
}

const ListfileReplayHandle &MVMEContext::getReplayFileHandle() const
{
    return m_d->listfileReplayHandle;
}

ListfileReplayHandle &MVMEContext::getReplayFileHandle()
{
    return m_d->listfileReplayHandle;
}

void MVMEContext::closeReplayFileHandle()
{
    if (getMode() != GlobalMode::ListFile)
        return;

    stopDAQ();

    m_d->listfileReplayHandle = {};
    m_d->m_isFirstConnectionAttempt = true;

    // Reload the "Run Notes" from the workspace file
    {
        QFile f(RunNotesFilename);

        if (f.open(QIODevice::ReadOnly))
        {
            setRunNotes(QString::fromLocal8Bit(f.readAll()));
        }
    }

    /* Open the last used VME config in the workspace. Create a new VME config
     * if no previous exists. */

    QString lastVMEConfig = makeWorkspaceSettings()->value(QSL("LastVMEConfig")).toString();

    if (!lastVMEConfig.isEmpty())
    {
        QDir wsDir(getWorkspaceDirectory());
        loadVMEConfig(wsDir.filePath(lastVMEConfig));
        if (lastVMEConfig == ListfileTempVMEConfigFilename)
            setVMEConfigFilename({});
    }
    else
    {
        setVMEConfig(new VMEConfig);
        setVMEConfigFilename(QString());
        setMode(GlobalMode::DAQ);
    }
}

void MVMEContext::setMode(GlobalMode mode)
{
    if (mode != m_mode)
    {
        m_mode = mode;
        emit modeChanged(m_mode);
    }
}

GlobalMode MVMEContext::getMode() const
{
    return m_mode;
}

void MVMEContext::addObject(QObject *object)
{
    qDebug() << __PRETTY_FUNCTION__ << object;
    m_objects.insert(object);
    emit objectAdded(object);
}

void MVMEContext::removeObject(QObject *object, bool doDeleteLater)
{
    if (m_objects.contains(object))
    {
        //qDebug() << __PRETTY_FUNCTION__ << object;
        emit objectAboutToBeRemoved(object);
        m_objects.remove(object);
        if (doDeleteLater)
            object->deleteLater();
    }
}

bool MVMEContext::containsObject(QObject *object)
{
    return m_objects.contains(object);
}

void MVMEContext::addObjectMapping(QObject *key, QObject *value, const QString &category)
{
    //qDebug() << __PRETTY_FUNCTION__ << category << key << "->" << value;
    m_objectMappings[category][key] = value;
    emit objectMappingAdded(key, value, category);
}

QObject *MVMEContext::removeObjectMapping(QObject *key, const QString &category)
{
    if (auto value = m_objectMappings[category].take(key))
    {
        //qDebug() << __PRETTY_FUNCTION__ << category << key << "->" << value;
        emit objectMappingRemoved(key, value, category);
        return value;
    }
    return nullptr;
}

QObject *MVMEContext::getMappedObject(QObject *key, const QString &category) const
{
    return m_objectMappings[category].value(key, nullptr);
}

void MVMEContext::setVMEConfigFilename(QString name, bool updateWorkspace)
{
    if (m_configFileName != name || updateWorkspace)
    {
        m_configFileName = name;
        if (updateWorkspace)
        {
            makeWorkspaceSettings()->setValue(
                QSL("LastVMEConfig"), name.remove(getWorkspaceDirectory() + '/'));
        }
        emit vmeConfigFilenameChanged(name);
    }
}

void MVMEContext::setAnalysisConfigFilename(QString name, bool updateWorkspace)
{
    if (m_analysisConfigFileName != name || updateWorkspace)
    {
        m_analysisConfigFileName = name;
        if (updateWorkspace)
        {
            makeWorkspaceSettings()->setValue(
                QSL("LastAnalysisConfig"), name.remove(getWorkspaceDirectory() + '/'));
        }
        emit analysisConfigFileNameChanged(name);
    }
}

// Rebuilds the analyis in the main thread and then tells the stream worker that
// a run is about to start.
bool MVMEContext::prepareStart()
{
#ifndef NDEBUG
    // Use this to force a crash in case deleted objects remain in the object set.
    for (auto it=m_objects.begin(); it!=m_objects.end(); ++it)
    {
        qDebug() << reinterpret_cast<void *>(*it);
        qDebug() << *it;
    }
#endif

    // add info from the workspace to the current RunInfo object
    auto wsSettings = makeWorkspaceSettings();
    auto experimentName = wsSettings->value("Experiment/Name").toString();
    if (experimentName.isEmpty())
        experimentName = "Experiment";
    m_d->m_runInfo.infoDict["ExperimentName"] = experimentName;
    m_d->m_runInfo.infoDict["ExperimentTitle"] = wsSettings->value("Experiment/Title");
    m_d->m_runInfo.infoDict["MVMEWorkspace"] = getWorkspaceDirectory();
    m_d->m_runInfo.ignoreStartupErrors = wsSettings->value(
        "Experiment/IgnoreVMEStartupErrors").toBool();

    qDebug() << __PRETTY_FUNCTION__
        << "free buffers:" << queue_size(&m_freeBuffers)
        << "filled buffers:" << queue_size(&m_fullBuffers);

    // Discard any filled buffers from a previous run, moving them back to the
    // free queue. This way the analysis side won't get stale data in case it
    // previously quit without consuming all enqueued buffers.
    while (auto buffer = dequeue(&m_fullBuffers))
        enqueue(&m_freeBuffers, buffer);

    assert(queue_size(&m_freeBuffers) == ReadoutBufferCount);
    assert(queue_size(&m_fullBuffers) == 0);

    assert(m_streamWorker->getState() == AnalysisWorkerState::Idle);

    if (m_streamWorker->getState() == AnalysisWorkerState::Idle)
    {
        qDebug() << __PRETTY_FUNCTION__ << "building analysis in main thread";

        auto analysis = getAnalysis();
        analysis->beginRun(getRunInfo(), getVMEConfig(),
                           [this] (const QString &msg) { logMessage(msg); });

        qDebug() << __PRETTY_FUNCTION__ << "starting mvme stream worker";

        // Use a local event loop to wait here until the stream worker thread
        // signals that startup is complete.
        QEventLoop localLoop;
        auto con_started = QObject::connect(
            m_streamWorker.get(), &MVMEStreamWorker::started,
            &localLoop, &QEventLoop::quit);

        auto con_stopped = QObject::connect(
            m_streamWorker.get(), &MVMEStreamWorker::stopped,
            &localLoop, &QEventLoop::quit);

        bool invoked = QMetaObject::invokeMethod(m_streamWorker.get(), "start",
                                                 Qt::QueuedConnection);
        assert(invoked);

        localLoop.exec();

        QObject::disconnect(con_started);
        QObject::disconnect(con_stopped);

        auto workerState = m_streamWorker->getState();

        if (workerState == AnalysisWorkerState::Running
            || workerState == AnalysisWorkerState::Paused)
        {
            qDebug() << __PRETTY_FUNCTION__ << "started mvme stream worker";
            return true;
        }
        else
        {
            qDebug() << __PRETTY_FUNCTION__ << "mvme stream worker did not start up correctly";
            return false;
        }
    }

    return false;
}

void MVMEContext::startDAQReadout(quint32 nCycles, bool keepHistoContents)
{
    Q_ASSERT(getDAQState() == DAQState::Idle);
    Q_ASSERT(getMVMEStreamWorkerState() == AnalysisWorkerState::Idle);
    Q_ASSERT(getMVMEState() == MVMEState::Idle);

    if (m_mode != GlobalMode::DAQ
        || getDAQState() != DAQState::Idle
        || getMVMEStreamWorkerState() != AnalysisWorkerState::Idle
        || getMVMEState() != MVMEState::Idle
       )
    {
        return;
    }

    // Can be used for last minute changes before the actual startup. Used in
    // the GUI to ask the user if modifications to VME scripts should be applied
    // and used for the run.
    emit daqAboutToStart();

    // Generate new RunInfo here. Has to happen before prepareStart() calls
    // MVMEStreamWorker::beginRun()
    // FIXME: does this still do anything? runId is still used in histo1d_widget?
    // FIXME: the output filename generation (based on run settings, etc) must
    // be done before filling this runinfo. Meaning move the output file
    // creation and error handling from the readout worker into this thread and
    // pass the open output file descriptor the readout worker instead.
    auto now = QDateTime::currentDateTime();
    m_d->m_runInfo.runId = now.toString("yyMMdd_HHmmss");
    m_d->m_runInfo.keepAnalysisState = keepHistoContents;
    m_d->m_runInfo.isReplay = false;

    // TODO: use ListFileOutputInfo to generate the runId for the analysis
    // FIXME: we don't actually know the runId before the listfile is successfully opened.
    // E.g. the runNumber might be incremented due to run files already existing.

    m_d->clearLog();

    assert(m_d->daqRunLogfileLimiter);

    m_d->daqRunLogfileLimiter->beginNewFile(m_d->m_runInfo.runId);

    // Log mvme version and bitness and runtime cpu architecture
    logMessage(QString(QSL("mvme %1 (%2) running on %3 (%4)\n"))
               .arg(GIT_VERSION)
               .arg(get_bitness_string())
               .arg(QSysInfo::prettyProductName())
               .arg(QSysInfo::currentCpuArchitecture()));

    logMessage(QSL("DAQ starting on %1")
               .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));


    if (!prepareStart())
    {
        logMessage("Failed to start stream worker (analysis side). Aborting startup.");
        return;
    }

    qDebug() << __PRETTY_FUNCTION__ << "starting readout worker";

    bool invoked = QMetaObject::invokeMethod(
        m_readoutWorker, "start", Qt::QueuedConnection,
        Q_ARG(quint32, nCycles));
    assert(invoked);
}

void MVMEContext::stopDAQ()
{
    m_d->stopDAQ();
}

void MVMEContext::pauseDAQ()
{
    m_d->pauseDAQ();
}

void MVMEContext::resumeDAQ(u32 nCycles)
{
    m_d->resumeDAQ(nCycles);
}

void MVMEContext::startDAQReplay(quint32 nEvents, bool keepHistoContents)
{
#if 0
    Q_ASSERT(getDAQState() == DAQState::Idle);
    Q_ASSERT(getMVMEStreamWorkerState() == AnalysisWorkerState::Idle);
#endif

    if (m_mode != GlobalMode::ListFile
        || getDAQState() != DAQState::Idle
        || getMVMEStreamWorkerState() != AnalysisWorkerState::Idle
        || getMVMEState() != MVMEState::Idle)
    {
        return;
    }

    if (m_d->listfileReplayHandle.format == ListfileBufferFormat::MVMELST
        && !m_d->listfileReplayHandle.listfile)
    {
        return;
    }

    // Extract a runId from the listfile filename.
    QFileInfo fi(m_d->listfileReplayHandle.inputFilename);
    m_d->m_runInfo.runId = fi.completeBaseName();
    m_d->m_runInfo.keepAnalysisState = keepHistoContents;
    m_d->m_runInfo.isReplay = true;
    m_d->m_runInfo.infoDict["replaySourceFile"] = fi.fileName();

    m_d->listfileReplayWorker->setEventsToRead(nEvents);

    if (auto mvmeStreamWorker = qobject_cast<MVMEStreamWorker *>(m_streamWorker.get()))
    {
        ListFile lf(m_d->listfileReplayHandle.listfile.get());
        lf.open();
        mvmeStreamWorker->setListFileVersion(lf.getFileVersion());
    }

    if (!prepareStart())
    {
        logMessage("Failed to start stream worker (analysis side). Aborting startup");
        return;
    }

    qDebug() << __PRETTY_FUNCTION__ << "starting listfile reader";

    bool invoked = QMetaObject::invokeMethod(
        m_d->listfileReplayWorker.get(), "start", Qt::QueuedConnection);
    assert(invoked);

    m_replayTime.restart();
}

void MVMEContext::addObjectWidget(QWidget *widget, QObject *object, const QString &stateKey)
{
    if (m_mainwin)
    {
        m_mainwin->addObjectWidget(widget, object, stateKey);
    }
}

bool MVMEContext::hasObjectWidget(QObject *object) const
{
    bool result = false;

    if (m_mainwin)
    {
        result = m_mainwin->hasObjectWidget(object);
    }

    return result;
}

QWidget *MVMEContext::getObjectWidget(QObject *object) const
{
    QWidget *result = nullptr;

    if (m_mainwin)
    {
        result = m_mainwin->getObjectWidget(object);
    }

    return result;
}

QList<QWidget *> MVMEContext::getObjectWidgets(QObject *object) const
{
    QList<QWidget *> result;

    if (m_mainwin)
    {
        result = m_mainwin->getObjectWidgets(object);
    }

    return result;
}

void MVMEContext::activateObjectWidget(QObject *object)
{
    if (m_mainwin)
    {
        m_mainwin->activateObjectWidget(object);
    }
}

void MVMEContext::addWidget(QWidget *widget, const QString &stateKey)
{
    if (m_mainwin)
    {
        m_mainwin->addWidget(widget, stateKey);
    }
}

void MVMEContext::logMessageRaw(const QString &msg)
{
    QMutexLocker lock(&m_d->m_logBufferMutex);

    if (m_d->m_logBuffer.size() >= LogBufferMaxEntries)
        m_d->m_logBuffer.pop_front();

    m_d->m_logBuffer.append(msg);
    emit sigLogMessage(msg);
}

void MVMEContext::logMessage(const QString &msg)
{
    QString fullMessage(QString("%1: %2")
             .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
             .arg(msg));

    logMessageRaw(fullMessage);
}

void MVMEContext::logError(const QString &msg)
{
    QString fullMessage(QString("%1: %2")
             .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
             .arg(msg));

    {
        QMutexLocker lock(&m_d->m_logBufferMutex);

        if (m_d->m_logBuffer.size() >= LogBufferMaxEntries)
            m_d->m_logBuffer.pop_front();

        m_d->m_logBuffer.append(msg);
    }

    emit sigLogError(fullMessage);
}

QStringList MVMEContext::getLogBuffer() const
{
    QMutexLocker lock(&m_d->m_logBufferMutex);
    return m_d->m_logBuffer;
}

void MVMEContext::onEventAdded(EventConfig *event)
{
    emit eventAdded(event);
    for (auto module: event->getModuleConfigs())
        onModuleAdded(module);

    connect(event, &EventConfig::moduleAdded, this, &MVMEContext::onModuleAdded);
    connect(event, &EventConfig::moduleAboutToBeRemoved, this, &MVMEContext::onModuleAboutToBeRemoved);
}

void MVMEContext::onEventAboutToBeRemoved(EventConfig *config)
{
    for (auto module: config->getModuleConfigs())
    {
        onModuleAboutToBeRemoved(module);
        emit objectAboutToBeRemoved(module);
    }

    for (auto key: config->vmeScripts.keys())
        emit objectAboutToBeRemoved(config->vmeScripts[key]);

    emit objectAboutToBeRemoved(config);
    emit eventAboutToBeRemoved(config);
}

void MVMEContext::onGlobalChildAboutToBeRemoved(ConfigObject *config)
{
    emit objectAboutToBeRemoved(config);
}

void MVMEContext::onModuleAdded(ModuleConfig *module)
{
    //qDebug() << __PRETTY_FUNCTION__ << module;
    if (auto analysis = getAnalysis())
        vme_analysis_common::update_analysis_vme_properties(getVMEConfig(), analysis);
    emit moduleAdded(module);
}

void MVMEContext::onModuleAboutToBeRemoved(ModuleConfig *module)
{
    auto vmeScripts = module->findChildren<VMEScriptConfig *>();
    for (auto script: vmeScripts)
    {
        emit objectAboutToBeRemoved(script);
    }

    emit moduleAboutToBeRemoved(module);
}

#if 1
vme_script::ResultList
MVMEContext::runScript(
    const vme_script::VMEScript &script,
    vme_script::LoggerFun logger,
    bool logEachResult)
{
    vme_script::ResultList results;

    if (!m_controller->isOpen())
    {
        logger("VME Script Error: VME controller not connected");
        vme_script::Result result = {};
        result.error = VMEError(VMEError::NotOpen);
        return { result };
    }

    // The MVLC can execute commands while the DAQ is running, other controllers
    // cannot so the DAQ has to be paused and resumed if needed.
    if (is_mvlc_controller(m_controller->getType()))
    {
        auto mvlc = qobject_cast<mesytec::mvme_mvlc::MVLC_VMEController *>(m_controller);
        assert(mvlc);

        // The below code should be equivalent to
        // results = vme_script::run_script(m_controller, script, logger, logEachResult);
        // but moves the run_script call into a different thread and shows a
        // progress dialog during execution.
        //
        // IMPORTANT: if logEachResult is true, then the logger will be invoked
        // from within the thread chosen by QtConcurrent::run(). This means
        // direct modification of GUI elements from within the logger code is
        // not allowed and will most likely lead to a crash!  A workaround is
        // to emit a signal from within the logger (via a lambda for example)
        // to deliver the message to the destination. Then the usual Qt
        // signal/slot thread handling will apply and the slot will be invoked
        // in the destinations thread.

        using Watcher = QFutureWatcher<vme_script::ResultList>;

        QProgressDialog pd;
        pd.setLabelText("VME Script execution in progress");
        pd.setMaximum(0);
        pd.setCancelButton(nullptr);

        Watcher fw;

        connect(&fw, &Watcher::finished, &pd, &QProgressDialog::accept);


        auto f = QtConcurrent::run(
            [=] () -> vme_script::ResultList {
                auto result = vme_script::run_script(
                    m_controller, script, logger,
                    logEachResult ? vme_script::run_script_options::LogEachResult : 0u);
                return result;
            });

        // Note: this call can lead to immediate signal emission from the
        // watcher.
        fw.setFuture(f);

        // No qt signal processing can be done between f.isFinished() and
        // pd.exec() so this should be fine. Either the future is finished and
        // we never enter the dialog event loop or we do enter the loop and
        // will get a finished signal from the Watcher.
        if (!f.isFinished())
            pd.exec();

        results = f.result();
    }
    else
    {
        DAQPauser pauser(this);

        // TODO: move this into a separate thread like in the MVLC case above.
        results = vme_script::run_script(m_controller, script, logger, logEachResult);
    }

    // Check for errors indicating connection loss and call close() on the VME
    // controller to update its status.
    //static const unsigned TimeoutConnectionLossThreshold = 3;
    //unsigned timeouts = 0;

    for (const auto &result: results)
    {
#if 0
        qDebug() << __PRETTY_FUNCTION__ << result.error.isError()
            << result.error.error()
            << result.error.getStdErrorCode().value()
            << result.error.getStdErrorCode().category().name()
            << (result.error.getStdErrorCode() == mesytec::mvme_mvlc::ErrorType::ConnectionError);
#endif

        if (result.error.isError())
        {
            if (result.error.error() == VMEError::NotOpen ||
                result.error.getStdErrorCode() == mesytec::mvlc::ErrorType::ConnectionError)
            {
                emit logMessage("ConnectionError during VME script execution,"
                                " closing connection to VME Controller");
                m_controller->close();
                break;
            }
        }
    }

    return results;
}
#endif

//
// Workspace handling
//

void make_empty_file(const QString &filePath)
{
    QFile file(filePath);

    if (file.exists())
        throw QString(QSL("File exists"));

    if (!file.open(QIODevice::WriteOnly))
        throw file.errorString();
}

void MVMEContext::newWorkspace(const QString &dirName)
{
    QDir destDir(dirName);

    // If the INI file exists assume this is a proper workspace and open it.
    if (destDir.exists(WorkspaceIniName))
    {
        openWorkspace(dirName);
        return;
    }

    // cleanup autosaves in the previous (currently open) workspace
    cleanupWorkspaceAutoSaveFiles();

    auto workspaceSettings(makeWorkspaceSettings(dirName));
    workspaceSettings->setValue(QSL("WriteListFile"), true);

    workspaceSettings->setValue(QSL("Experiment/Name"), "Experiment");
    workspaceSettings->setValue(QSL("Experiment/Title"), QString());

    workspaceSettings->setValue(QSL("JSON-RPC/Enabled"), false);
    workspaceSettings->setValue(QSL("JSON-RPC/ListenAddress"), QString());
    workspaceSettings->setValue(QSL("JSON-RPC/ListenPort"), JSON_RPC_DefaultListenPort);

    workspaceSettings->setValue(QSL("EventServer/Enabled"), false);
    workspaceSettings->setValue(QSL("EventServer/ListenAddress"), QString());
    workspaceSettings->setValue(QSL("EventServer/ListenPort"), EventServer_DefaultListenPort);


    // Force sync to create the mvmeworkspace.ini file
    workspaceSettings->sync();

    if (workspaceSettings->status() != QSettings::NoError)
    {
        throw QString("Error writing workspace settings to %1")
            .arg(workspaceSettings->fileName());
    }

    if (!destDir.exists(RunNotesFilename))
    {
        QFile inFile(":/default_run_notes.txt");

        if (!inFile.open(QIODevice::ReadOnly))
            throw QString("Error reading default_run_notes.txt from resource file.");

        QFile outFile(QDir(dirName).filePath(RunNotesFilename));

        if (!outFile.open(QIODevice::WriteOnly))
            throw QString(QSL("Error saving default DAQ run notes to file: %1").arg(outFile.errorString()));

        auto defRunNotes = inFile.readAll();
        auto written = outFile.write(defRunNotes);

        if (written != defRunNotes.size())
            throw QString(QSL("Error saving default DAQ run notes to file: %1").arg(outFile.errorString()));
    }

    openWorkspace(dirName);
}

void MVMEContext::openWorkspace(const QString &dirName)
{

    QString lastWorkspaceDirectory(m_workspaceDir);

    try
    {
        m_d->workspaceClosingCleanup();

        QDir dir(dirName);

        if (!dir.exists())
        {
            throw QString ("Workspace directory %1 does not exist.")
                .arg(dirName);
        }

        if (!dir.exists(WorkspaceIniName))
        {
            throw QString("Workspace settings file %1 not found in %2.")
                .arg(WorkspaceIniName)
                .arg(dirName);
        }

        if (!QDir::setCurrent(dirName))
        {
            throw QString("Could not change directory to workspace path %1.")
                .arg(dirName);
        }

        setWorkspaceDirectory(dirName);
        auto workspaceSettings(makeWorkspaceSettings(dirName));

        auto set_default = [&workspaceSettings](const QString &key, const auto &value)
        {
            if (!workspaceSettings->contains(key))
                workspaceSettings->setValue(key, value);
        };

        // settings defaults
        set_default(QSL("JSON-RPC/Enabled"), false);
        set_default(QSL("JSON-RPC/ListenAddress"), QString());
        set_default(QSL("JSON-RPC/ListenPort"), JSON_RPC_DefaultListenPort);
        set_default(QSL("EventServer/Enabled"), false);
        set_default(QSL("EventServer/ListenAddress"), QString());
        set_default(QSL("EventServer/ListenPort"), EventServer_DefaultListenPort);
        set_default(QSL("Logs/RunLogsMaxCount"), Default_RunLogsMaxCount);

        // listfile subdir
        {
            auto path = getWorkspacePath(QSL("ListFileDirectory"), QSL("listfiles"));
            logMessage(QString("Workspace ListFileDirectory=%1").arg(path));
            QDir dir(path);

            if (!QDir::root().mkpath(dir.absolutePath()))
            {
                throw QString(QSL("Error creating listfiles directory %1.")).arg(dir.path());
            }
        }

        // Creates non-existent workspace subdirectories. pathSettingsKey is
        // used to lookup the directory name from the workspace settings INI
        // file, defaultDirectoryName is the default name for the directory if
        // the setting is missing from the INI. Missing entries will be updated
        // in the INI file.
        auto make_missing_workspace_dir = [this] (
            const QString &pathSettingsKey, const QString &defaultDirectoryName)
        {
            QDir dir(getWorkspacePath(pathSettingsKey, defaultDirectoryName));

            if (!QDir::root().mkpath(dir.absolutePath()))
            {
                throw QString(QSL("Error creating %1 '%2'")
                              .arg(pathSettingsKey).arg(dir.absolutePath()));
            }
        };

        // Contains exported plots (PDF or images files)
        make_missing_workspace_dir(QSL("PlotsDirectory"), QSL("plots"));
        // Contains analysis session files.
        make_missing_workspace_dir(QSL("SessionDirectory"), QSL("sessions"));
        // Contains analysis ExportSink data
        make_missing_workspace_dir(QSL("ExportsDirectory"), QSL("exports"));
        // Holds logfiles of the last DAQ runs (also for unsuccessful starts)
        make_missing_workspace_dir(QSL("RunLogsDirectory"), RunLogsWorkspaceDirectory);
        // Holds mvme.log and mvme_last.log: log files rotated at mvme startup.
        // mvme.log is kept open during the application lifetime and contains
        // all logged messages.
        make_missing_workspace_dir(QSL("LogsDirectory"), LogsWorkspaceDirectory);

        // special listfile output directory handling.
        // FIXME: this might not actually be needed anymore
        {
            ListFileOutputInfo info = readFromSettings(*workspaceSettings);


            QDir listFileOutputDir(info.directory);

            if (listFileOutputDir.isAbsolute() && !listFileOutputDir.exists())
            {
                /* A non-existant absolute path was loaded from the INI -> go back
                 * to the default of "listfiles". */
                logMessage(QString("Warning: Stored Listfile directory %1 does not exist. "
                                   "Reverting back to default of \"listfiles\".")
                           .arg(info.directory));
                info.directory = QSL("listfiles");

                // create the default listfile directory if it does not exist
                listFileOutputDir = QDir(info.directory);
                if (!listFileOutputDir.exists())
                {
                    if (!QDir::root().mkpath(dir.absolutePath()))
                    {
                        throw QString(QSL("Error creating listfiles directory %1.")).arg(dir.path());
                    }
                }
            }

            info.fullDirectory = getListFileOutputDirectoryFullPath(info.directory);
            logMessage(QString("Setting ListFileOutputInfo.fullDirectory=%1").arg(info.fullDirectory));
            m_d->m_listfileOutputInfo = info;
            writeToSettings(info, *workspaceSettings);
        }

        // Create a new LastlogHelper to log into mvme.log until another
        // workspace is opened.
        // Do this before attempting to open vme and analysis files inside the
        // newly entered workspace so that messages end up in the correct log
        // file.
        m_d->lastLogfileHelper = nullptr; // Destroy the old instance to close the log file.
        m_d->lastLogfileHelper = std::make_unique<LastlogHelper>(
            QDir(getWorkspaceDirectory()).filePath(LogsWorkspaceDirectory),
            QSL("mvme.log"), QSL("last_mvme.log"));

        connect(this, &MVMEContext::sigLogMessage,
                this, [this] (const QString &msg)
                {
                    m_d->lastLogfileHelper->logMessage(msg + QSL("\n"));
                });

        connect(this, &MVMEContext::sigLogError,
                this, [this] (const QString &msg)
                {
                    m_d->lastLogfileHelper->logMessage(QSL("EE ") + msg + QSL("\n"));
                });


        //
        // VME config
        //
        auto lastVMEConfig = workspaceSettings->value(QSL("LastVMEConfig")).toString();

        if (dir.exists(VMEConfigAutoSaveFilename))
        {
            qDebug() << __PRETTY_FUNCTION__ << "found VMEConfig autosave";

            auto tLast = QFileInfo(lastVMEConfig).lastModified();
            auto tAuto = QFileInfo(dir.filePath(VMEConfigAutoSaveFilename)).lastModified();

            if (tLast < tAuto)
            {
                QMessageBox mb(
                    QMessageBox::Question, QSL("VME autosave file found"),
                    QSL("A VME config autosave file from a previous mvme session"
                        " was found in %1.<br>"
                        "Do you want to open the autosave?")
                    .arg(dirName),
                    QMessageBox::Open | QMessageBox::Cancel);

                mb.button(QMessageBox::Cancel)->setText(QSL("Ignore"));

                int choice = mb.exec();

                switch (choice)
                {
                    case QMessageBox::Open:
                        loadVMEConfig(dir.filePath(VMEConfigAutoSaveFilename));
                        getVMEConfig()->setModified(true);
                        setVMEConfigFilename(lastVMEConfig);
                        break;

                    case QMessageBox::Cancel:
                        loadVMEConfig(dir.filePath(lastVMEConfig));
                        break;

                    InvalidDefaultCase;
                }
            }
        }
        else
        {
            // Load the last used vme config
            if (!lastVMEConfig.isEmpty())
            {
                qDebug() << __PRETTY_FUNCTION__ << "loading vme config" << lastVMEConfig
                    << " (filename from mvmeworkspace.ini)";

                loadVMEConfig(dir.filePath(lastVMEConfig));

                // If it's the special temporary name for configs from the
                // listfile set the filename to an empty string. This way the
                // user has to pick a new filename when saving.
                if (lastVMEConfig == ListfileTempVMEConfigFilename)
                    setVMEConfigFilename({}, false);
            }
            else
            {
                // No VME config to load in the newly opened workspace. Create
                // a new one and set an empty filename.
                setVMEConfig(new VMEConfig(this));
                setVMEConfigFilename({}, false);
                getVMEConfig()->setModified(false);
            }
        }

        //
        // Analysis config
        //
        auto lastAnalysisConfig = workspaceSettings->value(QSL("LastAnalysisConfig")).toString();

        if (dir.exists(AnalysisAutoSaveFilename))
        {
            qDebug() << __PRETTY_FUNCTION__ << "found Analysis autosave";

            auto tLast = QFileInfo(lastAnalysisConfig).lastModified();
            auto tAuto = QFileInfo(dir.filePath(AnalysisAutoSaveFilename)).lastModified();


            if (tLast < tAuto)
            {
                QMessageBox mb(
                    QMessageBox::Question, QSL("Analysis autosave file found"),
                    QSL("An Analysis autosave file from a previous mvme session"
                        " was found in %1.<br>"
                        "Do you want to open the autosave?")
                    .arg(dirName),
                    QMessageBox::Open | QMessageBox::Cancel);

                mb.button(QMessageBox::Cancel)->setText(QSL("Ignore"));

                int choice = mb.exec();

                switch (choice)
                {
                    case QMessageBox::Open:
                        loadAnalysisConfig(dir.filePath(AnalysisAutoSaveFilename));
                        getAnalysis()->setModified(true);
                        setAnalysisConfigFilename(lastAnalysisConfig);
                        break;

                    case QMessageBox::Cancel:
                        loadAnalysisConfig(dir.filePath(lastAnalysisConfig));
                        break;

                    InvalidDefaultCase;
                }
            }
        }
        else
        {
            if (!lastAnalysisConfig.isEmpty())
            {
                qDebug() << __PRETTY_FUNCTION__ << "loading analysis config" <<
                    lastAnalysisConfig << " (filename from mvmeworkspace.ini)";

                bool couldLoad = loadAnalysisConfig(dir.filePath(lastAnalysisConfig));

                if (!couldLoad || lastAnalysisConfig == ListfileTempAnalysisConfigFilename)
                    setAnalysisConfigFilename({}, false);
            }
            else
            {
                // No analysis config to load in the newly opened workspace.
                // Create an new one and set a empty filename.
                auto tmpAnaJson = serialize_analysis_to_json_document(analysis::Analysis());
                bool couldLoad = loadAnalysisConfig(tmpAnaJson, "<temp_mem>");
                (void) couldLoad;
                assert(couldLoad);
                setAnalysisConfigFilename({}, false);
            }

            // No exceptions thrown -> store workspace directory in global settings
            QSettings settings;
            settings.setValue(QSL("LastWorkspaceDirectory"), getWorkspaceDirectory());

            //
            // Load analysis session auto save
            //

            /* Try to load an analysis session auto save. Only loads analysis data, not
             * the analysis itself from the file.
             * Does not have an effect if there's a mismatch between the current analysis
             * and the one stored in the session as operator ids will be different so no
             * data will be loaded.
             * NOTE: the session auto save is done at the end of
             * MVMEStreamWorker::start().
             */
            auto sessionPath = getWorkspacePath(QSL("SessionDirectory"));
            QFileInfo fi(sessionPath + "/last_session" + analysis::SessionFileExtension);

            if (fi.exists())
            {
                //logMessage(QString("Loading analysis session auto save %1").arg(fi.filePath()));
                load_analysis_session(fi.filePath(), getAnalysis());
            }
        }

        //
        // Run Notes
        //
        {
            QFile f(RunNotesFilename);

            if (f.open(QIODevice::ReadOnly))
                setRunNotes(QString::fromLocal8Bit(f.readAll()));
            else
                setRunNotes({});
        }

        //
        // Create the autosavers here as the workspace specific autosave
        // directory is known at this point.
        //

        // vme
        m_d->m_vmeConfigAutoSaver = std::make_unique<FileAutoSaver>(
            VMEConfigSerializer(this),
            dir.filePath(VMEConfigAutoSaveFilename),
            DefaultConfigFileAutosaveInterval_ms);

        m_d->m_vmeConfigAutoSaver->setObjectName(QSL("VmeConfigAutoSaver"));
        m_d->m_vmeConfigAutoSaver->start();

#if 0
        connect(m_d->m_vmeConfigAutoSaver.get(), &FileAutoSaver::writeError,
                this, [this] (const QString &/*filename*/, const QString &errorMessage) {
            logMessage(errorMessage);
        });
#endif

        // analysis
        m_d->m_analysisAutoSaver = std::make_unique<FileAutoSaver>(
            AnalysisSerializer(this),
            dir.filePath(AnalysisAutoSaveFilename),
            DefaultConfigFileAutosaveInterval_ms);

        m_d->m_analysisAutoSaver->setObjectName(QSL("AnalysisAutoSaver"));
        m_d->m_analysisAutoSaver->start();

#if 0
        connect(m_d->m_analysisAutoSaver.get(), &FileAutoSaver::writeError,
                this, [this] (const QString &/*filename*/, const QString &errorMessage) {
            logMessage(errorMessage);
        });
#endif

        // Create a new LogfileCountLimiter instance for the DAQ run logs in this
        // workspace.
        m_d->daqRunLogfileLimiter = std::make_unique<mesytec::mvme::LogfileCountLimiter>(
            QDir(getWorkspaceDirectory()).filePath(RunLogsWorkspaceDirectory),
            workspaceSettings->value(QSL("Logs/RunLogsMaxCount")).toUInt()
            );

        // The connections can be made immediately. As long as beginNewFile()
        // has not been called on the LogfileCountLimiter, the calls to logMessage()
        // will have no effect.
        connect(this, &MVMEContext::sigLogMessage,
                this, [this] (const QString &msg)
                {
                    m_d->daqRunLogfileLimiter->logMessage(msg + QSL("\n"));
                });

        connect(this, &MVMEContext::sigLogError,
                this, [this] (const QString &msg)
                {
                    m_d->daqRunLogfileLimiter->logMessage(QSL("EE ") + msg + QSL("\n"));
                });

        reapplyWorkspaceSettings();
    }
    catch (...)
    {
        // Restore previous workspace directory as the load was not successful
        // FIXME: other actions should be done here like recreating the
        // lastLogfileHelper. Just restoring the old workspace directory is not
        // enough.
        setWorkspaceDirectory(lastWorkspaceDirectory);
        throw;
    }
}

void MVMEContextPrivate::maybeSaveDAQNotes()
{
    if (!m_q->isWorkspaceOpen())
        return;

    // flush run notes to disk
    if (m_q->getMode() == GlobalMode::DAQ)
    {
        QFile f(RunNotesFilename);

        if (!f.open(QIODevice::WriteOnly))
            throw QString(QSL("Error saving DAQ run notes to file: %1").arg(f.errorString()));

        auto bytes = m_q->getRunNotes().toLocal8Bit();
        auto written = f.write(bytes);

        if (written != bytes.size())
            throw QString(QSL("Error saving DAQ run notes to file: %1").arg(f.errorString()));
    }
}

void MVMEContextPrivate::workspaceClosingCleanup()
{
    if (!m_q->isWorkspaceOpen())
        return;

    // cleanup files in the workspace that's being closed
    m_q->cleanupWorkspaceAutoSaveFiles();
    maybeSaveDAQNotes();
    m_q->setRunNotes({});

#if 0
    if (m_q->getMode() == GlobalMode::ListFile
        && m_q->m_configFileName.isEmpty()
        && !m_q->m_analysisConfigFileName.isEmpty())
    {
        // Detected the following:
        // - daq mode and vme and analysis loaded from listfile
        // - analysis was saved to disk (it has a filename)
        // - vme config was not saved to disk (no filename)
        // -> On next start the vme and analysis configs would not match. Users
        // will likely be confused as to what's happening.
        // - Solution:
        // Create a save file containing the vme config loaded from the listfile.
        // Set it to be the new auto load vme config in the mvmeworkspace.ini.
        QFile outFile(ListfileVMEConfigFilename);

        if (outFile.open(QIODevice::WriteOnly))
        {
            auto vmeConfig = m_q->getVMEConfig();
            if (mvme::vme_config::serialize_vme_config_to_device(outFile, *vmeConfig))
                m_q->makeWorkspaceSettings()->setValue(QSL("LastVMEConfig"), ListfileVMEConfigFilename);
        }
    }
#endif
}

void MVMEContext::cleanupWorkspaceAutoSaveFiles()
{
    if (isWorkspaceOpen())
    {
        qDebug() << __PRETTY_FUNCTION__ << "removing autosaves";

        QDir wsDir(getWorkspaceDirectory());
        QFile::remove(wsDir.filePath(VMEConfigAutoSaveFilename));
        QFile::remove(wsDir.filePath(AnalysisAutoSaveFilename));
    }
    else
    {
        qDebug() << __PRETTY_FUNCTION__ << "no workspace open, nothing to do";
    }
}

void MVMEContext::setWorkspaceDirectory(const QString &dirName)
{
    if (m_workspaceDir != dirName)
    {
        m_workspaceDir = dirName;
        emit workspaceDirectoryChanged(dirName);
    }
}

std::shared_ptr<QSettings> MVMEContext::makeWorkspaceSettings() const
{
    return makeWorkspaceSettings(getWorkspaceDirectory());
}

std::shared_ptr<QSettings> MVMEContext::makeWorkspaceSettings(const QString &workspaceDirectory) const
{
    return make_workspace_settings(workspaceDirectory);
}

QString MVMEContext::getWorkspacePath(const QString &settingsKey,
                                      const QString &defaultValue,
                                      bool setIfDefaulted) const
{
    auto settings = makeWorkspaceSettings();

    if (!settings)
    {
        return QSL("");
    }

    qDebug() << __PRETTY_FUNCTION__ << QSL("settings->contains(%1) = %2")
        .arg(settingsKey).arg(settings->contains(settingsKey));

    qDebug() << __PRETTY_FUNCTION__ << QSL("settings->value(%1) = %2")
        .arg(settingsKey).arg(settings->value(settingsKey).toString());

    if (!settings->contains(settingsKey) && setIfDefaulted)
    {
        settings->setValue(settingsKey, defaultValue);
    }

    QString settingsValue(settings->value(settingsKey, defaultValue).toString());
    QDir dir(settingsValue);

    if (dir.isAbsolute())
    {
        return dir.path();
    }

    return QDir(getWorkspaceDirectory()).filePath(settingsValue);
}

void MVMEContext::reapplyWorkspaceSettings()
{
    qDebug() << __PRETTY_FUNCTION__;

    auto settings = makeWorkspaceSettings();

    m_d->m_remoteControl->stop();

    if (settings->value(QSL("JSON-RPC/Enabled")).toBool())
    {
        m_d->m_remoteControl->setListenAddress(
            settings->value(QSL("JSON-RPC/ListenAddress")).toString());

        m_d->m_remoteControl->setListenPort(
            settings->value(QSL("JSON-RPC/ListenPort")).toInt());

        m_d->m_remoteControl->start();
    }

    // EventServer
    {
        bool enabled;
        QHostInfo hostInfo;
        int port;

        std::tie(enabled, hostInfo, port) = get_event_server_listen_info(*settings);

        if (enabled && (hostInfo.error() || hostInfo.addresses().isEmpty()))
        {
            logMessage(QSL("EventServer error: could not resolve listening address ")
                       + hostInfo.hostName() + ": " + hostInfo.errorString());
        }
        else if (enabled && !hostInfo.addresses().isEmpty())
        {
            m_d->m_eventServer->setListeningInfo(hostInfo.addresses().first(), port);
        }

        bool invoked = QMetaObject::invokeMethod(m_d->m_eventServer,
                                                 "setEnabled",
                                                 Qt::QueuedConnection,
                                                 Q_ARG(bool, enabled));
        assert(invoked);
        (void) invoked;
    }
}

void MVMEContext::loadVMEConfig(const QString &fileName)
{
    std::unique_ptr<VMEConfig> vmeConfig;
    QString errString;

    logMessage(QSL("Loading VME Config from '%1'").arg(fileName));

    auto logger = [this] (const QString &msg)
    {
        this->logMessage("VME config load: " + msg);
    };

    std::tie(vmeConfig, errString) = read_vme_config_from_file(fileName, logger);

    if (!errString.isEmpty())
    {
        QMessageBox::critical(nullptr,
                              QSL("Error loading VME config"),
                              QSL("Error loading VME config from file %1: %2")
                              .arg(fileName)
                              .arg(errString));
        return;
    }

    setVMEConfig(vmeConfig.release());
    setVMEConfigFilename(fileName);
    setMode(GlobalMode::DAQ);

    if (m_d->m_vmeConfigAutoSaver)
    {
        // Config load was successful. If an autosave exists it will contain the
        // (now obsolete) contents of the previous vme config so we remove it
        // and then restart the autosaver.
        m_d->m_vmeConfigAutoSaver->removeOutputFile();
        m_d->m_vmeConfigAutoSaver->start();
    }
}

void MVMEContext::vmeConfigWasSaved()
{
    if (m_d->m_vmeConfigAutoSaver)
    {
        // (re)start the autosaver
        m_d->m_vmeConfigAutoSaver->start();
    }
}

bool MVMEContext::loadAnalysisConfig(const QString &fileName)
{
    qDebug() << "loadAnalysisConfig from" << fileName;

    QJsonDocument doc(gui_read_json_file(fileName));

    if (doc.isNull())
        return false;

    if (loadAnalysisConfig(doc, QFileInfo(fileName).fileName()))
    {
        setAnalysisConfigFilename(fileName);
        return true;
    }

    return false;
}

bool MVMEContext::loadAnalysisConfig(QIODevice *input, const QString &inputInfo)
{
    QJsonDocument doc(gui_read_json(input));

    if (doc.isNull())
        return false;

    if (loadAnalysisConfig(doc, inputInfo))
    {
        setAnalysisConfigFilename(QString());
        return true;
    }

    return false;
}

bool MVMEContext::loadAnalysisConfig(const QByteArray &blob, const QString &inputInfo)
{
    auto doc = QJsonDocument::fromJson(blob);

    return loadAnalysisConfig(doc, inputInfo);
}

bool MVMEContext::loadAnalysisConfig(const QJsonDocument &doc, const QString &inputInfo,
                                     AnalysisLoadFlags flags)
{
    using namespace analysis;
    using namespace vme_analysis_common;

    QJsonObject json = doc.object();

    if (json.contains("AnalysisNG"))
    {
        json = json["AnalysisNG"].toObject();
    }

    auto analysis_ng = std::shared_ptr<Analysis>(new Analysis(),
        [] (auto a) { a->deleteLater(); });

    if (auto ec = analysis_ng->read(json, getVMEConfig()))
    {
        QMessageBox::critical(nullptr,
                              QSL("Error loading analysis"),
                              QSL("Error loading analysis from file %1: %2")
                              .arg(inputInfo)
                              .arg(ec.message().c_str()));

        return false;
    }

    handle_vme_analysis_assignment(getVMEConfig(), analysis_ng.get());

    try
    {
        bool was_running = isAnalysisRunning();

        if (was_running)
        {
            stopAnalysis();
        }

        m_analysis = std::move(analysis_ng);

        m_analysis->beginRun(getRunInfo(), getVMEConfig(),
                             [this](const QString &msg) { this->logMessage(msg); });

        if (m_d->m_analysisAutoSaver)
        {
            // (re)start the autosaver
            m_d->m_analysisAutoSaver->removeOutputFile();
            m_d->m_analysisAutoSaver->start();
        }

        emit analysisChanged(m_analysis.get());

        logMessage(QString("Loaded %1 from %2")
                   .arg(info_string(m_analysis.get()))
                   .arg(inputInfo)
                   );

        if (was_running && !flags.NoAutoResume)
        {
            resumeAnalysis(Analysis::ClearState);
        }
    }
    catch (const std::bad_alloc &e)
    {
        if (m_analysis)
            m_analysis->clear();
        setAnalysisConfigFilename(QString());
        if (m_mainwin)
        {
            QMessageBox::critical(m_mainwin, QSL("Error"),
                                  QString("Out of memory when creating analysis objects."));
        }
        emit analysisChanged(m_analysis.get());

        return false;
    }

    return true;
}

void MVMEContext::analysisWasCleared()
{
    if (m_d->m_analysisAutoSaver)
    {
        // (re)start the autosaver
        m_d->m_analysisAutoSaver->start();
    }
}

void MVMEContext::analysisWasSaved()
{
    if (m_d->m_analysisAutoSaver)
    {
        // (re)start the autosaver
        m_d->m_analysisAutoSaver->start();
    }
}

void MVMEContext::setListFileOutputInfo(const ListFileOutputInfo &info)
{
    m_d->m_listfileOutputInfo = info;

    auto settings = makeWorkspaceSettings();

    writeToSettings(info, *settings);

    emit ListFileOutputInfoChanged(info);
}

ListFileOutputInfo MVMEContext::getListFileOutputInfo() const
{
    // Tracing an issue on exit where m_d has already been destroyed but
    // MVMEMainWindow is calling this method.
    assert(m_d);
    return m_d->m_listfileOutputInfo;
}

QString MVMEContext::getListFileOutputDirectoryFullPath(const QString &directory) const
{
    QDir dir(directory);

    if (dir.isAbsolute())
        return dir.path();

    dir = QDir(getWorkspaceDirectory());
    return dir.filePath(directory);
}

/** True if at least one of VME-config and analysis-config is modified. */
bool MVMEContext::isWorkspaceModified() const
{
    return ((m_vmeConfig && m_vmeConfig->isModified())
            || (m_analysis && m_analysis->isModified())
           );
}

bool MVMEContext::isAnalysisRunning()
{
    return (getMVMEStreamWorkerState() != AnalysisWorkerState::Idle);
}

void MVMEContext::stopAnalysis()
{
    m_d->stopAnalysis();
}

void MVMEContext::resumeAnalysis(analysis::Analysis::BeginRunOption runOption)
{
    m_d->resumeAnalysis(runOption);
}

QJsonDocument MVMEContext::getAnalysisJsonDocument() const
{
    QJsonObject dest, json;
    getAnalysis()->write(dest);
    json[QSL("AnalysisNG")] = dest;
    QJsonDocument doc(json);
    return doc;
}

void MVMEContext::addAnalysisOperator(QUuid eventId,
                                      const std::shared_ptr<analysis::OperatorInterface> &op,
                                      s32 userLevel)
{
    if (auto eventConfig = m_vmeConfig->getEventConfig(eventId))
    {
        (void) eventConfig;
        AnalysisPauser pauser(getAnalysisServiceProvider());
        getAnalysis()->addOperator(eventId, userLevel, op);
        getAnalysis()->beginRun(analysis::Analysis::KeepState, getVMEConfig());

        if (m_analysisUi)
        {
            m_analysisUi->operatorAddedExternally(op);
        }
    }
}

void MVMEContext::analysisOperatorEdited(const std::shared_ptr<analysis::OperatorInterface> &op)
{
    AnalysisPauser pauser(getAnalysisServiceProvider());
    getAnalysis()->setOperatorEdited(op);
    getAnalysis()->beginRun(analysis::Analysis::KeepState, getVMEConfig());

    if (m_analysisUi)
    {
        m_analysisUi->operatorEditedExternally(op);
    }
}

RunInfo MVMEContext::getRunInfo() const
{
    return m_d->m_runInfo;
}

void MVMEContext::setRunNotes(const QString &notes)
{
    m_d->runNotes.access().ref() = notes;
}

QString MVMEContext::getRunNotes() const
{
    return m_d->runNotes.copy();
}

AnalysisServiceProvider *MVMEContext::getAnalysisServiceProvider() const
{
    return m_d->analysisServiceProvider;
}

// DAQPauser
DAQPauser::DAQPauser(MVMEContext *context)
    : context(context)
{
    was_running = (context->getDAQState() == DAQState::Running);

    qDebug() << __PRETTY_FUNCTION__ << "was_running =" << was_running;

    if (was_running)
    {
        context->pauseDAQ();
    }
}

DAQPauser::~DAQPauser()
{
    qDebug() << __PRETTY_FUNCTION__ << "was_running =" << was_running;
    if (was_running)
    {
        context->resumeDAQ();
    }
}
