/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "vmusb_readout_worker.h"

#include <functional>
#include <memory>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include "CVMUSBReadoutList.h"
#include "vmusb_buffer_processor.h"
#include "vmusb.h"

using namespace vmusb_constants;
using namespace vme_script;

static void processQtEvents(QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents)
{
    QCoreApplication::processEvents(flags);
}

namespace
{
    struct TriggerData
    {
        EventConfig *event;
        u8 irqLevel;
        u8 irqVector;
    };

    struct DuplicateTrigger: public std::runtime_error
    {
        DuplicateTrigger(TriggerCondition condition, TriggerData d1, TriggerData d2)
            : std::runtime_error("Duplicate VME Trigger Condition")
            , m_condition(condition)
            , m_data1(d1)
            , m_data2(d2)
        {
            Q_ASSERT(d1.event != d2.event);
        }

        QString toString() const
        {
            QString result(QSL("Duplicate Trigger Condition: "));

            if (m_condition == TriggerCondition::Interrupt)
            {
                result += QString("trigger=%1, level=%2, vector=%3, event1=\"%4\", event2=\"%5\"")
                    .arg(TriggerConditionNames.value(m_condition))
                    .arg(static_cast<u32>(m_data1.irqLevel))
                    .arg(static_cast<u32>(m_data1.irqVector))
                    .arg(m_data1.event->objectName())
                    .arg(m_data2.event->objectName())
                    ;
            }
            else
            {
                result += QString("trigger=%1, event1=\"%4\", event2=\"%5\"")
                    .arg(TriggerConditionNames.value(m_condition))
                    .arg(m_data1.event->objectName())
                    .arg(m_data2.event->objectName())
                    ;
            }

            return result;
        }

        TriggerCondition m_condition;
        TriggerData m_data1,
                    m_data2;
    };

    static void validate_vme_config(VMEConfig *vmeConfig)
    {
        QMultiMap<TriggerCondition, TriggerData> triggers;

        for (auto event: vmeConfig->getEventConfigs())
        {
            TriggerData data = {event, event->irqLevel, event->irqVector};
            TriggerCondition condition = event->triggerCondition;

            if (triggers.contains(condition))
            {
                auto otherDataList = triggers.values(condition);

                if (condition == TriggerCondition::Interrupt)
                {
                    for (auto otherData: otherDataList)
                    {
                        if (data.irqLevel == otherData.irqLevel
                            && data.irqVector == otherData.irqVector)
                        {
                            throw DuplicateTrigger(condition, data, otherData);
                        }
                    }
                }
                else
                {
                    throw DuplicateTrigger(condition, data, otherDataList.at(0));
                }
            }

            triggers.insert(condition, data);
        }
    }
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
    auto daqConfig = m_context->getVMEConfig();
    VMEError error;

    // Decide whether to log buffer contents.
    m_bufferProcessor->setLogBuffers(cycles == 1);

