#include "mvme_context.h"
#include "mvme.h"
#include "vmusb.h"
#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "mvme_event_processor.h"
#include "mvme_listfile.h"
#include "hist1d.h"
#include "hist2d.h"
#include "analysis/analysis.h"

#include <QtConcurrent>
#include <QTimer>
#include <QThread>
#include <QProgressDialog>

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
};

// FIXME: there are no checks done to see if any of the workers is already idle
// Right now these checks are only done in DAQControlWidget to decide which buttons to enable

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

    QTimer::singleShot(0, [this]() { QMetaObject::invokeMethod(m_q->m_listFileWorker, "stopReplay", Qt::QueuedConnection); });
    auto con = QObject::connect(m_q->m_listFileWorker, &ListFileReader::replayStopped, &localLoop, &QEventLoop::quit);
    localLoop.exec();
    QObject::disconnect(con);

    // At this point the ListFileReader is stopped and will not produce any
    // more buffers. Now tell the MVMEEventProcessor to stop after finishing
    // the current queue.
    // Alternative: monitor the m_filledBufferQueue and wait until it's empty

    QTimer::singleShot(0, [this]() { QMetaObject::invokeMethod(m_q->m_eventProcessor, "stopProcessing", Qt::QueuedConnection); });
    con = QObject::connect(m_q->m_eventProcessor, &MVMEEventProcessor::stopped, &localLoop, &QEventLoop::quit);
    localLoop.exec();
    QObject::disconnect(con);

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

    QTimer::singleShot(0, [this]() { QMetaObject::invokeMethod(m_q->m_readoutWorker, "stop", Qt::QueuedConnection); });
    auto con = QObject::connect(m_q->m_readoutWorker, &VMUSBReadoutWorker::daqStopped, &localLoop, &QEventLoop::quit);
    localLoop.exec();
    QObject::disconnect(con);

    QTimer::singleShot(0, [this]() { QMetaObject::invokeMethod(m_q->m_eventProcessor, "stopProcessing", Qt::QueuedConnection); });
    con = QObject::connect(m_q->m_eventProcessor, &MVMEEventProcessor::stopped, &localLoop, &QEventLoop::quit);
    localLoop.exec();
    QObject::disconnect(con);

    m_q->onDAQStateChanged(DAQState::Idle);
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

    // FIXME: onDAQStateChanged() does not properly describe the system
    // anymore. It doesn't say anything about event processing but the
    // implementation assumes that it does. Split this into something finer
    // grained? But most parts of the UI are only interested in whether things
    // are running or are stopped, not the individual components.  This is
    // different for the analysis which soon will want to know if
    // MVMEEventProcessor has been paused and it's thus safe to perform
    // modifications.

    connect(m_readoutWorker, &VMUSBReadoutWorker::stateChanged, this, &MVMEContext::onDAQStateChanged);
    connect(m_listFileWorker, &ListFileReader::stateChanged, this, &MVMEContext::onDAQStateChanged);


    connect(m_readoutWorker, &VMUSBReadoutWorker::logMessage, this, &MVMEContext::sigLogMessage);
    connect(m_readoutWorker, &VMUSBReadoutWorker::logMessages, this, &MVMEContext::logMessages);
    connect(m_bufferProcessor, &VMUSBBufferProcessor::logMessage, this, &MVMEContext::sigLogMessage);
    // FIXME: not actually emitted by ListFileReader
    //connect(m_listFileWorker, &ListFileReader::logMessage, this, &MVMEContext::sigLogMessage);
    connect(m_listFileWorker, &ListFileReader::replayStopped, this, &MVMEContext::onReplayDone);

    m_eventThread->setObjectName("mvme AnalysisThread");
    m_eventProcessor->moveToThread(m_eventThread);
    m_eventThread->start();
    connect(m_eventProcessor, &MVMEEventProcessor::logMessage, this, &MVMEContext::sigLogMessage);
    connect(m_eventProcessor, &MVMEEventProcessor::stateChanged, this, &MVMEContext::onEventProcessorStateChanged);

    setMode(GlobalMode::DAQ);

    setDAQConfig(new DAQConfig(this));
