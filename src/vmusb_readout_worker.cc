#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "vmusb.h"
#include <QCoreApplication>
#include <QThread>
#include <memory>

using namespace vmusb_constants;

static const int maxConsecutiveReadErrors = 5;

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
    Q_ASSERT(!"not implemented");
#if 0
    if (m_state != DAQState::Idle)
        return;

    clearError();

    auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());
    if (!vmusb)
    {
        setError("VMUSB controller required");
        return;
    }

    m_cyclesToRun = cycles;
    setState(DAQState::Starting);
    DAQStats &stats(m_context->getDAQStats());
    bool error = false;

    try
    {
        m_vmusbStack.resetLoadOffset(); // reset the static load offset

        emit logMessage(QSL("VMUSB readout starting"));
        int result;

        for (int i=0; i<8; ++i)
        {
            result = vmusb->setIrq(i, 0);
            if (result < 0)
                throw QString("Resetting IRQ vectors failed");
        }

        vmusb->setDaqSettings(0);

        // USB Bulk Transfer Setup Register
        // TODO: USB Bulk transfer: does not seem to have any effect...
        {
            // This sets the buffer end watchdog timeout to 1 second + timeoutValue seconds
            const int timeoutValue = 1;
            int value = timeoutValue << TransferSetupRegister::timeoutShift;
            vmusb->setUsbSettings(value);
        }

        int globalMode = vmusb->getMode();
        globalMode |= (1 << GlobalModeRegister::MixedBufferShift);
        vmusb->setMode(globalMode);

        int nextStackID = 2; // start at ID=2 as NIM=0 and scaler=1 (fixed)

        for (auto event: m_context->getEventConfigs())
        {
            qDebug() << "daq event" << event->getName();

            m_vmusbStack = VMUSBStack();
            m_vmusbStack.triggerCondition = event->triggerCondition;
            m_vmusbStack.irqLevel = event->irqLevel;
            m_vmusbStack.irqVector = event->irqVector;
            m_vmusbStack.scalerReadoutPeriod = event->scalerReadoutPeriod;
            m_vmusbStack.scalerReadoutFrequency = event->scalerReadoutFrequency;
            m_vmusbStack.readoutTriggerDelay = event->readoutTriggerDelay;

            if (event->triggerCondition == TriggerCondition::Interrupt)
            {
                event->stackID = nextStackID; // record the stack id in the event structure
                m_vmusbStack.setStackID(nextStackID);
                ++nextStackID;
            }
            else
            {
                // for NIM1 and scaler triggers the stack knows the stack number
                event->stackID = m_vmusbStack.getStackID();
            }

            qDebug() << "event " << event->getName() << " -> stackID =" << event->stackID;

            for (auto module: event->modules)
            {
                m_vmusbStack.addModule(module);
            }

            if (m_vmusbStack.getContents().size())
            {

                emit logMessage(QString("Loading readout stack for event \"%1\""
                                   ", stack id = %2, size= %4, load offset = %3")
                           .arg(event->getName())
                           .arg(m_vmusbStack.getStackID())
                           .arg(VMUSBStack::loadOffset)
                           .arg(m_vmusbStack.getContents().size())
                           );

                {
                    QString tmp;
                    for (u32 line: m_vmusbStack.getContents())
                    {
                        tmp.sprintf("  0x%08x", line);
                        emit logMessage(tmp);
                    }
                }

                result = m_vmusbStack.loadStack(vmusb);
                if (result < 0)
                    throw QString("Error loading readout stack");

                result = m_vmusbStack.enableStack(vmusb);
                if (result < 0)
                    throw QString("Error enabling readout stack");
            }
            else
            {
                emit logMessage(QString("Empty readout stack for event \"%1\".")
                                .arg(event->getName())
                               );
            }
        }

        static const int writeDelay_ms = 10;

        emit logMessage(QSL("Module Reset"));
        for (auto event: m_context->getEventConfigs())
        {
            for (auto module: event->modules)
            {
                emit logMessage(QString("  Event %1, Module %2").arg(event->getName()).arg(module->objectName()));
                auto regs = parseRegisterList(module->initReset, module->baseAddress);
                emit logMessages(toStringList(regs), QSL("    "));
                auto result = vmusb->applyRegisterList(regs, 0, writeDelay_ms,
                                                       module->getRegisterWidth(), module->getRegisterAddressModifier());

                if (result < 0)
                {
                    throw QString("Module Reset failed for %1.%2 (code=%3)")
                        .arg(event->getName())
                        .arg(module->getName())
                        .arg(result);
                }
            }
        }

        // Pause a bit after reset
        static const int postResetPause_ms = 500;
        QThread::msleep(postResetPause_ms);

        emit logMessage(QSL("Module Init"));
        for (auto event: m_context->getEventConfigs())
        {
            for (auto module: event->modules)
            {
                emit logMessage(QString("  Event %1, Module %2").arg(event->getName()).arg(module->getName()));
                auto regs = parseRegisterList(module->initParameters, module->baseAddress);
                regs += parseRegisterList(module->initReadout, module->baseAddress);
                emit logMessages(toStringList(regs), QSL("    "));
                auto result = vmusb->applyRegisterList(regs, 0, writeDelay_ms,
                                                       module->getRegisterWidth(), module->getRegisterAddressModifier());

                if (result < 0)
                {
                    throw QString("Module Init failed for %1.%2 (code=%3)")
                        .arg(event->getName())
                        .arg(module->getName())
                        .arg(result);
                }
            }
        }

        emit logMessage(QSL("Module Start DAQ"));
        for (auto event: m_context->getEventConfigs())
        {
            for (auto module: event->modules)
            {
                emit logMessage(QString("  Event %1, Module %2").arg(event->getName()).arg(module->getName()));
                auto regs = parseRegisterList(module->initStartDaq, module->baseAddress);
                emit logMessages(toStringList(regs), QSL("    "));
                auto result = vmusb->applyRegisterList(regs, 0, writeDelay_ms,
                                                       module->getRegisterWidth(), module->getRegisterAddressModifier());

                if (result < 0)
                {
                    throw QString("Module Start DAQ failed for %1.%2 (code=%3)")
                        .arg(event->getName())
                        .arg(module->getName())
                        .arg(result);
                }
            }
        }

        m_bufferProcessor->beginRun();
        emit logMessage(QSL("Entering readout loop\n"));
        stats.start();

        readoutLoop();

        stats.stop();
        emit logMessage(QSL("\nLeaving readout loop"));
        m_bufferProcessor->endRun();

        emit logMessage(QSL("Module Stop DAQ"));
        for (auto event: m_context->getEventConfigs())
        {
            for (auto module: event->modules)
            {
                emit logMessage(QString("  Event %1, Module %2").arg(event->getName()).arg(module->getName()));
                auto regs = parseRegisterList(module->initStopDaq, module->baseAddress);
                emit logMessages(toStringList(regs), QSL("    "));
                auto result = vmusb->applyRegisterList(regs, 0, writeDelay_ms,
                                                       module->getRegisterWidth(), module->getRegisterAddressModifier());

                if (result < 0)
                {
                    throw QString("Module Stop DAQ failed for %1.%2 (code=%3)")
                        .arg(event->getName())
                        .arg(module->getName())
                        .arg(result);
                }
            }
        }
    }
    catch (const char *message)
    {
        setError(message);
        error = true;
    }
    catch (const QString &message)
    {
        setError(message);
        error = true;
    }
    catch (const std::runtime_error &e)
    {
        setError(e.what());
        error = true;
    }


    setState(DAQState::Idle);

    if (error)
    {
        try
        {
            if (vmusb->isInDaqMode())
                vmusb->leaveDaqMode();
        }
        catch (...)
        {}
    }
