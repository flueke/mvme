#include "mvme_context.h"
#include "mvme.h"
#include "vmusb.h"
#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "mvme_event_processor.h"
#include "mvme_listfile.h"
#include "analysis/analysis.h"
#include "analysis/analysis_ui.h"

#include <QtConcurrent>
#include <QTimer>
#include <QThread>
#include <QProgressDialog>
#include <QMessageBox>

// Buffers to pass between DAQ/replay and the analysis. The buffer size should
// be at least twice as big as the max VMUSB buffer size (2 * 64k). Just using
// 1MB buffers for now as that's a good value for the listfile readout and
// doesn't affect the VMUSB readout negatively.
static const size_t DataBufferCount = 10;
static const size_t DataBufferSize = Megabytes(1);

static const int TryOpenControllerInterval_ms = 1000;
static const int PeriodicLoggingInterval_ms = 5000;
static const QString WorkspaceIniName = "mvmeworkspace.ini";

static void stop_coordinated(VMUSBReadoutWorker *readoutWorker, MVMEEventProcessor *eventProcessor);

static void processQtEvents(QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents)
{
    QCoreApplication::processEvents(flags);
}

struct MVMEContextPrivate
{
    MVMEContext *m_q;

    void stopDAQ();
    void stopDAQReplay();
    void stopDAQDAQ();

    void stopAnalysis();
    void resumeAnalysis();

    void convertAnalysisJsonToV2(QJsonObject &json);
};

void MVMEContextPrivate::stopDAQ()
{
    switch (m_q->m_mode)
    {
        case GlobalMode::DAQ: stopDAQDAQ(); break;
        case GlobalMode::ListFile: stopDAQReplay(); break;
        InvalidDefaultCase;
    }
}

void MVMEContextPrivate::stopDAQReplay()
{
    // FIXME: This is dangerous as there's no way to cancel and if one of the
    // signals below does not get emitted we're stuck here.
    QProgressDialog progressDialog("Stopping Replay", QString(), 0, 0);
    progressDialog.setWindowModality(Qt::ApplicationModal);
    progressDialog.setCancelButton(nullptr);
    progressDialog.show();

    QEventLoop localLoop;

    // First stop the ListFileReader

    // The timer is used to avoid a race between the worker stopping and the
    // progress dialog entering its eventloop.

    if (m_q->m_listFileWorker->isRunning())
    {
        QTimer::singleShot(0, [this]() { QMetaObject::invokeMethod(m_q->m_listFileWorker, "stop", Qt::QueuedConnection); });
        auto con = QObject::connect(m_q->m_listFileWorker, &ListFileReader::replayStopped, &localLoop, &QEventLoop::quit);
        localLoop.exec();
        QObject::disconnect(con);
    }

    // At this point the ListFileReader is stopped and will not produce any
    // more buffers. Now tell the MVMEEventProcessor to stop after finishing
    // the current queue.

    // There should be no race here. If the analysis is running we will stop it
    // and receive the stopped() signal.  If it just now stopped on its own
    // (e.g. end of replay) the signal is pending and will be delivered as soon
    // as we enter the event loop.
    if (m_q->m_eventProcessor->getState() != EventProcessorState::Idle)
    {
        QTimer::singleShot(0, [this]() { QMetaObject::invokeMethod(m_q->m_eventProcessor, "stopProcessing", Qt::QueuedConnection); });
        auto con = QObject::connect(m_q->m_eventProcessor, &MVMEEventProcessor::stopped, &localLoop, &QEventLoop::quit);
        localLoop.exec();
        QObject::disconnect(con);
    }

    m_q->onDAQStateChanged(DAQState::Idle);
}

void MVMEContextPrivate::stopDAQDAQ()
{
    // FIXME: This is dangerous as there's no way to cancel and if one of the
    // signals below does not get emitted we're stuck here.
    QProgressDialog progressDialog("Stopping Data Acquisition", QString(), 0, 0);
    progressDialog.setWindowModality(Qt::ApplicationModal);
    progressDialog.setCancelButton(nullptr);
    progressDialog.show();

    QEventLoop localLoop;

    if (m_q->m_readoutWorker->isRunning())
    {
        QTimer::singleShot(0, [this]() { QMetaObject::invokeMethod(m_q->m_readoutWorker, "stop", Qt::QueuedConnection); });
        auto con = QObject::connect(m_q->m_readoutWorker, &VMUSBReadoutWorker::daqStopped, &localLoop, &QEventLoop::quit);
        localLoop.exec();
        QObject::disconnect(con);
    }

    if (m_q->m_eventProcessor->getState() != EventProcessorState::Idle)
    {
        QTimer::singleShot(0, [this]() { QMetaObject::invokeMethod(m_q->m_eventProcessor, "stopProcessing", Qt::QueuedConnection); });
        auto con = QObject::connect(m_q->m_eventProcessor, &MVMEEventProcessor::stopped, &localLoop, &QEventLoop::quit);
        localLoop.exec();
        QObject::disconnect(con);
    }

    m_q->onDAQStateChanged(DAQState::Idle);
}