#ifdef ENABLE_OLD_ANALYSIS
    setAnalysisConfig(new AnalysisConfig(this));
#endif

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
            QMetaObject::invokeMethod(m_listFileWorker, "stopReplay", Qt::QueuedConnection);
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

        QMetaObject::invokeMethod(m_eventProcessor, "stopProcessing", Qt::QueuedConnection);

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
    qDeleteAll(m_freeBuffers);
    Q_ASSERT(m_freeBufferQueue.queue.size() == DataBufferCount);
    Q_ASSERT(m_filledBufferQueue.queue.size() == 0);
    qDeleteAll(m_freeBufferQueue.queue);
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

#ifdef ENABLE_OLD_ANALYSIS
void MVMEContext::setAnalysisConfig(AnalysisConfig *config)
{
    if (m_analysisConfig)
    {
        m_analysisConfig->setParent(nullptr);
        m_analysisConfig->deleteLater();

        for (auto config: m_analysisConfig->get1DHistogramConfigs())
        {
            auto object = getObjectForConfig(config);
            unregisterObjectAndConfig(object, config);
            removeObject(object);
            removeObject(config, false);
        }

        for (auto config: m_analysisConfig->get2DHistogramConfigs())
        {
            auto object = getObjectForConfig(config);
            unregisterObjectAndConfig(object, config);
            removeObject(object);
            removeObject(config, false);
        }

        {
            auto filterMaps = m_analysisConfig->getFilters();
            for (auto it = filterMaps.begin(); it != filterMaps.end(); ++it)
                for (auto jt = it->begin(); jt != it->end(); ++jt)
                    for (auto filterConfig: jt.value())
                        removeObject(filterConfig);
        }

        {
            auto filterMaps = m_analysisConfig->getDualWordFilters();
            for (auto it = filterMaps.begin(); it != filterMaps.end(); ++it)
                for (auto jt = it->begin(); jt != it->end(); ++jt)
                    for (auto filterConfig: jt.value())
                        removeObject(filterConfig);
        }
    }

    m_analysisConfig = config;
    config->setParent(this);

    for (auto histoConfig: config->get1DHistogramConfigs())
    {
        auto histo = createHistogram(histoConfig);
        histo->setParent(this);
        addObjectMapping(histoConfig, histo, QSL("ConfigToObject"));
        addObjectMapping(histo, histoConfig, QSL("ObjectToConfig"));
        addObject(histo);
    }

    for (auto histoConfig: config->get2DHistogramConfigs())
    {
        auto histo = createHistogram(histoConfig);
        histo->setParent(this);
        addObjectMapping(histoConfig, histo, QSL("ConfigToObject"));
        addObjectMapping(histo, histoConfig, QSL("ObjectToConfig"));
        addObject(histo);
    }

    connect(m_analysisConfig, &AnalysisConfig::objectAdded, this, &MVMEContext::addObject);
    connect(m_analysisConfig, &AnalysisConfig::objectAboutToBeRemoved, this, [this](QObject *object) {
        removeObject(object, false);
    });

    emit analysisConfigChanged(config);
}
#endif

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

void MVMEContext::setListFile(ListFile *listFile)
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


    for (auto histo: getObjects<Hist1D *>())
        histo->clear();

    for (auto histo: getObjects<Hist2D *>())
        histo->clear();

    m_eventProcessor->newRun();

    m_daqStats = DAQStats();

    qDebug() << __PRETTY_FUNCTION__
        << "free buffers:" << m_freeBufferQueue.queue.size()
        << "filled buffers:" << m_filledBufferQueue.queue.size();
}

void MVMEContext::startReplay(quint32 nEvents)
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

    QMetaObject::invokeMethod(m_listFileWorker, "startFromBeginning",
                              Qt::QueuedConnection, Q_ARG(quint32, nEvents));

    QMetaObject::invokeMethod(m_eventProcessor, "startProcessing",
                              Qt::QueuedConnection);

    m_replayTime.restart();
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
}