    try
    {
        logMessage(QString(QSL("VMUSB readout starting on %1"))
                   .arg(QDateTime::currentDateTime().toString())
                   );

        validate_vme_config(daqConfig); // throws on error

        //
        // Read and log firmware version
        //
        {
            u32 fwReg;
            error = vmusb->readRegister(FIDRegister, &fwReg);
            if (error.isError())
                throw QString("Error reading VMUSB firmware version: %1").arg(error.toString());

            u32 fwMajor = (fwReg & 0xFFFF);
            u32 fwMinor = ((fwReg >> 16) &  0xFFFF);

            logMessage(QString(QSL("VMUSB Firmware Version %1_%2\n"))
                       .arg(fwMajor, 4, 16, QLatin1Char('0'))
                       .arg(fwMinor, 4, 16, QLatin1Char('0'))
                      );
        }

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
                readoutScript += module->getReadoutScript()->getScript(module->getBaseAddress());
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
                logMessage(QString("Loading readout stack for event \"%1\""
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
                        logMessage(tmp);
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
                logMessage(QString("Empty readout stack for event \"%1\".")
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
            logMessage(QSL(""));
            logMessage(QSL("Global DAQ Start scripts:"));
            for (auto script: startScripts)
            {
                logMessage(QString("  %1").arg(script->objectName()));
                auto indentingLogger = [this](const QString &str) { this->logMessage(QSL("    ") + str); };
                run_script(vmusb, script->getScript(), indentingLogger, true);
            }
        }

        logMessage(QSL(""));
        logMessage(QSL("Initializing Modules:"));
        for (auto event: daqConfig->eventConfigs)
        {
            for (auto module: event->modules)
            {
                logMessage(QString("  %1.%2")
                                .arg(event->objectName())
                                .arg(module->objectName())
                                );

                QVector<VMEScriptConfig *> scripts;
                scripts.push_back(module->getResetScript());
                scripts.append(module->getInitScripts());

                for (auto scriptConfig: scripts)
                {
                    logMessage(QSL("    %1").arg(scriptConfig->objectName()));
                    auto indentingLogger = [this](const QString &str) { this->logMessage(QSL("      ") + str); };
                    run_script(vmusb, scriptConfig->getScript(module->getBaseAddress()), indentingLogger, true);
                }
            }
        }

        logMessage(QSL("Events DAQ Start"));
        for (auto event: daqConfig->eventConfigs)
        {
            logMessage(QString("  %1").arg(event->objectName()));
            auto indentingLogger = [this](const QString &str) { this->logMessage(QSL("    ") + str); };
            run_script(vmusb, event->vmeScripts["daq_start"]->getScript(), indentingLogger, true);
        }


        //
        // Debug Dump of all VMUSB registers
        //
        logMessage(QSL(""));
        dump_registers(vmusb, [this] (const QString &line) { this->logMessage(line); });

        //
        // Readout
        //
        m_bufferProcessor->beginRun();
        logMessage(QSL(""));
        logMessage(QSL("Entering readout loop"));
        stats.start();

        readoutLoop();

        stats.stop();
        logMessage(QSL("Leaving readout loop"));
        logMessage(QSL(""));

        //
        // DAQ Stop
        //
        logMessage(QSL("Events DAQ Stop"));
        for (auto event: daqConfig->eventConfigs)
        {
            logMessage(QString("  %1").arg(event->objectName()));
            auto indentingLogger = [this](const QString &str) { this->logMessage(QSL("    ") + str); };
            run_script(vmusb, event->vmeScripts["daq_stop"]->getScript(), indentingLogger, true);
        }

        auto stopScripts = daqConfig->vmeScriptLists["daq_stop"];
        if (!stopScripts.isEmpty())
        {
            logMessage(QSL("Global DAQ Stop scripts:"));
            for (auto script: stopScripts)
            {
                logMessage(QString("  %1").arg(script->objectName()));
                auto indentingLogger = [this](const QString &str) { this->logMessage(QSL("    ") + str); };
                run_script(vmusb, script->getScript(), indentingLogger, true);
            }
        }

        m_bufferProcessor->endRun();

        logMessage(QString(QSL("VMUSB readout stopped on %1"))
                   .arg(QDateTime::currentDateTime().toString())
                   );
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
    catch (const DuplicateTrigger &e)
    {
        logError(e.toString());
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
    emit daqStopped();
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

static const int leaveDaqReadTimeout_ms = 500;
static const int daqReadTimeout_ms = 500; // This should be higher than the watchdog timeout which is set to 250ms.
static const int daqModeHackTimeout_ms = leaveDaqReadTimeout_ms;

/* According to Jan we need to wait at least one millisecond
 * after entering DAQ mode to make sure that the VMUSB is
 * ready.
 * Trying to see if upping this value will make the USE_DAQMODE_HACK more stable.
 * This seems to fix the problems under 32bit WinXP.
 * */
static const int PostEnterDaqModeDelay_ms = 100;
static const int PostLeaveDaqModeDelay_ms = 100;

static VMEError enter_daq_mode(VMUSB *vmusb)
{
    auto result = vmusb->enterDaqMode();

    if (!result.isError())
    {
        QThread::msleep(PostEnterDaqModeDelay_ms);
    }

    return result;
}

static VMEError leave_daq_mode(VMUSB *vmusb)
{
    auto result = vmusb->leaveDaqMode();

    if (!result.isError())
    {
        QThread::msleep(PostLeaveDaqModeDelay_ms);
    }

    return result;
}

void VMUSBReadoutWorker::readoutLoop()
{
    auto vmusb = m_vmusb;
    auto error = enter_daq_mode(vmusb);

    if (error.isError())
        throw QString("Error entering VMUSB DAQ mode: %1").arg(error.toString());

    setState(DAQState::Running);

    DAQStats &stats(m_context->getDAQStats());
    QTime logReadErrorTimer;
    size_t nReadErrors = 0;

    while (true)
    {
        processQtEvents();

        // pause
        if (m_state == DAQState::Running && m_desiredState == DAQState::Paused)
        {
            error = leave_daq_mode(vmusb);
            if (error.isError())
                throw QString("Error leaving VMUSB DAQ mode: %1").arg(error.toString());

            while (readBuffer(leaveDaqReadTimeout_ms).bytesRead > 0);
            setState(DAQState::Paused);
            logMessage(QSL("VMUSB readout paused"));
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            error = enter_daq_mode(vmusb);
            if (error.isError())
                throw QString("Error entering VMUSB DAQ mode: %1").arg(error.toString());

            setState(DAQState::Running);
            logMessage(QSL("VMUSB readout resumed"));
        }
        // stop
        else if (m_desiredState == DAQState::Stopping)
        {
            logMessage(QSL("VMUSB readout stopping"));
            break;
        }
        // stay in running state
        else if (m_state == DAQState::Running)
        {
            auto readResult = readBuffer(daqReadTimeout_ms);

            /* XXX: Begin hack:
             * A timeout fro readBuffer() here can mean that either there was
             * an error when communicating with the vmusb or that no data is
             * available. The second case can happen if the module sends no or
             * very little data so that the internal buffer of the controller
             * does not fill up fast enough. To avoid this case a smaller
             * buffer size could be chosen but that will negatively impact
             * performance for high data rates. Another method would be to use
             * VMUSBs watchdog feature but that was never implemented despite
             * what the documentation says.
             *
             * The workaround: when getting a read timeout is to leave DAQ
             * mode, which forces the controller to dump its buffer, and to
             * then resume DAQ mode.
             *
             * If we still don't receive data after this there is a
             * communication error, otherwise the data rate was just too low to
             * fill the buffer and we continue on.
             *
             * Since firmware version 0A03_010917 there is a new watchdog
             * feature, different from the one in the documentation for version
             * 0A00. It does not use the USB Bulk Transfer Setup Register but
             * the Global Mode Register. The workaround here is left active to
             * work with older firmware versions. As long as daqReadTimeout_ms
             * is higher than the watchdog timeout the watchdog will be
             * activated if it is available. */
#define USE_DAQMODE_HACK
#ifdef USE_DAQMODE_HACK
            if (readResult.error.isTimeout() && readResult.bytesRead <= 0)
            {
                qDebug() << "begin USE_DAQMODE_HACK";
                error = leave_daq_mode(vmusb);
                if (error.isError())
                    throw QString("Error leaving VMUSB DAQ mode (in timeout handling): %1").arg(error.toString());

                readResult = readBuffer(daqModeHackTimeout_ms);

                error = enter_daq_mode(vmusb);
                if (error.isError())
                    throw QString("Error entering VMUSB DAQ mode (in timeout handling): %1").arg(error.toString());
                qDebug() << "end USE_DAQMODE_HACK";
            }
#endif

            if (readResult.bytesRead <= 0)
            {
                static const int LogReadErrorTimer_ms = 5000;
                ++nReadErrors;
                if (!logReadErrorTimer.isValid() || logReadErrorTimer.elapsed() >= LogReadErrorTimer_ms)
                {
                    logMessage(QString("VMUSB Warning: error from bulk read: %1, bytesReceived=%2 (total #readErrors=%3)")
                               .arg(readResult.error.toString())
                               .arg(readResult.bytesRead)
                               .arg(nReadErrors)
                               );
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
    error = leave_daq_mode(vmusb);
    if (error.isError())
        throw QString("Error leaving VMUSB DAQ mode: %1").arg(error.toString());

    while (readBuffer(leaveDaqReadTimeout_ms).bytesRead > 0);
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
    logMessage(QString("VMUSB Error: %1").arg(message));
}

void VMUSBReadoutWorker::logMessage(const QString &message)
{
    m_context->logMessage(message);
}

VMUSBReadoutWorker::ReadBufferResult VMUSBReadoutWorker::readBuffer(int timeout_ms)
{
    ReadBufferResult result = {};

    m_readBuffer->used = 0;

    result.error = m_vmusb->bulkRead(m_readBuffer->data, m_readBuffer->size, &result.bytesRead, timeout_ms);

    if (result.error.isError())
    {
        qDebug() << __PRETTY_FUNCTION__
            << "vmusb bulkRead result: " << result.error.toString()
            << "bytesRead =" << result.bytesRead;
    }

    if ((!result.error.isError() || result.error.isTimeout()) && result.bytesRead > 0)
    {
        m_readBuffer->used = result.bytesRead;
        DAQStats &stats(m_context->getDAQStats());
        stats.addBuffersRead(1);
        stats.addBytesRead(result.bytesRead);

        const double alpha = 0.1;
        stats.avgReadSize = (alpha * result.bytesRead) + (1.0 - alpha) * stats.avgReadSize;

        if (m_bufferProcessor)
            m_bufferProcessor->processBuffer(m_readBuffer);
    }

    return result;
}