void MVMEContextPrivate::stopAnalysis()
{
    QProgressDialog progressDialog("Stopping Analysis", QString(), 0, 0);
    progressDialog.setWindowModality(Qt::ApplicationModal);
    progressDialog.setCancelButton(nullptr);
    progressDialog.show();

    QEventLoop localLoop;

    if (m_q->m_eventProcessor->getState() != EventProcessorState::Idle)
    {
        // Tell the analysis top stop immediately
        QTimer::singleShot(0, [this]() { QMetaObject::invokeMethod(m_q->m_eventProcessor, "stopProcessing",
                                                                   Qt::QueuedConnection, Q_ARG(bool, false)); });
        QObject::connect(m_q->m_eventProcessor, &MVMEEventProcessor::stopped, &localLoop, &QEventLoop::quit);
        localLoop.exec();
    }

    qDebug() << __PRETTY_FUNCTION__ << "analysis stopped";
}

void MVMEContextPrivate::resumeAnalysis()
{
    if (m_q->m_eventProcessor->getState() == EventProcessorState::Idle)
    {
        QMetaObject::invokeMethod(m_q->m_eventProcessor, "startProcessing",
                                  Qt::QueuedConnection);

        qDebug() << __PRETTY_FUNCTION__ << "analysis resumed";
    }
}

void MVMEContextPrivate::convertAnalysisJsonToV2(QJsonObject &json)
{
    bool couldConvert = true;
    auto vmeConfig = m_q->getDAQConfig();

    // sources
    auto array = json["sources"].toArray();

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        auto objectJson = it->toObject();
        int eventIndex = objectJson["eventIndex"].toInt();
        int moduleIndex = objectJson["moduleIndex"].toInt();

        auto eventConfig = vmeConfig->getEventConfig(eventIndex);
        auto moduleConfig = vmeConfig->getModuleConfig(eventIndex, moduleIndex);

        if (eventConfig && moduleConfig)
        {
            objectJson["eventId"] = eventConfig->getId().toString();
            objectJson["moduleId"] = moduleConfig->getId().toString();
            *it = objectJson;
        }
        else
        {
            couldConvert = false;
        }
    }
    json["sources"] = array;

    // operators
    array = json["operators"].toArray();

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        auto objectJson = it->toObject();
        int eventIndex = objectJson["eventIndex"].toInt();

        auto eventConfig = vmeConfig->getEventConfig(eventIndex);

        if (eventConfig)
        {
            objectJson["eventId"] = eventConfig->getId().toString();
            *it = objectJson;
        }
        else
        {
            couldConvert = false;
        }
    }
    json["operators"] = array;

    if (couldConvert)
    {
        json["MVMEAnalysisVersion"] = 2; // bumping version number to 2

        qDebug() << "converted analysis config json to V2";
    }
}

