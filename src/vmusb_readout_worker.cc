/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
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
#include <QDebug>

#include "CVMUSBReadoutList.h"
#include "util/perf.h"
#include "vme_daq.h"
#include "vmusb_buffer_processor.h"
#include "vme_analysis_common.h"
#include "vmusb.h"

using namespace vmusb_constants;
using namespace vme_script;

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

    static void validate_event_readout_script(const VMEScript &script)
    {
        for (auto cmd: script)
        {
            switch (cmd.type)
            {
                case CommandType::BLT:
                case CommandType::BLTFifo:
                    if (cmd.transfers > vmusb_constants::BLTMaxTransferCount)
                        throw (QString("Maximum number of BLT transfers exceeded in '%1'").arg(to_string(cmd)));
                    break;

                case CommandType::MBLT:
                case CommandType::MBLTFifo:
                    if (cmd.transfers > vmusb_constants::MBLTMaxTransferCount)
                        throw (QString("Maximum number of MBLT transfers exceeded in '%1'").arg(to_string(cmd)));
                    break;

                default:
                    break;
            }
        }
    }
}

static const size_t MaxLogMessagesPerSecond = 5;

VMUSBReadoutWorker::VMUSBReadoutWorker(QObject *parent)
    : VMEReadoutWorker(parent)
    , m_state(DAQState::Idle)
    , m_desiredState(DAQState::Idle)
    , m_readBuffer(new DataBuffer(vmusb_constants::BufferMaxSize))
    , m_bufferProcessor(new VMUSBBufferProcessor(this))
{
}

VMUSBReadoutWorker::~VMUSBReadoutWorker()
{
    delete m_readBuffer;
}

void VMUSBReadoutWorker::pre_setContext(VMEReadoutWorkerContext newContext)
{
    m_bufferProcessor->m_freeBufferQueue = newContext.freeBuffers;
    m_bufferProcessor->m_filledBufferQueue = newContext.fullBuffers;
}

/* According to Jan we need to wait at least one millisecond
 * after entering DAQ mode to make sure that the VMUSB is
 * ready.
 * Trying to see if upping this value will make the USE_DAQMODE_HACK more stable.
 * This seems to fix the problems under 32bit WinXP.
 *
 * Note: enter_daq_mode() and leave_daq_mode() are asymmetric now:
 *
 * enter_daq_mode() sets the LEDSrcRegister and DEVSrcRegister registers prior
 * to writing the action register.
 *
 * leave_daq_mode() only writes the action register as writing registers is
 * only possible after all remaining data buffers have been read from the
 * controller. */
static const int PostEnterDaqModeDelay_ms = 100;
static const int PostLeaveDaqModeDelay_ms = 100;

static const double PauseMaxSleep_ms = 125.0;

static VMEError enter_daq_mode(VMUSB *vmusb, u32 additionalBits = 0,
                               u32 ledSources = 0,
                               std::function<void (const QString &)> logger = {})
{
    qDebug() << __PRETTY_FUNCTION__;

    VMEError result;

    result = vmusb->writeRegister(LEDSrcRegister, ledSources);
    if (result.isError())
        return result;

    result = vmusb->enterDaqMode(additionalBits);

    u32 finalValue = additionalBits | 1u; // set the DAQ mode bit

    if (logger)
    {
        logger(QString("VMUSB enter daq mode: writing 0x%1 to Action Register")
               .arg(finalValue, 4, 16, QLatin1Char('0')));
    }

    result = vmusb->writeActionRegister(finalValue);

    if (!result.isError())
    {
        QThread::msleep(PostEnterDaqModeDelay_ms);
    }

    return result;
}