void MVMEContext::onGlobalScriptAboutToBeRemoved(VMEScriptConfig *config)
{
    emit objectAboutToBeRemoved(config);
}

void MVMEContext::onModuleAdded(ModuleConfig *module)
{
    //qDebug() << __PRETTY_FUNCTION__ << module;
}

void MVMEContext::onModuleAboutToBeRemoved(ModuleConfig *module)
{
    for (auto key: module->vmeScripts.keys())
        emit objectAboutToBeRemoved(module->vmeScripts[key]);
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

    QFile analysisConfigFile(dir.filePath(QSL("analysis.json")));
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
        workspaceSettings->setValue(QSL("LastAnalysisConfig"), QSL("analysis.json"));
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
    QJsonDocument doc(gui_read_json_file(fileName));
#ifdef ENABLE_OLD_ANALYSIS
    auto config = new AnalysisConfig;
    config->read(doc.object()[QSL("AnalysisConfig")].toObject());
    setAnalysisConfig(config);
#endif
    setAnalysisConfigFileName(fileName);


    // FIXME: incomplete; error checking needed
    auto analysis_ng = new analysis::Analysis;
    auto readResult = analysis_ng->read(doc.object()[QSL("AnalysisNG")].toObject());
    if (readResult.code != analysis::Analysis::ReadResult::NoError)
    {
        // TODO: press the self-destruct button
        qDebug() << "!!!!! Error reading analysis ng from" << fileName << readResult.code << readResult.data;
    }
    else
    {
        delete m_analysis_ng;
        m_analysis_ng = analysis_ng;
        m_analysis_ng->beginRun(); // FIXME: good place to call this?
        emit analysisNGChanged();
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
#ifdef ENABLE_OLD_ANALYSIS
    return ((m_daqConfig && m_daqConfig->isModified())
            || (m_analysisConfig && m_analysisConfig->isModified()));
#else
    return ((m_daqConfig && m_daqConfig->isModified()));
#endif
}

QString getFilterPath(MVMEContext *context, DataFilterConfig *filterConfig, int filterAddress)
{
#ifdef ENABLE_OLD_ANALYSIS
    auto indexPair = context->getAnalysisConfig()->getEventAndModuleIndices(filterConfig);
    if (indexPair.first >= 0)
    {
        auto eventConfig = context->getDAQConfig()->getEventConfig(indexPair.first);
        auto moduleConfig = context->getDAQConfig()->getModuleConfig(indexPair.first, indexPair.second);

        if (eventConfig && moduleConfig)
        {
            return QString("%1/%2/%3/%4")
                .arg(eventConfig->objectName())
                .arg(moduleConfig->objectName())
                .arg(filterConfig->objectName())
                .arg(filterAddress);
        }
    }
#endif
    return QString();
}

QString getHistoPath(MVMEContext *context, Hist1DConfig *histoConfig)
{
#if ENABLE_ANALYSIS_NG
    auto filterId = histoConfig->getFilterId();
    auto filterAddress = histoConfig->getFilterAddress();
    auto filterConfig = context->getAnalysisConfig()->findChildById<DataFilterConfig *>(filterId);
    return getFilterPath(context, filterConfig, filterAddress);
#else
    return QString();
#endif
}

Hist1D *createHistogram(Hist1DConfig *config, MVMEContext *ctx)
{
    Hist1D *result = new Hist1D(config->getBits());
    result->setObjectName(config->objectName());

    if (ctx)
    {
        result->setParent(ctx);
        ctx->registerObjectAndConfig(result, config);
    }

    return result;
}

Hist2D *createHistogram(Hist2DConfig *config, MVMEContext *ctx)
{
    Hist2D *result = new Hist2D(config->getBits(Qt::XAxis), config->getBits(Qt::YAxis));
    result->setObjectName(config->objectName());

    if (ctx)
    {
        result->setParent(ctx);
        ctx->registerObjectAndConfig(result, config);
    }

    return result;
}