MVMEContext::MVMEContext(mvme *mainwin, QObject *parent)
    : QObject(parent)
    , m_d(new MVMEContextPrivate)
    , m_ctrlOpenTimer(new QTimer(this))
    , m_logTimer(new QTimer(this))
    , m_readoutThread(new QThread(this))
    , m_readoutWorker(new VMUSBReadoutWorker(this))
    , m_bufferProcessor(new VMUSBBufferProcessor(this))
    , m_eventThread(new QThread(this))
    , m_eventProcessor(new MVMEEventProcessor(this))
    , m_mainwin(mainwin)
    , m_mode(GlobalMode::NotSet)
    , m_daqState(DAQState::Idle)
    , m_listFileWorker(new ListFileReader(m_daqStats))
    , m_analysis_ng(new analysis::Analysis)
{
    m_d->m_q = this;

    for (size_t i=0; i<DataBufferCount; ++i)
    {
        m_freeBufferQueue.queue.push_back(new DataBuffer(DataBufferSize));
    }

    // TODO: maybe hide these things a bit
    m_listFileWorker->m_freeBufferQueue = &m_freeBufferQueue;
    m_listFileWorker->m_filledBufferQueue = &m_filledBufferQueue;
    m_bufferProcessor->m_freeBufferQueue = &m_freeBufferQueue;
    m_bufferProcessor->m_filledBufferQueue = &m_filledBufferQueue;
    m_eventProcessor->m_freeBufferQueue = &m_freeBufferQueue;
    m_eventProcessor->m_filledBufferQueue = &m_filledBufferQueue;

#if 0
    auto bufferQueueDebugTimer = new QTimer(this);
    bufferQueueDebugTimer->start(5000);
    connect(bufferQueueDebugTimer, &QTimer::timeout, this, [this] () {
        qDebug() << "MVMEContext:"
            << "free buffers:" << m_freeBufferQueue.queue.size()
            << "filled buffers:" << m_filledBufferQueue.queue.size();
    });
#endif

    connect(m_ctrlOpenTimer, &QTimer::timeout, this, &MVMEContext::tryOpenController);
    m_ctrlOpenTimer->setInterval(TryOpenControllerInterval_ms);
    m_ctrlOpenTimer->start();

    connect(&m_ctrlOpenWatcher, &QFutureWatcher<VMEError>::finished, this, [this] {
        auto result = m_ctrlOpenWatcher.result();
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
            else
            {
                logMessage(QString("Opened VME controller %1")
                           .arg(m_controller->getIdentifyingString()));
            }
        }
    });

    connect(m_logTimer, &QTimer::timeout, this, &MVMEContext::logModuleCounters);
    m_logTimer->setInterval(PeriodicLoggingInterval_ms);


    m_readoutThread->setObjectName("mvme ReadoutThread");
    m_readoutWorker->moveToThread(m_readoutThread);
    m_bufferProcessor->moveToThread(m_readoutThread);
    m_readoutWorker->setBufferProcessor(m_bufferProcessor); // FIXME: useless
    m_listFileWorker->moveToThread(m_readoutThread);

    m_readoutThread->start();

    connect(m_readoutWorker, &VMUSBReadoutWorker::stateChanged, this, &MVMEContext::onDAQStateChanged);
    connect(m_readoutWorker, &VMUSBReadoutWorker::daqStopped, this, &MVMEContext::onDAQDone);

    connect(m_listFileWorker, &ListFileReader::stateChanged, this, &MVMEContext::onDAQStateChanged);
    connect(m_listFileWorker, &ListFileReader::replayStopped, this, &MVMEContext::onReplayDone);


    connect(m_readoutWorker, &VMUSBReadoutWorker::logMessage, this, &MVMEContext::sigLogMessage);
    connect(m_readoutWorker, &VMUSBReadoutWorker::logMessages, this, &MVMEContext::logMessages);
    connect(m_bufferProcessor, &VMUSBBufferProcessor::logMessage, this, &MVMEContext::sigLogMessage);

    m_eventThread->setObjectName("mvme AnalysisThread");
    m_eventProcessor->moveToThread(m_eventThread);
    m_eventThread->start();
    connect(m_eventProcessor, &MVMEEventProcessor::logMessage, this, &MVMEContext::sigLogMessage);
    connect(m_eventProcessor, &MVMEEventProcessor::stateChanged, this, &MVMEContext::onEventProcessorStateChanged);

    setMode(GlobalMode::DAQ);

    setDAQConfig(new DAQConfig(this));

    tryOpenController();
}

