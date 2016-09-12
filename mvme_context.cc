#include "mvme_context.h"
#include "vmusb.h"
#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "mvme_event_processor.h"
#include "mvme_listfile.h"

#include <QtConcurrent>
#include <QTimer>
#include <QThread>

static const size_t dataBufferCount = 20;
static const size_t dataBufferSize  = 27 * 1024 * 2; // double the size of a vmusb read buffer
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
    , m_listFileWorker(new ListFileWorker)
{

    for (size_t i=0; i<dataBufferCount; ++i)
    {
        m_freeBuffers.push_back(new DataBuffer(dataBufferSize));
    }

    connect(m_ctrlOpenTimer, &QTimer::timeout, this, &MVMEContext::tryOpenController);
    m_ctrlOpenTimer->setInterval(TryOpenControllerInterval_ms);
    m_ctrlOpenTimer->start();

    connect(m_logTimer, &QTimer::timeout, this, &MVMEContext::logEventProcessorCounters);
    m_logTimer->setInterval(PeriodicLoggingInterval_ms);


    m_readoutThread->setObjectName("ReadoutThread");
    m_readoutWorker->moveToThread(m_readoutThread);
    m_bufferProcessor->moveToThread(m_readoutThread);
    m_readoutWorker->setBufferProcessor(m_bufferProcessor);
    m_listFileWorker->moveToThread(m_readoutThread);

    m_readoutThread->start();

    connect(m_readoutWorker, &VMUSBReadoutWorker::stateChanged, this, &MVMEContext::onDAQStateChanged);
    connect(m_readoutWorker, &VMUSBReadoutWorker::logMessage, this, &MVMEContext::sigLogMessage);
    connect(m_readoutWorker, &VMUSBReadoutWorker::logMessages, this, [this](const QStringList &messages) {
        for (auto msg: messages)
        {
            emit sigLogMessage(msg);
        }
    });
    connect(m_bufferProcessor, &VMUSBBufferProcessor::logMessage, this, &MVMEContext::sigLogMessage);
    connect(m_listFileWorker, &ListFileWorker::logMessage, this, &MVMEContext::sigLogMessage);
    connect(m_listFileWorker, &ListFileWorker::endOfFileReached, this, &MVMEContext::onReplayDone);

    m_eventThread->setObjectName("EventProcessorThread");
    m_eventProcessor->moveToThread(m_eventThread);
    m_eventThread->start();
    connect(m_eventProcessor, &MVMEEventProcessor::logMessage, this, &MVMEContext::sigLogMessage);

    setMode(GlobalMode::DAQ);
}

MVMEContext::~MVMEContext()
{
    QMetaObject::invokeMethod(m_readoutWorker, "stop", Qt::QueuedConnection);
    while (m_readoutWorker->getState() != DAQState::Idle)
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
    delete m_config;
    m_config = config;
    emit configChanged(config);
}

void MVMEContext::addModule(EventConfig *eventConfig, ModuleConfig *module)
{
    module->event = eventConfig;
    eventConfig->modules.push_back(module);
    emit moduleAdded(eventConfig, module);
}

void MVMEContext::addEventConfig(EventConfig *eventConfig)
{
    m_config->addEventConfig(eventConfig);
    emit eventConfigAdded(eventConfig);

    for (auto module: eventConfig->modules)
    {
        emit moduleAdded(eventConfig, module);
    }
}

void MVMEContext::removeEvent(EventConfig *event)
{
    if (m_config->removeEventConfig(event))
    {
        for (auto module: event->modules)
        {
            emit moduleAboutToBeRemoved(module);
        }
        emit eventConfigAboutToBeRemoved(event);
        delete event;
        m_config->setModified();
    }
}

void MVMEContext::removeModule(ModuleConfig *module)
{
    for (EventConfig *event: m_config->getEventConfigs())
    {
        if (event->modules.removeOne(module))
        {
            emit moduleAboutToBeRemoved(module);
            delete module;
            m_config->setModified();
            break;
        }
    }
}

void MVMEContext::setController(VMEController *controller)
{
    m_controller = controller;
    emit vmeControllerSet(controller);
}

