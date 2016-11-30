#include "mvme_context.h"
#include "mvme.h"
#include "vmusb.h"
#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "mvme_event_processor.h"
#include "mvme_listfile.h"
#include "hist1d.h"
#include "hist2d.h"

#include <QtConcurrent>
#include <QTimer>
#include <QThread>

static const size_t dataBufferCount = 20;
static const size_t dataBufferSize = vmusb_constants::BufferMaxSize * 2; // double the size of a vmusb read buffer
static const int TryOpenControllerInterval_ms = 1000;
static const int PeriodicLoggingInterval_ms = 5000;

static void processQtEvents(QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents)
{
    QCoreApplication::processEvents(flags);
}

MVMEContext::MVMEContext(mvme *mainwin, QObject *parent)
    : QObject(parent)
    , m_ctrlOpenTimer(new QTimer(this))
    , m_logTimer(new QTimer(this))
    , m_readoutThread(new QThread(this))
    , m_readoutWorker(new VMUSBReadoutWorker(this))
    , m_bufferProcessor(new VMUSBBufferProcessor(this))
    , m_eventThread(new QThread(this))
    , m_eventProcessor(new MVMEEventProcessor(this))
    , m_mainwin(mainwin)
    , m_mode(GlobalMode::NotSet)
    , m_state(DAQState::Idle)
    , m_listFileWorker(new ListFileReader(m_daqStats))
{

    for (size_t i=0; i<dataBufferCount; ++i)
    {
        m_freeBuffers.push_back(new DataBuffer(dataBufferSize));
    }

    connect(m_ctrlOpenTimer, &QTimer::timeout, this, &MVMEContext::tryOpenController);
    m_ctrlOpenTimer->setInterval(TryOpenControllerInterval_ms);
    m_ctrlOpenTimer->start();

    connect(&m_ctrlOpenWatcher, &QFutureWatcher<VMEError>::finished, this, [this] {
        auto result = m_ctrlOpenWatcher.result();
        if (!result.isError())
        {
            logMessage(QString("Opened VME controller %1")
                       .arg(m_controller->getIdentifyingString()));
        }
    });

    connect(m_logTimer, &QTimer::timeout, this, &MVMEContext::logModuleCounters);
    m_logTimer->setInterval(PeriodicLoggingInterval_ms);


    m_readoutThread->setObjectName("ReadoutThread");
    m_readoutWorker->moveToThread(m_readoutThread);
    m_bufferProcessor->moveToThread(m_readoutThread);
    m_readoutWorker->setBufferProcessor(m_bufferProcessor);
    m_listFileWorker->moveToThread(m_readoutThread);

    m_readoutThread->start();

    connect(m_readoutWorker, &VMUSBReadoutWorker::stateChanged, this, &MVMEContext::onDAQStateChanged);
    connect(m_readoutWorker, &VMUSBReadoutWorker::logMessage, this, &MVMEContext::sigLogMessage);
    connect(m_readoutWorker, &VMUSBReadoutWorker::logMessages, this, &MVMEContext::logMessages);
    connect(m_bufferProcessor, &VMUSBBufferProcessor::logMessage, this, &MVMEContext::sigLogMessage);
    connect(m_listFileWorker, &ListFileReader::stateChanged, this, &MVMEContext::onDAQStateChanged);
    connect(m_listFileWorker, &ListFileReader::logMessage, this, &MVMEContext::sigLogMessage);
    connect(m_listFileWorker, &ListFileReader::replayStopped, this, &MVMEContext::onReplayDone);
    //connect(m_listFileWorker, &ListFileWorker::progressChanged, this, [this](qint64 cur, qint64 total) {
    //    qDebug() << cur << total;
    //});

    m_eventThread->setObjectName("EventProcessorThread");
    m_eventProcessor->moveToThread(m_eventThread);
    m_eventThread->start();
    connect(m_eventProcessor, &MVMEEventProcessor::logMessage, this, &MVMEContext::sigLogMessage);

    setMode(GlobalMode::DAQ);

    setDAQConfig(new DAQConfig(this));
    setAnalysisConfig(new AnalysisConfig(this));

    tryOpenController();
}