MVMEContext::~MVMEContext()
{
    if (getDAQState() != DAQState::Idle)
    {
        qDebug() << __PRETTY_FUNCTION__ << "waiting for DAQ/Replay to stop";

        if (getMode() == GlobalMode::DAQ)
        {
            QMetaObject::invokeMethod(m_readoutWorker, "stop", Qt::QueuedConnection);
        }
        else if (getMode() == GlobalMode::ListFile)
        {
            QMetaObject::invokeMethod(m_listFileWorker, "stop", Qt::QueuedConnection);
        }

        while ((getDAQState() != DAQState::Idle))
        {
            processQtEvents();
            QThread::msleep(50);
        }
    }

    if (getEventProcessorState() != EventProcessorState::Idle)
    {
        qDebug() << __PRETTY_FUNCTION__ << "waiting for event processing to stop";

        QMetaObject::invokeMethod(m_eventProcessor, "stopProcessing", Qt::QueuedConnection, Q_ARG(bool, false));

        while (getEventProcessorState() != EventProcessorState::Idle)
        {
            processQtEvents();
            QThread::msleep(50);
        }
    }

    m_readoutThread->quit();
    m_readoutThread->wait();
    m_eventThread->quit();
    m_eventThread->wait();

    delete m_analysis_ng;
    delete m_readoutWorker;
    delete m_bufferProcessor;
    delete m_eventProcessor;
    delete m_listFileWorker;
    delete m_listFile;

    Q_ASSERT(m_freeBufferQueue.queue.size() + m_filledBufferQueue.queue.size() == DataBufferCount);
    qDeleteAll(m_freeBufferQueue.queue);
    qDeleteAll(m_filledBufferQueue.queue);

    // Wait for possibly active VMEController::open() to return before deleting
    // the controller object.
    m_ctrlOpenFuture.waitForFinished();
    // Disconnect controller signals so that we're not emitting our own
    // controllerStateChanged anymore.
    disconnect(m_controller, &VMEController::controllerStateChanged, this, &MVMEContext::controllerStateChanged);
    delete m_controller;

    delete m_d;

    qDebug() << __PRETTY_FUNCTION__ << "context destroyed";
}

void MVMEContext::setDAQConfig(DAQConfig *config)
{
    // TODO: create new vmecontroller and the corresponding readout worker if
    // the controller type changed.

    if (m_daqConfig)
    {
        for (auto eventConfig: m_daqConfig->getEventConfigs())
            onEventAboutToBeRemoved(eventConfig);

        for (auto key: m_daqConfig->vmeScriptLists.keys())
        {
            auto scriptList = m_daqConfig->vmeScriptLists[key];

            for (auto vmeScript: scriptList)
                emit objectAboutToBeRemoved(vmeScript);
        }

        m_daqConfig->deleteLater();
    }

    m_daqConfig = config;
    config->setParent(this);

    for (auto event: config->eventConfigs)
        onEventAdded(event);

    connect(m_daqConfig, &DAQConfig::eventAdded, this, &MVMEContext::onEventAdded);
    connect(m_daqConfig, &DAQConfig::eventAboutToBeRemoved, this, &MVMEContext::onEventAboutToBeRemoved);
    connect(m_daqConfig, &DAQConfig::globalScriptAboutToBeRemoved, this, &MVMEContext::onGlobalScriptAboutToBeRemoved);

    emit daqConfigChanged(config);
}

void MVMEContext::setController(VMEController *controller)
{
    m_controller = controller;
    connect(m_controller, &VMEController::controllerStateChanged,
            this, &MVMEContext::controllerStateChanged);
    emit vmeControllerSet(controller);
}

ControllerState MVMEContext::getControllerState() const
{
    auto result = ControllerState::Unknown;
    if (m_controller)
        result = m_controller->getState();
    return result;
}

QString MVMEContext::getUniqueModuleName(const QString &prefix) const
{
    auto moduleConfigs = m_daqConfig->getAllModuleConfigs();
    QSet<QString> moduleNames;

    for (auto cfg: moduleConfigs)
    {
        if (cfg->objectName().startsWith(prefix))
        {
            moduleNames.insert(cfg->objectName());
        }
    }

    QString result = prefix;
    u32 suffix = 0;
    while (moduleNames.contains(result))
    {
        result = QString("%1_%2").arg(prefix).arg(suffix++);
    }
    return result;
}

void MVMEContext::tryOpenController()
{
    if (m_controller && !m_controller->isOpen() && !m_ctrlOpenFuture.isRunning())
    {
        m_ctrlOpenFuture = QtConcurrent::run(m_controller, &VMEController::openFirstDevice);
        m_ctrlOpenWatcher.setFuture(m_ctrlOpenFuture);
    }
}

void MVMEContext::logModuleCounters()
{
#if 1

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

    emit sigLogMessage(buffer);
#endif
}

void MVMEContext::onDAQStateChanged(DAQState state)
{
    m_daqState = state;
    emit daqStateChanged(state);

    switch (state)
    {
        case DAQState::Idle:
            {
                logModuleCounters();
            } break;

        case DAQState::Starting:
            {
                //m_logTimer->start();
            } break;

        case DAQState::Running:
        case DAQState::Paused:
            break;

        case DAQState::Stopping:
            {
                //m_logTimer.stop();
            } break;
    }
}

