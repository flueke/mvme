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

MVMEContext::MVMEContext(mvme *mainwin, QObject *parent)
    : QObject(parent)
    , m_config(new DAQConfig)
    , m_ctrlOpenTimer(new QTimer(this))
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

    m_ctrlOpenTimer->setInterval(100);
    m_ctrlOpenTimer->start();

    m_readoutThread->setObjectName("ReadoutThread");
    m_readoutWorker->moveToThread(m_readoutThread);
    m_bufferProcessor->moveToThread(m_readoutThread);
    m_readoutWorker->setBufferProcessor(m_bufferProcessor);
    m_listFileWorker->moveToThread(m_readoutThread);

    m_readoutThread->start();

    connect(m_readoutWorker, &VMUSBReadoutWorker::stateChanged, this, &MVMEContext::daqStateChanged);
    connect(m_readoutWorker, &VMUSBReadoutWorker::logMessage, this, &MVMEContext::logMessage);
    connect(m_bufferProcessor, &VMUSBBufferProcessor::logMessage, this, &MVMEContext::logMessage);
    connect(m_listFileWorker, &ListFileWorker::logMessage, this, &MVMEContext::logMessage);

    m_eventThread->setObjectName("EventThread");
    m_eventProcessor->moveToThread(m_eventThread);
    m_eventThread->start();

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

    int channels = module->getNumberOfChannels();
    int resolution = module->getADCResolution();

    if (channels > 0 && resolution > 0)
    {
        auto hist = new Histogram(this, channels, resolution);
        hist->setProperty("Histogram.sourceModule", module->getFullPath());
        hist->setProperty("Histogram.autoCreated", true);
        addHistogram(module->getFullPath(), hist);
    }
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

void MVMEContext::tryOpenController()
{
    auto *vmusb = dynamic_cast<VMUSB *>(m_controller);

    if (vmusb && !vmusb->isOpen() && !m_ctrlOpenFuture.isRunning())
    {
        m_ctrlOpenFuture = QtConcurrent::run(vmusb, &VMUSB::openFirstUsbDevice);
    }
}

DAQState MVMEContext::getDAQState() const
{
    return m_readoutWorker->getState();
}

void MVMEContext::setListFile(ListFile *listFile)
{
    setConfig(listFile->getDAQConfig());
    setConfigFileName(QString());
    setMode(GlobalMode::ListFile);
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

void MVMEContext::startReplay()
{
    if (m_mode != GlobalMode::ListFile || !m_listFile)
        return;

    m_listFile->seek(0);
    QMetaObject::invokeMethod(m_listFileWorker, "readNextBuffer", Qt::QueuedConnection);
}

void MVMEContext::write(QJsonObject &json) const
{
    QJsonObject daqConfigObject;
    m_config->write(daqConfigObject);

    QJsonArray histArray;

    for (auto name: m_histograms.keys())
    {
        auto histo = m_histograms[name];
        QJsonObject histObject;
        histObject["type"] = "HistCollection";
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

    json["DAQConfig"] = daqConfigObject;
    json["Histograms"] = histArray;
}

void MVMEContext::read(const QJsonObject &json)
{
    auto config = new DAQConfig;
    config->read(json["DAQConfig"].toObject());
    setConfig(config);
    setMode(GlobalMode::DAQ);

    QJsonArray histograms = json["Histograms"].toArray();

    for (int i=0; i<histograms.size(); ++i)
    {
        QJsonObject histodef = histograms[i].toObject();
        QString type = histodef["type"].toString();
        QString name = histodef["name"].toString();

        if (type == "HistCollection")
        {
            int channels   = histodef["channels"].toInt();
            int resolution = histodef["resolution"].toInt();

            if (!name.isEmpty() && channels > 0 && resolution > 0)
            {
                auto histo = new Histogram(this, channels, resolution);
                auto properties = histodef["properties"].toObject().toVariantMap();

                for (auto propName: properties.keys())
                {
                    auto value = properties[propName];
                    histo->setProperty(propName.toLocal8Bit().constData(), value);
                }
                addHistogram(name, histo);
            }
        }
    }
}
