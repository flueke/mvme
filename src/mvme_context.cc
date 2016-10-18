#include "mvme_context.h"
#include "mvme.h"
#include "vmusb.h"
#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "mvme_event_processor.h"
#include "mvme_listfile.h"

#include <QtConcurrent>
#include <QTimer>
#include <QThread>

static const size_t dataBufferCount = 20;
static const size_t dataBufferSize = vmusb_constants::BufferMaxSize * 2; // double the size of a vmusb read buffer
static const int TryOpenControllerInterval_ms = 250;
static const int PeriodicLoggingInterval_ms = 5000;

MVMEContext::MVMEContext(mvme *mainwin, QObject *parent)
    : QObject(parent)
    , m_config(new DAQConfig)
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
}

MVMEContext::~MVMEContext()
{
    QMetaObject::invokeMethod(m_readoutWorker, "stop", Qt::QueuedConnection);
    while (getDAQState() != DAQState::Idle)
    {
        QThread::msleep(50);
    }
    m_readoutThread->quit();
    m_readoutThread->wait();
    m_eventThread->quit();
    m_eventThread->wait();
    delete m_readoutWorker;
    delete m_bufferProcessor;
    delete m_eventProcessor;
    delete m_listFileWorker;
    delete m_listFile;
    delete m_config;
}

void MVMEContext::setConfig(DAQConfig *config)
{
    // TODO: create new vmecontroller and the corresponding readout worker if
    // the controller type changed.

    delete m_config;

    m_config = config;

    for (auto event: config->eventConfigs)
        onEventAdded(event);

    connect(m_config, &DAQConfig::eventAdded, this, &MVMEContext::onEventAdded);
    connect(m_config, &DAQConfig::eventAboutToBeRemoved, this, &MVMEContext::onEventAboutToBeRemoved);

    emit configChanged(config);
}

void MVMEContext::setController(VMEController *controller)
{
    m_controller = controller;
    connect(m_controller, &VMEController::controllerStateChanged,
            this, &MVMEContext::controllerStateChanged);
    emit vmeControllerSet(controller);
}