void MVMEContext::onEventProcessorStateChanged(EventProcessorState state)
{
    emit eventProcessorStateChanged(state);
}

// Called on VMUSBReadoutWorker::daqStopped()
void MVMEContext::onDAQDone()
{
    QMetaObject::invokeMethod(m_eventProcessor, "stopProcessing", Qt::QueuedConnection);
}

// Called on ListFileReader::replayStopped()
void MVMEContext::onReplayDone()
{
    QMetaObject::invokeMethod(m_eventProcessor, "stopProcessing", Qt::QueuedConnection);

    double secondsElapsed = m_replayTime.elapsed() / 1000.0;
    u64 replayBytes = m_daqStats.totalBytesRead;
    double replayMB = (double)replayBytes / (1024.0 * 1024.0);
    double mbPerSecond = 0.0;
    if (secondsElapsed > 0)
    {
        mbPerSecond = replayMB / secondsElapsed;
    }

    QString str = QString("Replay finished: Read %1 MB in %2 s, %3 MB/s\n")
        .arg(replayMB)
        .arg(secondsElapsed)
        .arg(mbPerSecond)
        ;

    emit sigLogMessage(str);
}

DAQState MVMEContext::getDAQState() const
{
    return m_daqState;
}

EventProcessorState MVMEContext::getEventProcessorState() const
{
    // FIXME: might be better to keep a local copy which is _only_ updated
    // through the signal/slot mechanism. That way it's thread safe.
    return m_eventProcessor->getState();
}

void MVMEContext::setReplayFile(ListFile *listFile)
{
    auto configJson = listFile->getDAQConfig();
    auto daqConfig = new DAQConfig;
    daqConfig->read(configJson);
    setDAQConfig(daqConfig);

    delete m_listFile;
    m_listFile = listFile;
    m_listFileWorker->setListFile(listFile);
    setConfigFileName(QString(), false);
    setMode(GlobalMode::ListFile);
}