MVMEContext::~MVMEContext()
{
    if (getDAQState() != DAQState::Idle)
    {
        if (getMode() == GlobalMode::DAQ)
        {
            QMetaObject::invokeMethod(m_readoutWorker, "stop", Qt::QueuedConnection);
        }
        else if (getMode() == GlobalMode::ListFile)
        {
            QMetaObject::invokeMethod(m_listFileWorker, "stopReplay", Qt::QueuedConnection);
        }

        while ((getDAQState() != DAQState::Idle)
               || m_eventProcessor->isProcessingBuffer())
        {
            processQtEvents();
            QThread::msleep(50);
        }
    }

    qDebug() << __PRETTY_FUNCTION__ << "eventProcessor->isProcessingBuffer()" << m_eventProcessor->isProcessingBuffer();

    m_readoutThread->quit();
    m_readoutThread->wait();
    m_eventThread->quit();
    m_eventThread->wait();
    delete m_readoutWorker;
    delete m_bufferProcessor;
    delete m_eventProcessor;
    delete m_listFileWorker;
    delete m_listFile;
    qDeleteAll(m_freeBuffers);
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

void MVMEContext::setAnalysisConfig(AnalysisConfig *config)
{
    if (m_analysisConfig)
    {
        m_analysisConfig->setParent(nullptr);
        m_analysisConfig->deleteLater();

        for (auto histo: getObjects<Hist1D *>())
        {
            removeObject(histo);
            removeObjectMapping(histo, QSL("ObjectToConfig"));
        }
        
        for (auto histo: getObjects<Hist2D *>())
        {
            removeObject(histo);
            removeObjectMapping(histo, QSL("ObjectToConfig"));
        }

        for (auto histoConfig: m_analysisConfig->get1DHistogramConfigs())
        {
            removeObjectMapping(histoConfig, QSL("ConfigToObject"));
            removeObject(histoConfig, false);
        }

        for (auto histoConfig: m_analysisConfig->get2DHistogramConfigs())
        {
            removeObjectMapping(histoConfig, QSL("ConfigToObject"));
            removeObject(histoConfig, false);
        }

        auto filterMaps = m_analysisConfig->getFilters();

        for (auto it = filterMaps.begin(); it != filterMaps.end(); ++it)
        {
            for (auto jt = it->begin(); jt != it->end(); ++jt)
            {
                for (auto filterConfig: jt.value())
                {
                    removeObject(filterConfig);
                }
            }
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
        addObject(histo);
        addObjectMapping(histoConfig, histo, QSL("ConfigToObject"));
        addObjectMapping(histo, histoConfig, QSL("ObjectToConfig"));
    }

    connect(m_analysisConfig, &AnalysisConfig::objectAdded, this, &MVMEContext::addObject);
    connect(m_analysisConfig, &AnalysisConfig::objectAboutToBeRemoved, this, [this](QObject *object) {
        removeObject(object, false);
    });

    emit analysisConfigChanged(config);
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
    m_state = state;
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

void MVMEContext::onReplayDone()
{
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
    return m_state;
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
    setConfigFileName(QString());
    setMode(GlobalMode::ListFile);
}

void MVMEContext::setMode(GlobalMode mode)
{
    if (mode != m_mode)
    {
        switch (m_mode)
        {
            case GlobalMode::DAQ:
                {
                    disconnect(m_bufferProcessor, &VMUSBBufferProcessor::mvmeEventBufferReady,
                               m_eventProcessor, &MVMEEventProcessor::processDataBuffer);

                    disconnect(m_eventProcessor, &MVMEEventProcessor::bufferProcessed,
                               m_bufferProcessor, &VMUSBBufferProcessor::addFreeBuffer);
                } break;
            case GlobalMode::ListFile:
                {
                    disconnect(m_listFileWorker, &ListFileReader::mvmeEventBufferReady,
                               m_eventProcessor, &MVMEEventProcessor::processDataBuffer);

                    disconnect(m_eventProcessor, &MVMEEventProcessor::bufferProcessed,
                               m_listFileWorker, &ListFileReader::addFreeBuffer);
                } break;

            case GlobalMode::NotSet:
                break;
        }

        switch (mode)
        {
            case GlobalMode::DAQ:
                {
                    connect(m_bufferProcessor, &VMUSBBufferProcessor::mvmeEventBufferReady,
                            m_eventProcessor, &MVMEEventProcessor::processDataBuffer);

                    connect(m_eventProcessor, &MVMEEventProcessor::bufferProcessed,
                            m_bufferProcessor, &VMUSBBufferProcessor::addFreeBuffer);
                } break;
            case GlobalMode::ListFile:
                {
                    connect(m_listFileWorker, &ListFileReader::mvmeEventBufferReady,
                            m_eventProcessor, &MVMEEventProcessor::processDataBuffer);

                    connect(m_eventProcessor, &MVMEEventProcessor::bufferProcessed,
                            m_listFileWorker, &ListFileReader::addFreeBuffer);
                } break;

            case GlobalMode::NotSet:
                break;
        }


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

void MVMEContext::setConfigFileName(const QString &name)
{
    if (m_configFileName != name)
    {
        m_configFileName = name;
        emit daqConfigFileNameChanged(name);
    }
}

void MVMEContext::setAnalysisConfigFileName(const QString &name)
{
    if (m_analysisConfigFileName != name)
    {
        m_analysisConfigFileName = name;
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
}

void MVMEContext::startReplay()
{
    if (m_mode != GlobalMode::ListFile || !m_listFile || m_state != DAQState::Idle)
        return;

    prepareStart();
    emit sigLogMessage(QSL("Replay starting"));
    m_mainwin->clearLog();
    QMetaObject::invokeMethod(m_listFileWorker, "startFromBeginning", Qt::QueuedConnection);
    m_replayTime.restart();
}

void MVMEContext::startDAQ(quint32 nCycles)
{
    if (m_mode != GlobalMode::DAQ)
        return;

    emit daqAboutToStart(nCycles);

    prepareStart();
    emit sigLogMessage(QSL("DAQ starting"));
    m_mainwin->clearLog();
    QMetaObject::invokeMethod(m_readoutWorker, "start",
                              Qt::QueuedConnection, Q_ARG(quint32, nCycles));
}

void MVMEContext::stopDAQ()
{
    if (m_mode == GlobalMode::DAQ)
    {
        emit sigLogMessage(QSL("DAQ stopping"));
        QMetaObject::invokeMethod(m_readoutWorker, "stop",
                                  Qt::QueuedConnection);
    }
    else if (m_mode == GlobalMode::ListFile)
    {
        emit sigLogMessage(QSL("Replay stopping"));
        QMetaObject::invokeMethod(m_listFileWorker, "stopReplay",
                                  Qt::QueuedConnection);
    }
}

void MVMEContext::pauseDAQ()
{
    QMetaObject::invokeMethod(m_readoutWorker, "pause", Qt::QueuedConnection);
}

void MVMEContext::resumeDAQ()
{
    QMetaObject::invokeMethod(m_readoutWorker, "resume", Qt::QueuedConnection);
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

QString getFilterPath(MVMEContext *context, DataFilterConfig *filterConfig, int filterAddress)
{
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
    return QString();
}

QString getHistoPath(MVMEContext *context, Hist1DConfig *histoConfig)
{
    auto filterId = histoConfig->getFilterId();
    auto filterAddress = histoConfig->getFilterAddress();
    auto filterConfig = context->getAnalysisConfig()->findChildById<DataFilterConfig *>(filterId);
    return getFilterPath(context, filterConfig, filterAddress);
}

Hist1D *createHistogram(Hist1DConfig *config)
{
    Hist1D *result = new Hist1D(config->getBits());
    result->setProperty("configId", config->getId());
    return result;
}

Hist2D *createHistogram(Hist2DConfig *config)
{
    Hist2D *result = new Hist2D(config->getBits(Qt::XAxis), config->getBits(Qt::YAxis));
    result->setProperty("configId", config->getId());
    return result;
}
