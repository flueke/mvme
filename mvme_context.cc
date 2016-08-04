#include "mvme_context.h"
#include "vme_module.h"
#include "vmusb.h"
#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "mvme_event_processor.h"

#include <QtConcurrent>
#include <QTimer>
#include <QThread>

MVMEContext::MVMEContext(mvme *mainwin, QObject *parent)
    : QObject(parent)
    , m_config(new DAQConfig)
    , m_ctrlOpenTimer(new QTimer(this))
    , m_readoutThread(new QThread(this))
    , m_readoutWorker(new VMUSBReadoutWorker(this))
    , m_bufferProcessor(new VMUSBBufferProcessor(this))
    , m_eventProcessorThread(new QThread(this))
    , m_eventProcessor(new MVMEEventProcessor(this))
    , m_mainwin(mainwin)
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

    m_readoutThread->start();

    connect(m_readoutWorker, &VMUSBReadoutWorker::stateChanged, this, &MVMEContext::daqStateChanged);

    m_eventProcessorThread->setObjectName("EventProcessorThread");
    m_eventProcessor->moveToThread(m_eventProcessorThread);
    m_eventProcessorThread->start();

    connect(m_bufferProcessor, &VMUSBBufferProcessor::mvmeEventBufferReady,
            m_eventProcessor, &MVMEEventProcessor::processEventBuffer);

    connect(m_eventProcessor, &MVMEEventProcessor::bufferProcessed,
            m_bufferProcessor, &VMUSBBufferProcessor::addFreeBuffer);
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
    delete m_readoutWorker;
    delete m_bufferProcessor;
    delete m_config;
}

void MVMEContext::setConfig(DAQConfig *config)
{
    delete m_config;
    m_config = config;
    emit configChanged();
}

void MVMEContext::addModule(EventConfig *eventConfig, ModuleConfig *module)
{
    module->event = eventConfig;
    eventConfig->modules.push_back(module);
    emit moduleAdded(eventConfig, module);
}

void MVMEContext::addEventConfig(EventConfig *eventConfig)
{
    m_config->eventConfigs.push_back(eventConfig);
    emit eventConfigAdded(eventConfig);

    for (auto module: eventConfig->modules)
    {
        emit moduleAdded(eventConfig, module);
    }
}

void MVMEContext::removeEvent(EventConfig *event)
{
    if (m_config->eventConfigs.removeOne(event))
    {
        for (auto module: event->modules)
        {
            emit moduleAboutToBeRemoved(module);
        }
        emit eventConfigAboutToBeRemoved(event);
        delete event;
        notifyConfigModified();
    }
}

void MVMEContext::removeModule(ModuleConfig *module)
{
    for (EventConfig *event: m_config->eventConfigs)
    {
        if (event->modules.removeOne(module))
        {
            emit moduleAboutToBeRemoved(module);
            delete module;
            notifyConfigModified();
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