QString MVMEContext::getUniqueModuleName(const QString &prefix) const
{
    auto moduleConfigs = m_config->getAllModuleConfigs();
    QSet<QString> moduleNames;

    for (auto cfg: moduleConfigs)
    {
        if (cfg->getName().startsWith(prefix))
        {
            moduleNames.insert(cfg->getName());
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

void MVMEContext::logEventProcessorCounters()
{
    auto counters = m_eventProcessor->getCounters();

    QString buffer;
    QTextStream stream(&buffer);

    stream << endl;
    stream << "Buffers:         " << counters.buffers << endl;
    stream << "Event Sections:  " << counters.events << endl;

    for (auto it = counters.moduleCounters.begin();
         it != counters.moduleCounters.end();
         ++it)
    {
        auto mod = it.key();
        auto modCounters = it.value();

        stream << mod->getName() << endl;
        stream << "  Events:  " << modCounters.events << endl;
        stream << "  Headers: " << modCounters.headerWords << endl;
        stream << "  Data:    " << modCounters.dataWords << endl;
        stream << "  EOE:     " << modCounters.eoeWords << endl;
        //stream << "  avg event size: " << ((float)modCounters.dataWords / (float)modCounters.events) << endl;
        stream << "  data/headers: " << ((float)modCounters.dataWords / (float)modCounters.headerWords) << endl;
    }

    emit sigLogMessage(buffer);
}

void MVMEContext::onDAQStateChanged(DAQState state)
{
    emit daqStateChanged(state);

    switch (state)
    {
        case DAQState::Idle:
            {
                logEventProcessorCounters();
            } break;

        case DAQState::Starting:
            {
                //m_logTimer->start();
            } break;

        case DAQState::Running:
            break;

        case DAQState::Stopping:
            {
                //m_logTimer.stop();
            } break;
    }
}

void MVMEContext::onReplayDone()
{
    logEventProcessorCounters();
    double secondsElapsed = m_replayTime.elapsed() / 1000.0;
    qint64 replayBytes = m_listFile->getFile().size();
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
    return m_readoutWorker->getState();
}

void MVMEContext::setListFile(ListFile *listFile)
{
    read(listFile->getDAQConfig());
    setConfigFileName(QString());
    setMode(GlobalMode::ListFile);
    delete m_listFile;
    m_listFile = listFile;
    m_listFileWorker->setListFile(listFile);
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
                    disconnect(m_listFileWorker, &ListFileWorker::mvmeEventBufferReady,
                               m_eventProcessor, &MVMEEventProcessor::processEventBuffer);

                    disconnect(m_eventProcessor, &MVMEEventProcessor::bufferProcessed,
                               m_listFileWorker, &ListFileWorker::readNextBuffer);
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
                    connect(m_listFileWorker, &ListFileWorker::mvmeEventBufferReady,
                            m_eventProcessor, &MVMEEventProcessor::processEventBuffer);

                    connect(m_eventProcessor, &MVMEEventProcessor::bufferProcessed,
                            m_listFileWorker, &ListFileWorker::readNextBuffer);
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

void MVMEContext::addHist2D(Hist2D *hist2d)
{
    m_2dHistograms.append(hist2d);
    emit hist2DAdded(hist2d);
}

void MVMEContext::prepareStart()
{
    auto histograms = m_histograms.values();
    for (ModuleConfig *module: m_config->getAllModuleConfigs())
    {
        module->updateRegisterCache();

        auto findResult = std::find_if(histograms.begin(), histograms.end(),
                                       [module](HistogramCollection *hist) {
            auto id = hist->property("Histogram.sourceModule").toUuid();
            return module->getId() == id;
        });

        HistogramCollection *hist;

        int nChannels = module->getNumberOfChannels();
        int resolution = RawHistogramResolution;

        if (nChannels > 0 && resolution > 0)
        {

            if (findResult == histograms.end())
            {
                hist = new HistogramCollection(this, nChannels, resolution);
                hist->setProperty("Histogram.sourceModule", module->getId());
                addHistogram(module->getFullPath(), hist);
            }
            else
            {
                (*findResult)->resize(nChannels, resolution);
            }
        }
    }

    m_eventProcessor->newRun();
}

void MVMEContext::startReplay()
{
    if (m_mode != GlobalMode::ListFile || !m_listFile)
        return;

    prepareStart();

    QMetaObject::invokeMethod(m_listFileWorker, "startFromBeginning", Qt::QueuedConnection);
    m_replayTime.restart();
}

void MVMEContext::startDAQ(quint32 nCycles)
{
    if (m_mode != GlobalMode::DAQ)
        return;

    prepareStart();
    QMetaObject::invokeMethod(m_readoutWorker, "start",
                              Qt::QueuedConnection, Q_ARG(quint32, nCycles));
}

void MVMEContext::stopDAQ()
{
    if (m_mode != GlobalMode::DAQ)
        return;

    QMetaObject::invokeMethod(m_readoutWorker, "stop",
                              Qt::QueuedConnection);
}

void MVMEContext::write(QJsonObject &json) const
{
    QJsonObject daqConfigObject;
    m_config->write(daqConfigObject);
    json["DAQConfig"] = daqConfigObject;

    QJsonArray histArray;

    for (auto name: m_histograms.keys())
    {
        auto histo = m_histograms[name];
        QJsonObject histObject;
        histObject["name"] = name;
        histObject["channels"] = (int)histo->m_channels;
        histObject["resolution"] = (int)histo->m_resolution;

        QJsonObject propObject;
        for (auto name: histo->dynamicPropertyNames())
        {
           propObject[QString::fromLocal8Bit(name)] = QJsonValue::fromVariant(histo->property(name.constData()));
        }

        histObject["properties"] = propObject;

        histArray.append(histObject);
    }

    json["Histograms"] = histArray;

    QJsonArray hist2DArray;

    for (auto hist2d: m_2dHistograms)
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
    removeHistograms();
    remove2DHistograms();

    auto config = new DAQConfig;
    config->read(json["DAQConfig"].toObject());
    setConfig(config);
    setMode(GlobalMode::DAQ);

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
            auto properties = histodef["properties"].toObject().toVariantMap();

            for (auto propName: properties.keys())
            {
                auto value = properties[propName];
                histo->setProperty(propName.toLocal8Bit().constData(), value);
            }
            addHistogram(name, histo);
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
            addHist2D(hist2d);
        }
    }
}

void MVMEContext::logMessage(const QString &msg)
{
    emit sigLogMessage(msg);
}