static VMEError leave_daq_mode(VMUSB *vmusb, u32 additionalBits = 0,
                               std::function<void (const QString &)> logger = {})
{
    qDebug() << __PRETTY_FUNCTION__;

    auto write_action_register_and_sleep = [&vmusb, &logger] (u32 value) -> VMEError
    {
        if (logger)
        {
            logger(QString("VMUSB leave daq mode: writing 0x%1 to Action Register")
                   .arg(value, 4, 16, QLatin1Char('0')));
        }

        VMEError result = vmusb->writeActionRegister(value);

        if (!result.isError())
        {
            QThread::msleep(PostLeaveDaqModeDelay_ms);
        }

        return result;
    };

    VMEError result = {};
    u32 finalValue = additionalBits & (~1u); // clear the DAQ mode bit
    result = write_action_register_and_sleep(finalValue);

    return result;
}

void VMUSBReadoutWorker::start(quint32 cycles)
{
    if (m_state != DAQState::Idle)
        return;

    auto vmusb = qobject_cast<VMUSB *>(m_workerContext.controller);
    if (!vmusb)
    {
        logError(QSL("VMUSB controller required"));
        return;
    }

    m_vmusb = vmusb;

    m_cyclesToRun = cycles;
    setState(DAQState::Starting);
    DAQStats &stats(m_workerContext.daqStats);
    bool errorThrown = false;
    auto daqConfig = m_workerContext.vmeConfig;
    VMEError error;

    auto ctrlSettings = daqConfig->getControllerSettings();

    // Decide whether to log buffer contents.
    m_bufferProcessor->setLogBuffers(cycles == 1);

    try
    {
        logMessage(QString(QSL("VMUSB readout starting on %1"))
                   .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
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
        // Reset IRQ mask
        //
        error = vmusb->resetIrqMask();
        if (error.isError())
            throw QString("Resetting IRQ mask failed: %1").arg(error.toString());

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
        // LED Sources
        //

        // set bottom yellow light to USB Trigger and latched
        m_daqLedSources  = (1u << 24) | (1u << 28);
        m_daqLedSources |= (3u <<  0); // top yellow: USB InFIFO Full

        error = vmusb->setLedSources(m_daqLedSources);
        if (error.isError())
            throw QString("Setting VMUSB LED Sources Register failed: %1").arg(error.toString());

        //
        // Dev Sources (NIM outputs)
        // The output register is set to zero. Should happen before vme_daq_init() as vme
        // scripts can write this register.
        //
        u32 daqDevSources = 0;

        error = vmusb->setDeviceSources(daqDevSources);
        if (error.isError())
            throw QString("Setting VMUSB DEV Sources Register failed: %1").arg(error.toString());

        //
        // Generate and load VMUSB stacks
        //
        m_vmusbStack.resetLoadOffset(); // reset the static load offset
        int nextStackID = 2; // start at ID=2 as NIM=0 and scaler=1 (fixed)

        for (auto event: daqConfig->getEventConfigs())
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
                s16 stackID = m_vmusbStack.getStackID();

                if (stackID < 0)
                {
                    auto msg = (QSL("VMUSB configuration error: invalid trigger for event \"%1\"")
                                .arg(event->objectName()));
                    throw msg;
                }

                // for NIM1 and scaler triggers the stack knows the stack number
                event->stackID = m_vmusbStack.getStackID();
            }

            qDebug() << "event " << event->objectName() << " -> stackID =" << event->stackID;

            VMEScript readoutScript = build_event_readout_script(event);
            validate_event_readout_script(readoutScript);

            CVMUSBReadoutList readoutList(readoutScript);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
            auto stlvec = readoutList.get();
            m_vmusbStack.setContents(QVector<u32>(std::begin(stlvec), std::end(stlvec)));
#else
            m_vmusbStack.setContents(QVector<u32>::fromStdVector(readoutList.get()));
#endif

            if (m_vmusbStack.getContents().size())
            {
                auto msg = (QString("Loading readout stack for event \"%1\""
                                   ", stack id = %2, size= %4, load offset = %3")
                           .arg(event->objectName())
                           .arg(m_vmusbStack.getStackID())
                           .arg(VMUSBStack::loadOffset)
                           .arg(m_vmusbStack.getContents().size())
                           );

                logMessage(msg);

                {
                    for (u32 line: m_vmusbStack.getContents())
                    {
                        logMessage(QStringLiteral("  0x%1").arg(line, 8, 16, QLatin1Char('0')));
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
        if (!do_VME_DAQ_Init(vmusb))
        {
            setState(DAQState::Idle);
            return;
        }

        //
        // Debug Dump of all VMUSB registers
        //
        logMessage(QSL(""));
        dump_registers(vmusb, [this] (const QString &line) { logMessage(line); });

        //
        // Debug: record raw buffers to file
        //
        if (ctrlSettings.value("DebugRawBuffers").toBool())
        {
            m_rawBufferOut.setFileName("vmusb_raw_buffers.bin");
            if (!m_rawBufferOut.open(QIODevice::WriteOnly))
            {
                auto msg = (QString("Error opening vmusb raw buffers file for writing: %1")
                            .arg(m_rawBufferOut.errorString()));
                logMessage(msg);
                qDebug() << __PRETTY_FUNCTION__ << msg;
            }
            else
            {
                auto msg = (QString("Writing raw VMUSB buffers to %1")
                            .arg(m_rawBufferOut.fileName()));
                logMessage(msg);
                qDebug() << __PRETTY_FUNCTION__ << msg;
            }
        }

        //
        // Readout
        //
        m_bufferProcessor->beginRun();
        logMessage(QSL(""));
        logMessage(QSL("Entering readout loop"));
        stats.start();

        readoutLoop();

        logMessage(QSL("Leaving readout loop"));
        logMessage(QSL(""));

        //
        // DAQ Stop
        //
        vme_daq_shutdown(daqConfig, vmusb, [this] (const QString &msg) { logMessage(msg); });

        //
        // Debug: close raw buffers file
        //
        if (m_rawBufferOut.isOpen())
        {
            auto msg = (QString("Closing vmusb raw buffers file %1")
                        .arg(m_rawBufferOut.fileName()));
            logMessage(msg);

            m_rawBufferOut.close();
        }

        logMessage(QSL(""));
        logMessage(QString(QSL("VMUSB readout stopped on %1"))
                   .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                   );

        // Note: endRun() collects the log contents, which means it should be one of the
        // last actions happening in here. Log messages generated after this point won't
        // show up in the listfile.
        m_bufferProcessor->endRun();
        stats.stop();
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
    catch (const vme_script::ParseError &e)
    {
        logError(QSL("VME Script parse error: ") + e.toString());
        errorThrown = true;
    }
    catch (const VMEError &e)
    {
        logError(e.toString());
        errorThrown = true;
    }

    if (errorThrown)
    {
        try
        {
            if (vmusb->isInDaqMode())
                leave_daq_mode(vmusb, 0, [this] (const QString &msg) { logMessage(msg); });
        }
        catch (...)
        {}
    }

    setState(DAQState::Idle);
}

void VMUSBReadoutWorker::stop()
{
    qDebug() << __PRETTY_FUNCTION__;

    if (m_state == DAQState::Running || m_state == DAQState::Paused)
        m_desiredState = DAQState::Stopping;
}

void VMUSBReadoutWorker::pause()
{
    qDebug() << __PRETTY_FUNCTION__;

    if (m_state == DAQState::Running)
        m_desiredState = DAQState::Paused;
}

void VMUSBReadoutWorker::resume(quint32 nCycles)
{
    qDebug() << __PRETTY_FUNCTION__ << nCycles;

    if (m_state == DAQState::Paused)
    {
        m_cyclesToRun = nCycles;
        m_desiredState = DAQState::Running;
    }
}

static const int leaveDaqReadTimeout_ms = 500;
static const int daqReadTimeout_ms = 500; // This should be higher than the watchdog timeout which is set to 250ms.
static const int daqModeHackTimeout_ms = leaveDaqReadTimeout_ms;

void VMUSBReadoutWorker::readoutLoop()
{
    auto vmusb = m_vmusb;
    u32 actionRegisterAdditionalBits = 1u << 1; // USB trigger
    auto error = enter_daq_mode(vmusb, actionRegisterAdditionalBits, m_daqLedSources,
                                [this] (const QString &msg) { logMessage(msg); });

    if (error.isError())
        throw QString("Error entering VMUSB DAQ mode: %1").arg(error.toString());

    setState(DAQState::Running);

    QElapsedTimer logReadErrorTimer;
    u64 nReadErrors = 0;
    u64 nGoodReads = 0;

    using vme_analysis_common::TimetickGenerator;

    TimetickGenerator timetickGen;
    m_bufferProcessor->timetick(); // immediately write out the very first timetick

    while (true)
    {
        int elapsedSeconds = timetickGen.generateElapsedSeconds();

        while (elapsedSeconds >= 1)
        {
            m_bufferProcessor->timetick();
            elapsedSeconds--;
        }

        // pause
        if (m_state == DAQState::Running && m_desiredState == DAQState::Paused)
        {
            error = leave_daq_mode(vmusb, 0, [this] (const QString &msg) { logMessage(msg); });
            if (error.isError())
                throw QString("Error leaving VMUSB DAQ mode: %1").arg(error.toString());

            while (readBuffer(leaveDaqReadTimeout_ms).bytesRead > 0);

            error = vmusb->setLedSources(0);
            if (error.isError())
            {
                throw QString("Error leaving VMUSB DAQ mode: setLedSources() failed: %1")
                    .arg(error.toString());
            }

            m_bufferProcessor->handlePause();
            setState(DAQState::Paused);
            logMessage(QSL("VMUSB readout paused"));
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            error = enter_daq_mode(vmusb, actionRegisterAdditionalBits, m_daqLedSources,
                                   [this] (const QString &msg) { logMessage(msg); });
            if (error.isError())
                throw QString("Error entering VMUSB DAQ mode: %1").arg(error.toString());

            m_bufferProcessor->handleResume();
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
        else if (likely(m_state == DAQState::Running))
        {
            auto readResult = readBuffer(daqReadTimeout_ms);

            /* XXX: Begin hack:
             * A timeout from readBuffer() here can mean that either there was
             * an error when communicating with the vmusb or that no data is
             * available. The second case can happen if the module sends no or
             * very little data so that the internal buffer of the controller
             * does not fill up fast enough. To avoid this case a smaller
             * buffer size could be chosen but that will negatively impact
             * performance for high data rates. Another method would be to use
             * VMUSBs watchdog feature but that was never implemented despite
             * what the documentation says.
             *
             * The workaround when getting a read timeout is to leave DAQ mode,
             * which forces the controller to dump its buffer, and to then
             * resume DAQ mode.
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
                error = leave_daq_mode(vmusb, 0, [this] (const QString &msg) { logMessage(msg); });
                if (error.isError())
                    throw QString("Error leaving VMUSB DAQ mode (in timeout handling): %1")
                        .arg(error.toString());

                readResult = readBuffer(daqModeHackTimeout_ms);

                error = enter_daq_mode(vmusb, actionRegisterAdditionalBits, m_daqLedSources,
                                       [this] (const QString &msg) { logMessage(msg); });
                if (error.isError())
                    throw QString("Error entering VMUSB DAQ mode (in timeout handling): %1")
                        .arg(error.toString());
                qDebug() << "end USE_DAQMODE_HACK";
            }
#endif

            if (!readResult.error.isError())
            {
                ++nGoodReads;
            }

            if (readResult.bytesRead <= 0)
            {
                static const int LogReadErrorTimer_ms = 5000;
                ++nReadErrors;
                if (!logReadErrorTimer.isValid() || logReadErrorTimer.elapsed() >= LogReadErrorTimer_ms)
                {
                    logMessage(QString("VMUSB Warning: error from bulk read: %1, bytesReceived=%2"
                                       " (total #readErrors=%3, #goodReads=%4)")
                               .arg(readResult.error.toString())
                               .arg(readResult.bytesRead)
                               .arg(nReadErrors)
                               .arg(nGoodReads),
                               true
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
            QThread::msleep(std::min(PauseMaxSleep_ms, timetickGen.getTimeToNextTick_ms()));
        }
        else
        {
            InvalidCodePath;
        }
    }

    setState(DAQState::Stopping);

    qDebug() << __PRETTY_FUNCTION__ << "left readoutLoop, reading remaining data";
    error = leave_daq_mode(vmusb, 0, [this] (const QString &msg) { logMessage(msg); });
    if (error.isError())
        throw QString("Error leaving VMUSB DAQ mode: %1").arg(error.toString());

    while (readBuffer(leaveDaqReadTimeout_ms).bytesRead > 0);

    vmusb->resetIrqMask();

    error = vmusb->setLedSources(0);
    if (error.isError())
        throw QString("Error leaving VMUSB DAQ mode: setLedSources() failed: %1").arg(error.toString());
}

void VMUSBReadoutWorker::setState(DAQState state)
{
    qDebug() << __PRETTY_FUNCTION__ << DAQStateStrings[m_state] << "->" << DAQStateStrings[state];
    m_state = state;
    m_desiredState = state;
    emit stateChanged(state);

    switch (state)
    {
        case DAQState::Idle:
            emit daqStopped();
            break;

        case DAQState::Paused:
            emit daqPaused();
            break;

        case DAQState::Starting:
        case DAQState::Stopping:
            break;

        case DAQState::Running:
            emit daqStarted();
    }

    QCoreApplication::processEvents();
}

void VMUSBReadoutWorker::logError(const QString &message)
{
    logMessage(QString("VMUSB Error: %1").arg(message));
}

VMUSBReadoutWorker::ReadBufferResult VMUSBReadoutWorker::readBuffer(int timeout_ms)
{
    ReadBufferResult result = {};

    m_readBuffer->used = 0;

    result.error = m_vmusb->bulkRead(m_readBuffer->data, m_readBuffer->size, &result.bytesRead, timeout_ms);

    /* Raw buffer output for debugging purposes.
     * The file consists of a sequence of entries with each entry having the following format:
     *   s32 VMEError::errorType
     *   s32 VMEError::errorCode
     *   s32 dataBytes
     *   u8 data[dataBytes]
     * If dataBytes is 0 the data entry will be of size 0. No byte order
     * conversion is done so the format is architecture dependent!
     */
    if (m_rawBufferOut.isOpen())
    {
        s32 errorType = static_cast<s32>(result.error.error());
        s32 errorCode = result.error.errorCode();
        s32 bytesRead = result.bytesRead;

        m_rawBufferOut.write(reinterpret_cast<const char *>(&errorType), sizeof(errorType));
        m_rawBufferOut.write(reinterpret_cast<const char *>(&errorCode), sizeof(errorCode));
        m_rawBufferOut.write(reinterpret_cast<const char *>(&bytesRead), sizeof(bytesRead));
        m_rawBufferOut.write(reinterpret_cast<const char *>(m_readBuffer->data), bytesRead);
    }

    if (result.error.isError())
    {
        qDebug() << __PRETTY_FUNCTION__
            << "vmusb bulkRead result: " << result.error.toString()
            << "bytesRead =" << result.bytesRead;
    }

    if ((!result.error.isError() || result.error.isTimeout()) && result.bytesRead > 0)
    {
        m_readBuffer->used = result.bytesRead;
        DAQStats &stats(m_workerContext.daqStats);
        stats.totalBytesRead += result.bytesRead;
        stats.totalBuffersRead++;

        if (m_bufferProcessor)
            m_bufferProcessor->processBuffer(m_readBuffer);
    }

    return result;
}