QString MVMEContext::getUniqueModuleName(const QString &prefix) const
{
    auto moduleConfigs = m_config->getAllModuleConfigs();
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
    read(listFile->getDAQConfig());
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
                               m_eventProcessor, &MVMEEventProcessor::processEventBuffer);

                    disconnect(m_eventProcessor, &MVMEEventProcessor::bufferProcessed,
                               m_bufferProcessor, &VMUSBBufferProcessor::addFreeBuffer);
                } break;
            case GlobalMode::ListFile:
                {
                    disconnect(m_listFileWorker, &ListFileReader::mvmeEventBufferReady,
                               m_eventProcessor, &MVMEEventProcessor::processEventBuffer);

                    disconnect(m_eventProcessor, &MVMEEventProcessor::bufferProcessed,
                               m_listFileWorker, &ListFileReader::readNextBuffer);
                } break;

            case GlobalMode::NotSet:
                break;
        }

        switch (mode)
        {
            case GlobalMode::DAQ:
                {
                    connect(m_bufferProcessor, &VMUSBBufferProcessor::mvmeEventBufferReady,
                            m_eventProcessor, &MVMEEventProcessor::processEventBuffer);

                    connect(m_eventProcessor, &MVMEEventProcessor::bufferProcessed,
                            m_bufferProcessor, &VMUSBBufferProcessor::addFreeBuffer);
                } break;
            case GlobalMode::ListFile:
                {
                    connect(m_listFileWorker, &ListFileReader::mvmeEventBufferReady,
                            m_eventProcessor, &MVMEEventProcessor::processEventBuffer);

                    connect(m_eventProcessor, &MVMEEventProcessor::bufferProcessed,
                            m_listFileWorker, &ListFileReader::readNextBuffer);
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

void MVMEContext::removeObject(QObject *object)
{
    if (m_objects.contains(object))
    {
        qDebug() << __PRETTY_FUNCTION__ << object;
        emit objectAboutToBeRemoved(object);
        m_objects.remove(object);
        object->deleteLater();
    }
}

bool MVMEContext::containsObject(QObject *object)
{
    return m_objects.contains(object);
}

void MVMEContext::prepareStart()
{
    for (ModuleConfig *module: m_config->getAllModuleConfigs())
    {
        updateHistogramCollectionDefinition(module);
    }

    for (auto hist2d: getObjects<Hist2D *>())
    {
        hist2d->clear();
    }

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

void MVMEContext::write(QJsonObject &json) const
{
    QJsonObject daqConfigObject;
    m_config->write(daqConfigObject);
    json["DAQConfig"] = daqConfigObject;

    QJsonArray histArray;

    for (auto histo: getObjects<HistogramCollection *>())
    {
        QJsonObject json;
        json["name"] = histo->objectName();
        json["channels"] = (int)histo->m_channels;
        json["resolution"] = (int)histo->m_resolution;

        QJsonObject propObject;
        for (auto name: histo->dynamicPropertyNames())
        {
           propObject[QString::fromLocal8Bit(name)] = QJsonValue::fromVariant(histo->property(name.constData()));
        }

        json["properties"] = propObject;

        histArray.append(json);
    }

    json["Histograms"] = histArray;

    QJsonArray hist2DArray;

    for (auto hist2d: getObjects<Hist2D *>())
    {
        QJsonObject json;
        json["name"] = hist2d->objectName();
        json["xAxisBits"] = (qint64)hist2d->getXBits();
        json["yAxisBits"] = (qint64)hist2d->getYBits();
        json["xAxisSource"] = QJsonValue::fromVariant(hist2d->property("Hist2D.xAxisSource"));
        json["yAxisSource"] = QJsonValue::fromVariant(hist2d->property("Hist2D.yAxisSource"));
        hist2DArray.append(json);
    }

    json["2DHistograms"] = hist2DArray;
}

void MVMEContext::read(const QJsonObject &json)
{
    for (auto obj: getObjects<HistogramCollection *>())
        removeObject(obj);

    for (auto obj: getObjects<Hist2D *>())
        removeObject(obj);

    QJsonArray histograms = json["Histograms"].toArray();

    for (int i=0; i<histograms.size(); ++i)
    {
        QJsonObject histodef = histograms[i].toObject();
        QString name = histodef["name"].toString();

        int channels   = histodef["channels"].toInt();
        int resolution = histodef["resolution"].toInt();

        if (!name.isEmpty() && channels > 0 && resolution > 0)
        {
            auto histo = new HistogramCollection(this, channels, resolution);
            histo->setObjectName(name);
            auto properties = histodef["properties"].toObject().toVariantMap();

            for (auto propName: properties.keys())
            {
                auto value = properties[propName];
                histo->setProperty(propName.toLocal8Bit().constData(), value);
            }
            addObject(histo);
        }
    }

    QJsonArray hist2DArray = json["2DHistograms"].toArray();

    for (int i=0; i<hist2DArray.size(); ++i)
    {
        QJsonObject histodef = hist2DArray[i].toObject();
        QString name = histodef["name"].toString();
        int xBits = histodef["xAxisBits"].toInt();
        int yBits = histodef["yAxisBits"].toInt();
        QString xAxisSource = histodef["xAxisSource"].toString();
        QString yAxisSource = histodef["yAxisSource"].toString();

        if (!name.isEmpty() && xBits > 0 && yBits > 0)
        {
            auto hist2d = new Hist2D(xBits, yBits, this);
            hist2d->setObjectName(name);
            hist2d->setProperty("Hist2D.xAxisSource", xAxisSource);
            hist2d->setProperty("Hist2D.yAxisSource", yAxisSource);
            addObject(hist2d);
        }
    }

    auto config = new DAQConfig;
    config->read(json["DAQConfig"].toObject());
    setConfig(config);
    setMode(GlobalMode::DAQ);
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
}

void MVMEContext::onModuleAdded(ModuleConfig *module)
{
    qDebug() << __PRETTY_FUNCTION__ << module;
    updateHistogramCollectionDefinition(module);
}

void MVMEContext::onModuleAboutToBeRemoved(ModuleConfig *module)
{
    auto predicate = [module](HistogramCollection *hist) {
        auto id = hist->property("Histogram.sourceModule").toUuid();
        return module->getId() == id;
    };

    auto histos = filterObjects<HistogramCollection *>(predicate);

    for (auto histo: histos)
        removeObject(histo);
}

void MVMEContext::updateHistogramCollectionDefinition(ModuleConfig *module)
{
    qDebug() << __PRETTY_FUNCTION__ << module;
    module->updateRegisterCache();
    int nChannels = module->getNumberOfChannels();
    int resolution = RawHistogramResolution;


    if (nChannels > 0 && resolution > 0)
    {
        auto predicate = [module](HistogramCollection *hist) {
            auto id = hist->property("Histogram.sourceModule").toUuid();
            return module->getId() == id;
        };
        auto histo = filterObjects<HistogramCollection *>(predicate).value(0, nullptr);

        if (!histo)
        {
            histo = new HistogramCollection(this, nChannels, resolution);
            histo->setProperty("Histogram.sourceModule", module->getId());
            histo->setObjectName(module->getObjectPath());
            addObject(histo);
        }
        else
        {
            histo->resize(nChannels, resolution);
        }
    }
}

QFuture<vme_script::ResultList>
MVMEContext::runScript(const vme_script::VMEScript &script, vme_script::LoggerFun logger, bool logEachResult)
{
#if 0
    auto daqState = getDAQState();

    if (daqState != DAQState::Idle && daqState != DAQState::Paused)
    {
        m_readoutWorker->
    }
#endif

    return QFuture<vme_script::ResultList>();
}
