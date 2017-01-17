#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "vmusb.h"
#include "CVMUSBReadoutList.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <memory>
#include <functional>

using namespace vmusb_constants;
using namespace vme_script;

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

    clearError();

    auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());
    if (!vmusb)
    {
        logError("VMUSB controller required");
        return;
    }

    m_vmusb = vmusb;

    m_cyclesToRun = cycles;
    setState(DAQState::Starting);
    DAQStats &stats(m_context->getDAQStats());
    bool errorThrown = false;
    auto daqConfig = m_context->getDAQConfig();
    VMEError error;

    m_bufferProcessor->setLogBuffers(cycles == 1);

    try
    {
        emit logMessage(QSL("VMUSB readout starting"));

        //
        // Reset IRQs
        //
        for (int i = StackIDMin; i <= StackIDMax; ++i)
        {
            error = vmusb->setIrq(i, 0);
            if (error.isError())
                throw QString("Resetting IRQ vectors failed: %1").arg(error.toString());
        }

        //
        // DAQ Settings Register
        //
        u32 daqSettings = 0;

        error = vmusb->setDaqSettings(daqSettings);

        if (error.isError())
            throw QString("Setting DaqSettings register failed: %1").arg(error.toString());

        //
        // Global Mode Register
        //
        int globalMode = 0;
        globalMode |= (1 << GlobalModeRegister::MixedBufferShift);
        globalMode |= GlobalModeRegister::WatchDog250; // 250ms watchdog
        //globalMode |= GlobalModeRegister::NoIRQHandshake;

        error = vmusb->setMode(globalMode);
        if (error.isError())
            throw QString("Setting VMUSB global mode failed: %1").arg(error.toString());

        //
        // USB Bulk Transfer Setup Register
        //
        u32 bulkTransfer = 0;

        error = vmusb->setUsbSettings(bulkTransfer);
        if (error.isError())
            throw QString("Setting VMUSB Bulk Transfer Register failed: %1").arg(error.toString());

        //
        // Generate and load VMUSB stacks
        //
        m_vmusbStack.resetLoadOffset(); // reset the static load offset
        int nextStackID = 2; // start at ID=2 as NIM=0 and scaler=1 (fixed)

        for (auto event: daqConfig->eventConfigs)
        {
            qDebug() << "daq event" << event->objectName();

            m_vmusbStack = VMUSBStack();
            m_vmusbStack.triggerCondition = event->triggerCondition;
            m_vmusbStack.irqLevel = event->irqLevel;
            m_vmusbStack.irqVector = event->irqVector;
            m_vmusbStack.scalerReadoutPeriod = event->scalerReadoutPeriod;
            m_vmusbStack.scalerReadoutFrequency = event->scalerReadoutFrequency;

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

            qDebug() << "event " << event->objectName() << " -> stackID =" << event->stackID;

            VMEScript readoutScript;

            readoutScript += event->vmeScripts["readout_start"]->getScript();

            for (auto module: event->modules)
            {
                readoutScript += module->vmeScripts["readout"]->getScript(module->getBaseAddress());
                Command marker;
                marker.type = CommandType::Marker;
                marker.value = EndMarker;
                readoutScript += marker;
            }

            readoutScript += event->vmeScripts["readout_end"]->getScript();

            CVMUSBReadoutList readoutList(readoutScript);
            m_vmusbStack.setContents(QVector<u32>::fromStdVector(readoutList.get()));

            if (m_vmusbStack.getContents().size())
            {
                emit logMessage(QString("Loading readout stack for event \"%1\""
                                   ", stack id = %2, size= %4, load offset = %3")
                           .arg(event->objectName())
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

                error = m_vmusbStack.loadStack(vmusb);
                if (error.isError())
                    throw QString("Error loading readout stack: %1").arg(error.toString());

                error = m_vmusbStack.enableStack(vmusb);
                if (error.isError())
                    throw QString("Error enabling readout stack: %1").arg(error.toString());
            }
            else
            {
                emit logMessage(QString("Empty readout stack for event \"%1\".")
                                .arg(event->objectName())
                               );
            }
        }

        //
        // DAQ Init
        //

        //using namespace std::placeholders;
        //vme_script::LoggerFun logger = std::bind(&VMUSBReadoutWorker::logMessage, this, _1);

        auto startScripts = daqConfig->vmeScriptLists["daq_start"];
        if (!startScripts.isEmpty())
        {
            emit logMessage(QSL("Global DAQ Start scripts:"));
            for (auto script: startScripts)
            {
                emit logMessage(QString("  %1").arg(script->objectName()));
                auto indentingLogger = [this](const QString &str) { this->logMessage(QSL("    ") + str); };
                run_script(vmusb, script->getScript(), indentingLogger, true);
            }
        }

        emit logMessage(QSL("\nInitializing Modules:"));
        for (auto event: daqConfig->eventConfigs)
        {
            for (auto module: event->modules)
            {
                emit logMessage(QString("  %1.%2")
                                .arg(event->objectName())
                                .arg(module->objectName())
                                );

                QVector<VMEScriptConfig *> scripts = {
                    module->vmeScripts["reset"],
                    module->vmeScripts["parameters"],
                    module->vmeScripts["readout_settings"]
                };

                for (auto scriptConfig: scripts)
                {
                    emit logMessage(QSL("    %1")
                                    .arg(scriptConfig->objectName())
                                    //.arg(event->objectName())
                                    //.arg(module->objectName())
                                    );

                    auto indentingLogger = [this](const QString &str) { this->logMessage(QSL("      ") + str); };
                    run_script(vmusb, scriptConfig->getScript(module->getBaseAddress()), indentingLogger, true);
                }
            }
        }

        emit logMessage(QSL("Events DAQ Start"));
        for (auto event: daqConfig->eventConfigs)
        {
            emit logMessage(QString("  %1").arg(event->objectName()));
            auto indentingLogger = [this](const QString &str) { this->logMessage(QSL("    ") + str); };
            run_script(vmusb, event->vmeScripts["daq_start"]->getScript(), indentingLogger, true);
        }


        //
        // Debug Dump of all VMUSB registers
        //
        emit logMessage(QSL(""));
        dump_registers(vmusb, [this] (const QString &line) { this->logMessage(line); });

        //
        // Readout
        //
        m_bufferProcessor->beginRun();
        emit logMessage(QSL(""));
        emit logMessage(QSL("Entering readout loop"));
        stats.start();

        readoutLoop();

        stats.stop();
        emit logMessage(QSL(""));
        emit logMessage(QSL("Leaving readout loop"));
        emit logMessage(QSL(""));
        m_bufferProcessor->endRun();

        //
        // DAQ Stop
        //
        emit logMessage(QSL("Events DAQ Stop"));
        for (auto event: daqConfig->eventConfigs)
        {
            emit logMessage(QString("  %1").arg(event->objectName()));
            auto indentingLogger = [this](const QString &str) { this->logMessage(QSL("    ") + str); };
            run_script(vmusb, event->vmeScripts["daq_stop"]->getScript(), indentingLogger, true);
        }

        auto stopScripts = daqConfig->vmeScriptLists["daq_stop"];
        if (!stopScripts.isEmpty())
        {
            emit logMessage(QSL("Global DAQ Stop scripts:"));
            for (auto script: stopScripts)
            {
                emit logMessage(QString("  %1").arg(script->objectName()));
                auto indentingLogger = [this](const QString &str) { this->logMessage(QSL("    ") + str); };
                run_script(vmusb, script->getScript(), indentingLogger, true);
            }
        }
    }
    catch (const char *message)
    {
        logError(message);
        errorThrown = true;
    }
    catch (const QString &message)
    {
        logError(message);
        errorThrown = true;
    }
    catch (const std::runtime_error &e)
    {
        logError(e.what());
        errorThrown = true;
    }
    catch (const vme_script::ParseError &)
    {
        logError(QSL("VME Script parse error"));
        errorThrown = true;
    }

    if (errorThrown)
    {
        try
        {
            if (vmusb->isInDaqMode())
                vmusb->leaveDaqMode();
        }
        catch (...)
        {}
    }

    setState(DAQState::Idle);
}

