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

    auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());
    if (!vmusb)
    {
        setError("VMUSB controller required");
        return;
    }

    m_cyclesToRun = cycles;
    setState(DAQState::Starting);
    DAQStats &stats(m_context->getDAQStats());
    stats = DAQStats();

    try
    {
        m_vmusbStack.resetLoadOffset(); // reset the static load offset

        emit logMessage(QSL("VMUSB readout starting"));

        for (int i=0; i<8; ++i)
        {
            vmusb->setIrq(i, 0);
        }
        int globalMode = vmusb->getMode();
        globalMode |= (1 << GlobalModeRegister::MixedBufferShift);
        vmusb->setMode(globalMode);

        // TODO: use register lists instead of command list here
        VMECommandList resetCommands, initCommands, startCommands;
        RegisterList initParametersAndReadout;
        m_stopCommands = VMECommandList();

        int stackID = 2; // start at ID=2 as 0=NIM, 1=scaler

        for (auto event: m_context->getEventConfigs())
        {
            qDebug() << "daq event" << event->getName();

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
                resetCommands.append(VMECommandList::fromInitList(parseRegisterList(module->initReset), module->baseAddress));
                initCommands.append(VMECommandList::fromInitList(parseRegisterList(module->initParameters), module->baseAddress));
                initCommands.append(VMECommandList::fromInitList(parseRegisterList(module->initReadout), module->baseAddress));

                initParametersAndReadout += parseRegisterList(module->initParameters, module->baseAddress);
                initParametersAndReadout += parseRegisterList(module->initReadout, module->baseAddress);

                qDebug() << initParametersAndReadout;

                startCommands.append(VMECommandList::fromInitList(parseRegisterList(module->initStartDaq), module->baseAddress));
                m_stopCommands.append(VMECommandList::fromInitList(parseRegisterList(module->initStopDaq), module->baseAddress));

                m_vmusbStack.addModule(module);
            }

            logMessage(QString("Loading readout stack for event \"%1\""
                               ", stack id = %2, load offset = %3")
                       .arg(event->getName())
                       .arg(m_vmusbStack.getStackID())
                       .arg(VMUSBStack::loadOffset));

            {
                QString tmp;
                for (u32 line: m_vmusbStack.getContents())
                {
                    tmp.sprintf("  0x%08x", line);
                    emit logMessage(tmp);
                }
            }

            m_vmusbStack.loadStack(vmusb);
            m_vmusbStack.enableStack(vmusb);
        }

        char buffer[100];

        emit logMessage(QSL("Running reset commands"));
        emit logMessage(resetCommands.toString());
        vmusb->executeCommands(&resetCommands, buffer, sizeof(buffer));
        QThread::sleep(1);

        emit logMessage(QSL("Running module init commands"));
        emit logMessage(initCommands.toString());
        // TODO: log initParametersAndReadout instead of initCommands!
        vmusb->applyRegisterList(initParametersAndReadout, 0, 10);

        emit logMessage(QSL("Running module start commands"));
        emit logMessage(startCommands.toString());
        vmusb->executeCommands(&startCommands, buffer, sizeof(buffer));

        m_bufferProcessor->beginRun();
        emit logMessage(QSL("Entering readout loop"));
        stats.startTime = QDateTime::currentDateTime();
        readoutLoop();
        stats.endTime = QDateTime::currentDateTime();
        emit logMessage(QSL("Leaving readout loop"));
        m_bufferProcessor->endRun();
    }
    catch (const char *message)
    {
        emit logMessage(QString("Error: %1").arg(message));
        try {
            if (vmusb->isInDaqMode())
                vmusb->leaveDaqMode();
        } catch (...)
        {}
        setError(message);
    }
    catch (const QString &message)
    {
        emit logMessage(QString("Error: %1").arg(message));
        try {
            if (vmusb->isInDaqMode())
                vmusb->leaveDaqMode();
        } catch (...)
        {}
        setError(message);
    }
    catch (const std::runtime_error &e)
    {
        emit logMessage(QString("Error: %1").arg(e.what()));
        try {
            if (vmusb->isInDaqMode())
                vmusb->leaveDaqMode();
        } catch (...)
        {}
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

    auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());
    vmusb->enterDaqMode();

    int timeout_ms = 2000; // TODO: make this dynamic and dependent on the Bulk Transfer Setup Register timeout

    DAQStats &stats(m_context->getDAQStats());

    while (m_state == DAQState::Running)
    {
        processQtEvents();

        m_readBuffer->used = 0;

        int bytesRead = vmusb->bulkRead(m_readBuffer->data, m_readBuffer->size, timeout_ms);

        if (bytesRead > 0)
        {
            m_readBuffer->used = bytesRead;
            stats.totalBytesRead += bytesRead;
            stats.totalBuffersRead++;
            stats.readSize = bytesRead;
            if (m_bufferProcessor)
            {
                m_bufferProcessor->processBuffer(m_readBuffer);
            }
        }
        else
        {
            // TODO: error out after a number of successive read errors
            qDebug() << "warning: bulk read returned" << bytesRead;
            emit logMessage(QString("Error: vmusb bulk read returned %1").arg(bytesRead));
        }

        //qDebug() << "cyclesToRun =" << m_cyclesToRun << ", bytesRead =" << bytesRead;

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
            stats.totalBytesRead += bytesRead;
            stats.totalBuffersRead++;
            stats.readSize = bytesRead;
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