void MVMEContext::closeReplayFile()
{
    if (getMode() == GlobalMode::ListFile)
    {
        stopDAQ();

        delete m_listFile;
        m_listFile = nullptr;
        m_listFileWorker->setListFile(nullptr);

        /* Open the last used VME config in the workspace. Create a new VME config
         * if no previous exists. */

        QString lastVMEConfig = makeWorkspaceSettings()->value(QSL("LastVMEConfig")).toString();

        if (!lastVMEConfig.isEmpty())
        {
            QDir wsDir(getWorkspaceDirectory());
            loadVMEConfig(wsDir.filePath(lastVMEConfig));
        }
        else
        {
            setDAQConfig(new DAQConfig);
            setConfigFileName(QString());
            setMode(GlobalMode::DAQ);
        }
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

void MVMEContext::setConfigFileName(QString name, bool updateWorkspace)
{
    if (m_configFileName != name)
    {
        m_configFileName = name;
        if (updateWorkspace)
        {
            makeWorkspaceSettings()->setValue(QSL("LastVMEConfig"), name.remove(getWorkspaceDirectory() + '/'));
        }
        emit daqConfigFileNameChanged(name);
    }
}

void MVMEContext::setAnalysisConfigFileName(QString name)
{
    if (m_analysisConfigFileName != name)
    {
        m_analysisConfigFileName = name;
        makeWorkspaceSettings()->setValue(QSL("LastAnalysisConfig"), name.remove(getWorkspaceDirectory() + '/'));
        emit analysisConfigFileNameChanged(name);
    }
}

void MVMEContext::prepareStart()
{
#if 0
    // Use this to force a crash in case deleted objects remain in the object set.
    for (auto it=m_objects.begin(); it!=m_objects.end(); ++it)
    {
        qDebug() << reinterpret_cast<void *>(*it);
        qDebug() << *it;
    }
#endif

    m_eventProcessor->newRun();

    m_daqStats = DAQStats();

    qDebug() << __PRETTY_FUNCTION__
        << "free buffers:" << m_freeBufferQueue.queue.size()
        << "filled buffers:" << m_filledBufferQueue.queue.size();
}

void MVMEContext::startDAQ(quint32 nCycles)
{
    Q_ASSERT(getDAQState() == DAQState::Idle);
    Q_ASSERT(getEventProcessorState() == EventProcessorState::Idle);

    if (m_mode != GlobalMode::DAQ
        || getDAQState() != DAQState::Idle
        || getEventProcessorState() != EventProcessorState::Idle)
    {
        return;
    }

    emit daqAboutToStart(nCycles);

    prepareStart();
    emit sigLogMessage(QSL("DAQ starting"));
    m_mainwin->clearLog();
    QMetaObject::invokeMethod(m_readoutWorker, "start",
                              Qt::QueuedConnection, Q_ARG(quint32, nCycles));
    QMetaObject::invokeMethod(m_eventProcessor, "startProcessing",
                              Qt::QueuedConnection);
}

void MVMEContext::stopDAQ()
{
    m_d->stopDAQ();
}

void MVMEContext::pauseDAQ()
{
    QMetaObject::invokeMethod(m_readoutWorker, "pause", Qt::QueuedConnection);
}

void MVMEContext::resumeDAQ()
{
    QMetaObject::invokeMethod(m_readoutWorker, "resume", Qt::QueuedConnection);
}

void MVMEContext::startReplay(u32 nEvents)
{
    Q_ASSERT(getDAQState() == DAQState::Idle);
    Q_ASSERT(getEventProcessorState() == EventProcessorState::Idle);

    if (m_mode != GlobalMode::ListFile || !m_listFile
        || getDAQState() != DAQState::Idle
        || getEventProcessorState() != EventProcessorState::Idle)
    {
        return;
    }

    prepareStart();
    emit sigLogMessage(QSL("Replay starting"));
    m_mainwin->clearLog();

    m_listFileWorker->setEventsToRead(nEvents);

    QMetaObject::invokeMethod(m_listFileWorker, "start", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_eventProcessor, "startProcessing", Qt::QueuedConnection);

    m_replayTime.restart();
}

void MVMEContext::pauseReplay()
{
    QMetaObject::invokeMethod(m_listFileWorker, "pause", Qt::QueuedConnection);
}

void MVMEContext::resumeReplay(u32 nEvents)
{
    Q_ASSERT(getDAQState() == DAQState::Idle || getDAQState() == DAQState::Paused);
    m_listFileWorker->setEventsToRead(nEvents);
    QMetaObject::invokeMethod(m_listFileWorker, "resume", Qt::QueuedConnection);
}

void MVMEContext::openInNewWindow(QObject *object)
{
    if (m_mainwin)
        m_mainwin->openInNewWindow(object);
}

void MVMEContext::addWidgetWindow(QWidget *widget, QSize windowSize)
{
    if (m_mainwin)
    {
        m_mainwin->addWidgetWindow(widget, windowSize);
    }
}

void MVMEContext::logMessage(const QString &msg)
{
    emit sigLogMessage(msg);
}

void MVMEContext::logMessages(const QStringList &messages, const QString &prefix)
{
    for (auto msg: messages)
    {
        emit sigLogMessage(prefix + msg);
    }
}

void MVMEContext::onEventAdded(EventConfig *event)
{
    emit eventAdded(event);
    for (auto module: event->modules)
        onModuleAdded(module);

    connect(event, &EventConfig::moduleAdded, this, &MVMEContext::onModuleAdded);
    connect(event, &EventConfig::moduleAboutToBeRemoved, this, &MVMEContext::onModuleAboutToBeRemoved);
}

void MVMEContext::onEventAboutToBeRemoved(EventConfig *config)
{
    for (auto module: config->modules)
    {
        onModuleAboutToBeRemoved(module);
        emit objectAboutToBeRemoved(module);
    }

    for (auto key: config->vmeScripts.keys())
        emit objectAboutToBeRemoved(config->vmeScripts[key]);

    emit objectAboutToBeRemoved(config);
    emit eventAboutToBeRemoved(config);
}

void MVMEContext::onGlobalScriptAboutToBeRemoved(VMEScriptConfig *config)
{
    emit objectAboutToBeRemoved(config);
}

void MVMEContext::onModuleAdded(ModuleConfig *module)
{
    //qDebug() << __PRETTY_FUNCTION__ << module;
    emit moduleAdded(module);
}

void MVMEContext::onModuleAboutToBeRemoved(ModuleConfig *module)
{
    for (auto key: module->vmeScripts.keys())
        emit objectAboutToBeRemoved(module->vmeScripts[key]);
    emit moduleAboutToBeRemoved(module);
}

//QFuture<vme_script::ResultList>
/* FIXME: this is a bad hack
 * ExcludeUserInputEvents is used to "freeze" the GUI in case the transition to
 * Paused state takes some time. This prevents the user from clicking "run"
 * multiple times (or invoking run via different gui elements)
 * What if the transition to Paused never happens? We're stuck here...
 */
vme_script::ResultList
MVMEContext::runScript(const vme_script::VMEScript &script,
                       vme_script::LoggerFun logger,
                       bool logEachResult)
{
    auto may_run_script = [this]()
    {
        auto daqState = this->getDAQState();
        return (daqState == DAQState::Idle || daqState == DAQState::Paused);
    };

    bool wasPaused = (getDAQState() == DAQState::Paused);
    pauseDAQ();
    while (!may_run_script())
    {
        processQtEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents);
    }

    auto result = vme_script::run_script(m_controller, script, logger, logEachResult);

    if (!wasPaused)
        resumeDAQ();

    return result;
}

//
// Workspace handling
//
void MVMEContext::newWorkspace(const QString &dirName)
{
    QDir dir(dirName);

    if (!dir.entryList(QDir::AllEntries | QDir::NoDot | QDir::NoDotDot).isEmpty())
        throw QString(QSL("Selected workspace directory is not empty"));

    if (!dir.mkdir(QSL("listfiles")))
        throw QString(QSL("Error creating listfiles subdirectory"));

    QFile vmeConfigFile(dir.filePath(QSL("vme.mvmecfg")));
    if (!vmeConfigFile.open(QIODevice::WriteOnly))
    {
        throw QString("Error opening %1 for writing:  %2")
            .arg(vmeConfigFile.fileName())
            .arg(vmeConfigFile.errorString());
    }

    QFile analysisConfigFile(dir.filePath(QSL("analysis.analysis")));
    if (!analysisConfigFile.open(QIODevice::WriteOnly))
    {
        throw QString("Error opening %1 for writing:  %2")
            .arg(analysisConfigFile.fileName())
            .arg(analysisConfigFile.errorString());
    }

    setWorkspaceDirectory(dirName);

    {
        auto workspaceSettings(makeWorkspaceSettings());
        workspaceSettings->setValue(QSL("LastVMEConfig"), QSL("vme.mvmecfg"));
        workspaceSettings->setValue(QSL("LastAnalysisConfig"), QSL("analysis.analysis"));
        workspaceSettings->setValue(QSL("ListfileDirectory"), QSL("listfiles"));
        workspaceSettings->setValue(QSL("WriteListfile"), true);
        workspaceSettings->sync();

        if (workspaceSettings->status() != QSettings::NoError)
        {
            throw QString("Error writing workspace settings to %1")
                .arg(workspaceSettings->fileName());
        }
    }

    openWorkspace(dirName);
}

void MVMEContext::openWorkspace(const QString &dirName)
{
    QDir dir(dirName);

    if (!dir.exists(WorkspaceIniName))
    {
        throw QString("Workspace settings file %1 not found in %2")
            .arg(WorkspaceIniName)
            .arg(dirName);
    }

    setWorkspaceDirectory(dirName);
    auto workspaceSettings(makeWorkspaceSettings());

    auto listfileDirectory  = workspaceSettings->value(QSL("ListfileDirectory")).toString();
    auto listfileEnabled    = workspaceSettings->value(QSL("WriteListfile")).toBool();
    auto lastVMEConfig      = workspaceSettings->value(QSL("LastVMEConfig")).toString();
    auto lastAnalysisConfig = workspaceSettings->value(QSL("LastAnalysisConfig")).toString();

    setListFileDirectory(dir.filePath(listfileDirectory));
    setListFileOutputEnabled(listfileEnabled);

    if (!lastVMEConfig.isEmpty())
    {
        loadVMEConfig(dir.filePath(lastVMEConfig));
    }

    if (!lastAnalysisConfig.isEmpty())
    {
        loadAnalysisConfig(dir.filePath(lastAnalysisConfig));
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
    QDir dir(getWorkspaceDirectory());
    return std::make_shared<QSettings>(dir.filePath(WorkspaceIniName), QSettings::IniFormat);
}

void MVMEContext::loadVMEConfig(const QString &fileName)
{
    QJsonDocument doc(gui_read_json_file(fileName));
    auto daqConfig = new DAQConfig;
    daqConfig->read(doc.object()["DAQConfig"].toObject());
    setDAQConfig(daqConfig);
    setConfigFileName(fileName);
    setMode(GlobalMode::DAQ);
}

void MVMEContext::loadAnalysisConfig(const QString &fileName)
{
    qDebug() << "loadAnalysisConfig from" << fileName;

    QJsonDocument doc(gui_read_json_file(fileName));

    using namespace analysis;

    auto json = doc.object()[QSL("AnalysisNG")].toObject();

    /* This code converts from analysis config versions prior to V2, which
     * stored eventIndex and moduleIndex instead of eventId and moduleId. This
     * could be done in Analysis::read() but then the Analysis object would
     * need a reference to either this context or the current DAQConfig which
     * is not great.
     * The solution implemented here makes it so that this context object has
     * knowledge about the Analysis json structure which isn't great either.
     */
    int version = json[QSL("MVMEAnalysisVersion")].toInt(0);
    if (version < 2)
    {
        m_d->convertAnalysisJsonToV2(json);
    }

    auto analysis_ng = std::make_unique<Analysis>();
    auto readResult = analysis_ng->read(json);

    if (readResult.code != Analysis::ReadResult::NoError)
    {
        // TODO: print readResult.data in the messagebox
        qDebug() << "!!!!! Error reading analysis ng from" << fileName << readResult.code << readResult.data;
        QMessageBox::critical(m_mainwin, QSL("Error"),
                              QString("Error loading analysis\n"
                                      "File %1\n"
                                      "Error: %2")
                              .arg(fileName)
                              .arg(Analysis::ReadResult::ErrorCodeStrings.value(readResult.code, "Unknown error")));
    }
    else
    {
        try
        {
            bool was_running = isAnalysisRunning();

            if (was_running)
            {
                stopAnalysis();
            }

            delete m_analysis_ng;
            m_analysis_ng = analysis_ng.release();

            // Prepares operators, allocates histograms, etc..
            m_eventProcessor->newRun();
            setAnalysisConfigFileName(fileName);
            emit analysisNGChanged();

            if (was_running)
            {
                resumeAnalysis();
            }
        }
        catch (const std::bad_alloc &e)
        {
            m_analysis_ng->clear();
            setAnalysisConfigFileName(QString());
            QMessageBox::critical(m_mainwin, QSL("Error"), QString("Out of memory when creating analysis objects."));
            emit analysisNGChanged();
        }
    }
}

void MVMEContext::setListFileDirectory(const QString &dirName)
{
    m_listFileDir = dirName;
}

void MVMEContext::setListFileOutputEnabled(bool b)
{
    if (m_listFileEnabled != b)
    {
        m_listFileEnabled = b;
        makeWorkspaceSettings()->setValue(QSL("WriteListfile"), b);
    }
}

/** True if at least one of VME-config and analysis-config is modified. */
bool MVMEContext::isWorkspaceModified() const
{
    return ((m_daqConfig && m_daqConfig->isModified())
            || (m_analysis_ng && m_analysis_ng->isModified())
           );
}

bool MVMEContext::isAnalysisRunning()
{
    return (getEventProcessorState() != EventProcessorState::Idle);
}

void MVMEContext::stopAnalysis()
{
    m_d->stopAnalysis();
}

void MVMEContext::resumeAnalysis()
{
    m_d->resumeAnalysis();
}

void MVMEContext::addAnalysisOperator(QUuid eventId, const std::shared_ptr<analysis::OperatorInterface> &op, s32 userLevel)
{
    auto eventConfig = m_daqConfig->getEventConfig(eventId);
    if (eventConfig)
    {
        AnalysisPauser pauser(this);
        getAnalysisNG()->addOperator(eventId, op, userLevel);

        if (m_analysisUi)
        {
            m_analysisUi->operatorAdded(op);
        }
    }
}

void MVMEContext::analysisOperatorEdited(const std::shared_ptr<analysis::OperatorInterface> &op)
{
    AnalysisPauser pauser(this);
    analysis::do_beginRun_forward(op.get());

    if (m_analysisUi)
    {
        m_analysisUi->operatorEdited(op);
    }
}

AnalysisPauser::AnalysisPauser(MVMEContext *context)
    : context(context)
{
    was_running = context->isAnalysisRunning();

    qDebug() << __PRETTY_FUNCTION__ << was_running;

    if (was_running)
    {
        context->stopAnalysis();
    }
}

AnalysisPauser::~AnalysisPauser()
{
    qDebug() << __PRETTY_FUNCTION__ << was_running;
    if (was_running)
    {
        context->resumeAnalysis();
    }
}
