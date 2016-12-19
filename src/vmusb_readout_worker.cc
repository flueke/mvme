#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "vmusb.h"
#include "CVMUSBReadoutList.h"
#include <QCoreApplication>
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
        setError("VMUSB controller required");
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

        // FIXME: test code; remove once done
        //u32 scalerPeriodSeconds = 4;
        //daqSettings |= ((scalerPeriodSeconds / 2) << DaqSettingsRegister::ScalerReadoutPerdiodShift);
        // FIXME: end of test code
        error = vmusb->setDaqSettings(daqSettings);

        if (error.isError())
            throw QString("Setting DaqSettings register failed: %1").arg(error.toString());

        //
        // Global Mode Register
        //
        int globalMode = 0;
        globalMode |= (1 << GlobalModeRegister::MixedBufferShift);
        //globalMode |= (1 << GlobalModeRegister::ForceScalerDumpShift);

        error = vmusb->setMode(globalMode);
        if (error.isError())
            throw QString("Setting VMUSB global mode failed: %1").arg(error.toString());

        //
        // EventsPerBuffer - This only has an effect if BuffOpt=9 in GlobalModeRegister
        //
#if 0
        static const u32 eventsPerBuffer = 3;
        error = vmusb->setEventsPerBuffer(eventsPerBuffer);

        if (error.isError())
            throw QString("Setting VMUSB EventsPerBuffer failed: %1").arg(error.toString());
#endif
        

        //
        // USB Bulk Transfer Setup Register
        //
        u32 bulkTransfer = 0;

        // FIXME: test code; remove once done
        //static const int usbBulkTimeoutSecs = 1; // resulting register value is usbBulkTimeoutSecs - 1 (0 == 1s)
        //static const int usbBulkNumberOfBuffers = 200;

        //u32 bulkTransfer = (usbBulkNumberOfBuffers | (
        //        ((usbBulkTimeoutSecs - 1) << TransferSetupRegister::timeoutShift) & TransferSetupRegister::timeoutMask));
        // FIXME: end of test code

        qDebug() << "setting bulkTransfer to" << QString().sprintf("0x%08x", bulkTransfer);
        
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
        setError(message);
        errorThrown = true;
    }
    catch (const QString &message)
    {
        setError(message);
        errorThrown = true;
    }
    catch (const std::runtime_error &e)
    {
        setError(e.what());
        errorThrown = true;
    }
    catch (const vme_script::ParseError &)
    {
        setError(QSL("VME Script parse error"));
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

static const int leaveDaqReadTimeout = 100;
static const int daqReadTimeout = 2000;

void VMUSBReadoutWorker::readoutLoop()
{
    auto vmusb = m_vmusb;
    auto error = vmusb->enterDaqMode();

    if (error.isError())
        throw QString("Error entering VMUSB DAQ mode: %1").arg(error.toString());

    setState(DAQState::Running);

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

            while (readBuffer(leaveDaqReadTimeout) > 0);
            setState(DAQState::Paused);
            emit logMessage(QSL("VMUSB readout paused"));
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            auto error = vmusb->enterDaqMode();
            if (error.isError())
                throw QString("Error entering VMUSB DAQ mode: %1").arg(error.toString());

            setState(DAQState::Running);
            emit logMessage(QSL("VMUSB readout resumed"));
        }
        else if (m_desiredState == DAQState::Stopping)
        {
            break;
        }
        else if (m_state == DAQState::Running)
        {
            int bytesRead = readBuffer(daqReadTimeout);

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

    while (readBuffer(leaveDaqReadTimeout) > 0);
}

void VMUSBReadoutWorker::setState(DAQState state)
{
    qDebug() << __PRETTY_FUNCTION__ << DAQStateStrings[m_state] << "->" << DAQStateStrings[state];
    m_state = state;
    m_desiredState = state;
    emit stateChanged(state);
}

// TODO: rename to logError() as it does not do anything else right now
void VMUSBReadoutWorker::setError(const QString &message)
{
    emit logMessage(QString("VMUSB Error: %1").arg(message));
    //setState(DAQState::Idle);
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