void VMUSBReadoutWorker::stop()
{
    if (!(m_state == DAQState::Running || m_state == DAQState::Paused))
        return;

    m_desiredState = DAQState::Stopping;
}

void VMUSBReadoutWorker::pause()
{
    if (m_state == DAQState::Running)
        m_desiredState = DAQState::Paused;
}

void VMUSBReadoutWorker::resume()
{
    if (m_state == DAQState::Paused)
        m_desiredState = DAQState::Running;
}

static const int leaveDaqReadTimeout_ms = 100;
static const int daqReadTimeout_ms = 500; // This should be higher than the watchdog timeout.

void VMUSBReadoutWorker::readoutLoop()
{
    auto vmusb = m_vmusb;
    auto error = vmusb->enterDaqMode();

    if (error.isError())
        throw QString("Error entering VMUSB DAQ mode: %1").arg(error.toString());

    setState(DAQState::Running);

    /* According to Jan we need to wait at least one millisecond
     * after entering DAQ mode to make sure that the VMUSB is
     * ready. */
    QThread::msleep(1);

    DAQStats &stats(m_context->getDAQStats());
    QTime logReadErrorTimer;

    while (true)
    {
        processQtEvents();

        // pause
        if (m_state == DAQState::Running && m_desiredState == DAQState::Paused)
        {
            //QThread::msleep(5000);
            auto error = vmusb->leaveDaqMode();
            if (error.isError())
                throw QString("Error leaving VMUSB DAQ mode: %1").arg(error.toString());

            while (readBuffer(leaveDaqReadTimeout_ms) > 0);
            setState(DAQState::Paused);
            emit logMessage(QSL("VMUSB readout paused"));
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            error = vmusb->enterDaqMode();
            if (error.isError())
                throw QString("Error entering VMUSB DAQ mode: %1").arg(error.toString());

            setState(DAQState::Running);
            emit logMessage(QSL("VMUSB readout resumed"));
        }
        // stop
        else if (m_desiredState == DAQState::Stopping)
        {
            break;
        }
        // stay in running state
        else if (m_state == DAQState::Running)
        {
            int bytesRead = readBuffer(daqReadTimeout_ms);

            /* XXX: Begin hack:
             * A timeout here can mean that either there is an error when
             * communicating with the vmusb or that no data is available. The
             * second case can happen if the module sends no or very little
             * data so that the internal buffer of the controller does not fill
             * up fast enough. To avoid the second case a smaller buffer size
             * could be chosen but that will negatively impact performance for
             * high data rates. Another method would be to use VMUSBs watchdog
             * feature but that does not seem to work.
             *
             * Now testing another method: when getting a read timeout leave
             * DAQ mode which forces the controller to dump its buffer, then
             * resume DAQ mode. If we still don't receive data after this there
             * is a communication error, otherwise the data rate was just too
             * low to fill the buffer and we continue on.
             *
             * Since firmware version 0A03_010917 there is a new watchdog
             * feature, different from the one in the documentation for version
             * 0A00. It does not use the USB Bulk Transfer Setup Register but
             * the Global Mode Register. The workaround here is left active to
             * work with older firmware versions. As long as the
             * daqReadTimeout_ms is higher than the watchdog timeout the
             * watchdog will be activated if it is available. */
#define USE_DAQMODE_HACK
#ifdef USE_DAQMODE_HACK
            if (bytesRead <= 0)
            {
                error = vmusb->leaveDaqMode();
                if (error.isError())
                    throw QString("Error leaving VMUSB DAQ mode (in timeout handling): %1").arg(error.toString());

                /* This timeout can be very small as leaving DAQ mode forces a buffer dump. */
                static const int daqModeHackTimeout = 10;
                bytesRead = readBuffer(daqModeHackTimeout);

                /* According to Jan we need to wait at least one millisecond
                 * after entering DAQ mode to make sure that the VMUSB is
                 * ready. */
                QThread::msleep(1);

                error = vmusb->enterDaqMode();
                if (error.isError())
                    throw QString("Error entering VMUSB DAQ mode (in timeout handling): %1").arg(error.toString());
            }
#endif

            if (bytesRead <= 0)
            {
                if (!logReadErrorTimer.isValid() || logReadErrorTimer.elapsed() >= 1000)
                {
                    emit logMessage(QString("VMUSB Warning: no data from bulk read (error=\"%1\", code=%2)")
                                    .arg(strerror(-bytesRead))
                                    .arg(bytesRead));
                    logReadErrorTimer.restart();
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
        else if (m_state == DAQState::Paused)
        {
            processQtEvents(QEventLoop::WaitForMoreEvents);
        }
        else
        {
            Q_ASSERT(!"Unhandled case in vmusb readoutLoop");
        }
    }

    setState(DAQState::Stopping);
    processQtEvents();

    qDebug() << __PRETTY_FUNCTION__ << "left readoutLoop, reading remaining data";
    error = vmusb->leaveDaqMode();
    if (error.isError())
        throw QString("Error leaving VMUSB DAQ mode: %1").arg(error.toString());

    while (readBuffer(leaveDaqReadTimeout_ms) > 0);
}

void VMUSBReadoutWorker::setState(DAQState state)
{
    qDebug() << __PRETTY_FUNCTION__ << DAQStateStrings[m_state] << "->" << DAQStateStrings[state];
    m_state = state;
    m_desiredState = state;
    emit stateChanged(state);
}

void VMUSBReadoutWorker::logError(const QString &message)
{
    emit logMessage(QString("VMUSB Error: %1").arg(message));
}


int VMUSBReadoutWorker::readBuffer(int timeout_ms)
{
    m_readBuffer->used = 0;

    int bytesRead = m_vmusb->bulkRead(m_readBuffer->data, m_readBuffer->size, timeout_ms);

    if (bytesRead > 0)
    {
        m_readBuffer->used = bytesRead;
        DAQStats &stats(m_context->getDAQStats());
        stats.addBuffersRead(1);
        stats.addBytesRead(bytesRead);

        const double alpha = 0.1;
        stats.avgReadSize = (alpha * bytesRead) + (1.0 - alpha) * stats.avgReadSize;

        if (m_bufferProcessor)
            m_bufferProcessor->processBuffer(m_readBuffer);
    }

    return bytesRead;
}
