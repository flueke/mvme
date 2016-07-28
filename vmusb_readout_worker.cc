#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "vmusb.h"
#include <QCoreApplication>
#include <QThread>
#include <memory>

using namespace vmusb_constants;

static void processQtEvents(QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents)
{
    QCoreApplication::processEvents(flags);
}

VMUSBReadoutWorker::VMUSBReadoutWorker(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_context(context)
    , m_readBuffer(new DataBuffer(vmusb_constants::BufferMaxSize))
{
}

VMUSBReadoutWorker::~VMUSBReadoutWorker()
{
    delete m_readBuffer;
}

void VMUSBReadoutWorker::start(quint32 cycles)
{
    if (m_state != DAQState::Idle)
        return;

    qDebug() << __PRETTY_FUNCTION__ << "cycles =" << cycles;

    m_cyclesToRun = cycles;
    setState(DAQState::Starting);

    try
    {
        auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());

        if (!vmusb) throw "VMUSB controller required";

        m_vmusbStack.resetLoadOffset(); // reset the static load offset

        qDebug() << "resetting vmusb ISVs";
        for (int i=0; i<8; ++i)
        {
            vmusb->setIrq(i, 0);
        }

        VMECommandList resetCommands, initCommands, startCommands;
        m_stopCommands = VMECommandList();

        int stackID = 2; // start at ID=2 as 0=NIM, 1=scaler

        for (auto event: m_context->getEventConfigs())
        {
            qDebug() << "daq event" << event->name;

            m_vmusbStack = VMUSBStack();
            m_vmusbStack.triggerCondition = event->triggerCondition;
            m_vmusbStack.irqLevel = event->irqLevel;
            m_vmusbStack.irqVector = event->irqVector;
            m_vmusbStack.scalerReadoutPeriod = event->scalerReadoutPeriod;
            m_vmusbStack.scalerReadoutFrequency = event->scalerReadoutFrequency;

            if (event->triggerCondition == TriggerCondition::Interrupt)
            {
                event->stackID = stackID; // record the stack id in the event structure
                m_vmusbStack.setStackID(stackID);
                ++stackID;
            }
            else
            {
                // for NIM1 and scaler triggers the stack knows the stack number
                event->stackID = m_vmusbStack.getStackID();
            }

            for (auto module: event->modules)
            {
                qDebug() << "reset module" << module->name;

                resetCommands.append(VMECommandList::fromInitList(parseInitList(module->initReset), module->baseAddress));
                initCommands.append(VMECommandList::fromInitList(parseInitList(module->initParameters), module->baseAddress));
                initCommands.append(VMECommandList::fromInitList(parseInitList(module->initReadout), module->baseAddress));
                startCommands.append(VMECommandList::fromInitList(parseInitList(module->initStartDaq), module->baseAddress));
                m_stopCommands.append(VMECommandList::fromInitList(parseInitList(module->initStopDaq), module->baseAddress));

                m_vmusbStack.addModule(module);
            }

            qDebug() << "loading and enabling stack for" << event->name;
            m_vmusbStack.loadStack(vmusb);
            m_vmusbStack.enableStack(vmusb);
        }

        char buffer[100];

        {
            QString tmp;
            QTextStream strm(&tmp);
            initCommands.dump(strm);
            qDebug() << "init" << endl << tmp << endl;
        }

        qDebug() << "running reset commands";
        vmusb->executeCommands(&resetCommands, buffer, sizeof(buffer));
        QThread::sleep(1);

        // TODO: run init commands one by one to make sure the device has enough time to do its work
        qDebug() << "running init commands";
        vmusb->executeCommands(&initCommands, buffer, sizeof(buffer));

        {
            QString tmp;
            QTextStream strm(&tmp);
            startCommands.dump(strm);
            qDebug() << "start" << endl << tmp << endl;
        }

        qDebug() << "running start commands";
        vmusb->executeCommands(&startCommands, buffer, sizeof(buffer));

        readoutLoop();
    }
    catch (const char *message)
    {
        setError(message);
    }
    catch (const std::runtime_error &e)
    {
        setError(e.what());
    }
}

void VMUSBReadoutWorker::stop()
{
    if (m_state != DAQState::Running)
        return;

    setState(DAQState::Stopping);
    processQtEvents();
}

void VMUSBReadoutWorker::readoutLoop()
{
    setState(DAQState::Running);
    m_bufferProcessor->resetRunState();

    auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());
    vmusb->enterDaqMode();

    int timeout_ms = 2000; // TODO: make this dynamic and dependent on the Bulk Transfer Setup Register timeout

    while (m_state == DAQState::Running)
    {
        processQtEvents();

        m_readBuffer->used = 0;

        int bytesRead = vmusb->bulkRead(m_readBuffer->data, m_readBuffer->size, timeout_ms);

        if (bytesRead > 0)
        {
            m_readBuffer->used = bytesRead;

            if (m_bufferProcessor)
            {
                m_bufferProcessor->processBuffer(m_readBuffer);
            }
        }
        else
        {
            // TODO: error out after a number of successive read errors
            qDebug() << "warning: bulk read returned" << bytesRead;
        }

        qDebug() << "cyclesToRun =" << m_cyclesToRun << ", bytesRead =" << bytesRead;

        if (m_cyclesToRun > 0)
        {
            if (m_cyclesToRun == 1)
            {
                qDebug() << "cycles to run reached";
                break;
            }
            --m_cyclesToRun;
        }
    }

    processQtEvents();

    qDebug() << __PRETTY_FUNCTION__ << "left readoutLoop";
    vmusb->leaveDaqMode();

    int bytesRead = 0;

    do
    {
        m_readBuffer->used = 0;

        bytesRead = vmusb->bulkRead(m_readBuffer->data, m_readBuffer->size, timeout_ms);

        if (bytesRead > 0)
        {
            m_readBuffer->used = bytesRead;
            if (m_bufferProcessor)
            {
                m_bufferProcessor->processBuffer(m_readBuffer);
            }
        }
        else
        {
            qDebug() << "bulkRead returned" << bytesRead;
        }
        processQtEvents();
    } while (bytesRead > 0);

    qDebug() << "running stop commands";
    char buffer[100];
    vmusb->executeCommands(&m_stopCommands, buffer, sizeof(buffer));

    qDebug() << "DAQ state = idle";
    setState(DAQState::Idle);
}

void VMUSBReadoutWorker::setState(DAQState state)
{
    m_state = state;
    emit stateChanged(state);
}

void VMUSBReadoutWorker::setError(const QString &message)
{
    emit error(message);
    setState(DAQState::Idle);
}