#endif
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
    if (!vmusb->enterDaqMode())
    {
        throw QString("Error entering VMUSB DAQ mode");
    }

    int timeout_ms = 2000; // TODO: make this dynamic and dependent on the Bulk Transfer Setup Register timeout
    int consecutiveReadErrors = 0;

    DAQStats &stats(m_context->getDAQStats());

    while (m_state == DAQState::Running)
    {
        processQtEvents();

        m_readBuffer->used = 0;

        int bytesRead = vmusb->bulkRead(m_readBuffer->data, m_readBuffer->size, timeout_ms);

        if (bytesRead > 0)
        {
            m_readBuffer->used = bytesRead;
            stats.addBuffersRead(1);
            stats.addBytesRead(bytesRead);

            const double alpha = 0.1;
            stats.avgReadSize = (alpha * bytesRead) + (1.0 - alpha) * stats.avgReadSize;
            consecutiveReadErrors = 0;
            if (m_bufferProcessor)
            {
                m_bufferProcessor->processBuffer(m_readBuffer);
            }
        }
        else
        {
#if 0
            if (consecutiveReadErrors >= maxConsecutiveReadErrors)
            {
                emit logMessage(QString("VMUSB Error: %1 consecutive reads failed. Stopping DAQ.").arg(consecutiveReadErrors));
                break;
            }
            else
#endif


            {
                ++consecutiveReadErrors;
                emit logMessage(QString("VMUSB Warning: no data from bulk read (error=\"%1\", code=%2)")
                                .arg(strerror(-bytesRead))
                                .arg(bytesRead));
            }
        }

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

    qDebug() << __PRETTY_FUNCTION__ << "left readoutLoop, reading remaining data";
    vmusb->leaveDaqMode();

    int bytesRead = 0;

    do
    {
        m_readBuffer->used = 0;

        bytesRead = vmusb->bulkRead(m_readBuffer->data, m_readBuffer->size, timeout_ms);

        if (bytesRead > 0)
        {
            m_readBuffer->used = bytesRead;
            stats.addBuffersRead(1);
            stats.addBytesRead(bytesRead);
            
            const double alpha = 0.1;
            stats.avgReadSize = (alpha * bytesRead) + (1.0 - alpha) * stats.avgReadSize;
            if (m_bufferProcessor)
            {
                m_bufferProcessor->processBuffer(m_readBuffer);
            }
        }
        processQtEvents();
    } while (bytesRead > 0);
}

void VMUSBReadoutWorker::setState(DAQState state)
{
    m_state = state;
    emit stateChanged(state);
}

void VMUSBReadoutWorker::setError(const QString &message)
{
    emit logMessage(QString("VMUSB Error: %1").arg(message));
    setState(DAQState::Idle);
}

