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

#include "sis3153_readout_worker.h"

#include <QCoreApplication>

#include "sis3153/sis3153ETH_vme_class.h"
#include "sis3153/sis3153eth.h"
#include "vme_daq.h"
#include "mvme_listfile.h"

#define SIS_READOUT_DEBUG               1   // enable debugging code
#define SIS_READOUT_BUFFER_DEBUG_PRINT  0   // print buffers to console
#define SIS_READOUT_BUFFER_DEBUG_FILE   1   // print buffers to buffer.log

#define SIS_READOUT_ENABLE_JUMBO_FRAMES 0

using namespace vme_script;

namespace
{
    void validate_vme_config(VMEConfig *vmeConfig)
    {
    }

    static const size_t LocalBufferSize = Megabytes(1);
    static const size_t ReadBufferSize  = Megabytes(1);

    /* Returns the size of the result list in bytes when written to an mvme
     * format buffer. */
    size_t calculate_result_size(const vme_script::ResultList &results)
    {
        size_t size = 0;

        for (const auto &result: results)
        {
            if (result.error.isError())
                continue;

            switch (result.command.type)
            {
                // FIXME: not correct for d16 read
                case CommandType::Read:
                    size += sizeof(u32);
                    break;

                case CommandType::BLT:
                case CommandType::BLTFifo:
                case CommandType::MBLT:
                case CommandType::MBLTFifo:
                    size += result.valueVector.size() * sizeof(u32);
                    break;

                case CommandType::Marker:
                    size += sizeof(u32);
                    break;

                default:
                    break;
            }
        }

        return size;
    }

    size_t calculate_stackList_size(const vme_script::VMEScript &commands)
    {
        size_t size = 2 + 2; // header and trailer

        for (const auto &command: commands)
        {
            switch (command.type)
            {
                case  CommandType::Read:
                    size += 3;
                    break;

                case  CommandType::Write:
                case  CommandType::WriteAbs:
                    size += 4;
                    break;

                case  CommandType::Marker:
                    size += 2;
                    break;

                case  CommandType::BLT:
                case  CommandType::BLTFifo:
                case  CommandType::MBLT:
                case  CommandType::MBLTFifo:
                    size += 3;
                    break;

                case  CommandType::Wait:
                    InvalidCodePath;
                    break;

                case  CommandType::BLTCount:
                case  CommandType::BLTFifoCount:
                case  CommandType::MBLTCount:
                case  CommandType::MBLTFifoCount:
                    InvalidCodePath;
                    break;

                case  CommandType::SetBase:
                case  CommandType::ResetBase:
                    break;

                case  CommandType::Invalid:
                    InvalidCodePath;
                    break;
            }
        }

        return size;
    }

    static const u32 AccessSize8  = 0;
    static const u32 AccessSize16 = 1;
    static const u32 AccessSize32 = 2;
    static const u32 AccessSize64 = 3;

    inline u32 get_access_size(DataWidth dw)
    {
        switch (dw)
        {
            case DataWidth::D16:
                return AccessSize16;
            case DataWidth::D32:
                return AccessSize32;
        }
        InvalidCodePath;
        return 0;
    }

    // These functions are modeled after sis3153eth::list_generate_add_vmeA32D32_read/write.
    // accessSize arg: (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
    void stackList_add_single_read(u32 *list_ptr, u32 *list_buffer, u32 vme_addr, u32 vme_access_size, u32 vme_am_mode)
    {
        unsigned int vme_write_flag = 0;
        unsigned int vme_fifo_mode  = 0;
        unsigned int vme_nof_bytes  = (1u << vme_access_size);

        list_buffer[*list_ptr + 0] = 0xAAAA4000 | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes >> 16) & 0xFF);
        list_buffer[*list_ptr + 1] = ((vme_am_mode & 0xFFFF) << 16) | (vme_nof_bytes & 0xFFFF);
        list_buffer[*list_ptr + 2] = vme_addr;
        *list_ptr = *list_ptr + 3;
    }

    void stackList_add_single_write(u32 *list_ptr, u32 *list_buffer, u32 vme_addr, u32 vme_data, u32 vme_access_size, u32 vme_am_mode)
    {
        unsigned int vme_write_flag = 1;
        unsigned int vme_fifo_mode  = 0;
        unsigned int vme_nof_bytes  = (1u << vme_access_size);

        list_buffer[*list_ptr + 0] = 0xAAAA4000 | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes >> 16) & 0xFF);
        list_buffer[*list_ptr + 1] = ((vme_am_mode & 0xFFFF) << 16) | (vme_nof_bytes & 0xFFFF);
        list_buffer[*list_ptr + 2] = vme_addr & 0xfffffffc;
        list_buffer[*list_ptr + 3] = vme_data;
        *list_ptr = *list_ptr + 4;
    }

    void stackList_add_block_read(u32 *list_ptr, u32 *list_buffer, u32 vme_addr, u32 vme_nof_bytes, bool mblt, bool fifo)
    {
        unsigned int vme_write_flag  = 0;
        unsigned int vme_fifo_mode   = fifo ? 1 : 0;
        unsigned int vme_access_size = mblt ? AccessSize64 : AccessSize32;
        unsigned int vme_am_mode     = mblt ? 0x8 : 0xB;

        list_buffer[*list_ptr + 0] = 0xAAAA4000 | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes >> 16) & 0xFF);
        list_buffer[*list_ptr + 1] = ((vme_am_mode & 0xFFFF) << 16) | (vme_nof_bytes & 0xFFFF);
        list_buffer[*list_ptr + 2] = vme_addr;
        *list_ptr = *list_ptr + 3;
    }

    QVector<u32> build_stackList(SIS3153 *sis, const vme_script::VMEScript &commands)
    {
        size_t stackListSize = calculate_stackList_size(commands);
        QVector<u32> result(stackListSize);
        u32 resultOffset = 0;

        auto impl = sis->getImpl();
        impl->list_generate_add_header(&resultOffset, result.data());

        // FIXME: hardcoding data widths and stuff for now
        // TODO: support other address modes than a32

        for (const auto &command: commands)
        {
            switch (command.type)
            {
                case  CommandType::Read:
                    stackList_add_single_read(&resultOffset, result.data(), command.address,
                                              get_access_size(command.dataWidth), 0x9);
                    break;

                case  CommandType::Write:
                case  CommandType::WriteAbs:
                    stackList_add_single_write(&resultOffset, result.data(), command.address, command.value,
                                               get_access_size(command.dataWidth), 0x9);
                    break;

                case  CommandType::Marker:
                    impl->list_generate_add_marker(&resultOffset, result.data(), command.value);
                    break;

                case  CommandType::BLT:
                    stackList_add_block_read(&resultOffset, result.data(), command.address, command.transfers, false, false);
                    break;

                case  CommandType::BLTFifo:
                    stackList_add_block_read(&resultOffset, result.data(), command.address, command.transfers, false, true);
                    break;
                case  CommandType::MBLT:
                    stackList_add_block_read(&resultOffset, result.data(), command.address, command.transfers, true, false);
                    break;
                case  CommandType::MBLTFifo:
                    stackList_add_block_read(&resultOffset, result.data(), command.address, command.transfers, true, true);
                    break;

                case  CommandType::Wait:
                    InvalidCodePath;
                    break;

                case  CommandType::BLTCount:
                case  CommandType::BLTFifoCount:
                case  CommandType::MBLTCount:
                case  CommandType::MBLTFifoCount:
                    InvalidCodePath;
                    break;

                case  CommandType::SetBase:
                case  CommandType::ResetBase:
                    break;

                case  CommandType::Invalid:
                    InvalidCodePath;
                    break;
            }
        }

        impl->list_generate_add_trailer(&resultOffset, result.data());

        Q_ASSERT(resultOffset == stackListSize);

        return result;
    }
}


SIS3153ReadoutWorkerIRQPolling::SIS3153ReadoutWorkerIRQPolling(QObject *parent)
    : VMEReadoutWorker(parent)
    , m_localEventBuffer(LocalBufferSize)
{
}

SIS3153ReadoutWorkerIRQPolling::~SIS3153ReadoutWorkerIRQPolling()
{
}

/* readout using polling
 * =====================
 *
 * Notes:
 * - just ignoring non-irq events for now
 * - ignoring any irqVector that's set on events. just the irq number counts
 *
 * build readout script for each event
 * store irq -> readout_script mapping
 * run init sequence
 * readoutLoop:
 *  poll irq status
 *  for all irqs:
 *      if irq is set and we have an event for it
 *          run readout scripts and capture output
 *
 * run shutdown sequence
 */

void SIS3153ReadoutWorkerIRQPolling::start(quint32 cycles)
{
    qDebug() << __PRETTY_FUNCTION__ << "cycles =" << cycles;

    if (m_state != DAQState::Idle)
        return;

    auto sis = qobject_cast<SIS3153 *>(m_workerContext.controller);
    if (!sis)
    {
        logError(QSL("SIS3153 controller required"));
        return;
    }

    m_sis = sis;
    m_cyclesToRun = cycles;
    VMEError error;
    setState(DAQState::Starting);

    try
    {
        logMessage(QString(QSL("SIS3153 readout starting on %1"))
                   .arg(QDateTime::currentDateTime().toString())
                   );

        validate_vme_config(m_workerContext.vmeConfig); // throws on error

        //
        // Build IRQ Readout Scripts
        //

        m_irqReadoutScripts.fill(VMEScript());
        m_irqEventConfigs.fill(nullptr);
        m_irqEventConfigIndex.fill(-1);

        for (s32 eventIndex = 0;
             eventIndex < m_workerContext.vmeConfig->eventConfigs.size();
             ++eventIndex)
        {
            auto event = m_workerContext.vmeConfig->eventConfigs[eventIndex];

            if (event->triggerCondition == TriggerCondition::Interrupt)
            {
                qDebug() << __PRETTY_FUNCTION__ << event << ", irq =" << event->irqLevel;
                m_irqReadoutScripts[event->irqLevel] = build_event_readout_script(event);
                m_irqEventConfigs[event->irqLevel] = event;
                m_irqEventConfigIndex[event->irqLevel] = eventIndex;

                qDebug() << __PRETTY_FUNCTION__ << event << ", #commands =" << m_irqReadoutScripts[event->irqLevel].size();
            }
        }

        //
        // Read and log firmware version
        //
        {
            u32 fwReg;
            error = sis->readRegister(SIS3153Registers::ModuleIdAndFirmware, &fwReg);
            if (error.isError())
                throw QString("Error reading SIS3153 firmware version: %1").arg(error.toString());

            u32 fwMajor = (fwReg & 0xff00) >> 8;
            u32 fwMinor = (fwReg & 0x00ff);

            u32 serReg;
            error = sis->readRegister(SIS3153Registers::SerialNumber, &serReg);
            if (error.isError())
                throw QString("Error reading SIS3153 serial number: %1").arg(error.toString());

            logMessage(QString(QSL("SIS3153 (SerialNumber=%1, Firmware=%2.%3)\n"))
                       .arg(serReg)
                       .arg(fwMajor, 2, 16, QLatin1Char('0'))
                       .arg(fwMinor, 2, 16, QLatin1Char('0'))
                      );
        }

        //
        // DAQ Init
        //
        vme_daq_init(m_workerContext.vmeConfig, sis, [this] (const QString &msg) { logMessage(msg); });

        //
        // Debug Dump of SIS3153 registers
        //
        logMessage(QSL(""));
        dump_registers(sis, [this] (const QString &line) { this->logMessage(line); });

        //
        // Readout
        //
        logMessage(QSL(""));
        logMessage(QSL("Entering readout loop"));
        m_workerContext.daqStats->start();

        readoutLoop();

        m_workerContext.daqStats->stop();
        logMessage(QSL("Leaving readout loop"));
        logMessage(QSL(""));

        //
        // DAQ Stop
        //
        vme_daq_shutdown(m_workerContext.vmeConfig, sis, [this] (const QString &msg) { logMessage(msg); });
        logMessage(QString(QSL("SIS3153 readout stopped on %1"))
                   .arg(QDateTime::currentDateTime().toString())
                   );
    }
    catch (const std::runtime_error &e)
    {
        logError(e.what());
    }
    catch (const VMEError &e)
    {
        logError(e.toString());
    }
    catch (const QString &e)
    {
        logError(e);
    }

    setState(DAQState::Idle);
    emit daqStopped();
}

void SIS3153ReadoutWorkerIRQPolling::readoutLoop()
{
    setState(DAQState::Running);

    auto sis = m_sis;
    VMEError error;

    QTime elapsedTime;
    elapsedTime.start();
    //m_bufferProcessor->timetick(); // FIXME

    while (true)
    {
        // Qt event processing to handle queued slots invocations (stop, pause, resume)
        processQtEvents();

        // One timetick for every elapsed second.
        s32 elapsedSeconds = elapsedTime.elapsed() / 1000;

        if (elapsedSeconds >= 1)
        {
            do
            {
                //m_bufferProcessor->timetick(); // FIXME
            } while (--elapsedSeconds);
            elapsedTime.restart();
        }

        // stay in running state
        if (m_state == DAQState::Running && m_desiredState == DAQState::Running)
        {
            u32 irqStatus;
            error = sis->readRegister(SIS3153Registers::VMEInterruptStatus, &irqStatus);
            if (error.isError()) throw error;

            for (u32 irq = 1; irq <= 7; ++irq)
            {
                if (irqStatus & (1u << irq)
                    && !m_irqReadoutScripts[irq].isEmpty())
                {
                    // ack irq and as a side effect get the irqVector
                    u8 irqVector;
                    s32 resultCode = sis->getImpl()->vme_IACK_D8_read(irq, &irqVector);
                    if (resultCode != 0)
                        throw make_sis_error(resultCode);

                    auto results = run_script(sis, m_irqReadoutScripts[irq], [this] (const QString &msg) { logMessage(msg); });

#if SIS_READOUT_DEBUG
                    qDebug() << "readout script produced" << results.size() << "results:";

                    for (const auto &result: results)
                    {
                        qDebug() << "\t" << to_string(result.command.type);
                    }
#endif

                    processReadoutResults(m_irqEventConfigs[irq], m_irqEventConfigIndex[irq], results);
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
        // pause
        else if (m_state == DAQState::Running && m_desiredState == DAQState::Paused)
        {
            setState(DAQState::Paused);
            logMessage(QSL("SIS3153 readout paused"));
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            setState(DAQState::Running);
            logMessage(QSL("SIS3153 readout resumed"));
        }
        // stop
        else if (m_desiredState == DAQState::Stopping)
        {
            logMessage(QSL("SIS3153 readout stopping"));
            break;
        }
        // paused
        else if (m_state == DAQState::Paused)
        {
            // In paused state process Qt events for a maximum of 1s, then run
            // another iteration of the loop to handle timeticks.
            processQtEvents(1000);
        }
        else
        {
            InvalidCodePath;
        }
    }

    setState(DAQState::Stopping);
    processQtEvents();

    qDebug() << __PRETTY_FUNCTION__ << "left readoutLoop, reading remaining data";

}

void SIS3153ReadoutWorkerIRQPolling::stop()
{
    qDebug() << __PRETTY_FUNCTION__;
    if (m_state == DAQState::Running || m_state == DAQState::Paused)
        m_desiredState = DAQState::Stopping;
}

void SIS3153ReadoutWorkerIRQPolling::pause()
{
    qDebug() << __PRETTY_FUNCTION__;
    if (m_state == DAQState::Running)
        m_desiredState = DAQState::Paused;
}

void SIS3153ReadoutWorkerIRQPolling::resume()
{
    qDebug() << __PRETTY_FUNCTION__;
    if (m_state == DAQState::Paused)
        m_desiredState = DAQState::Running;
}

bool SIS3153ReadoutWorkerIRQPolling::isRunning() const
{
    return m_state != DAQState::Idle;
}

void SIS3153ReadoutWorkerIRQPolling::setState(DAQState state)
{
    qDebug() << __PRETTY_FUNCTION__ << DAQStateStrings[m_state] << "->" << DAQStateStrings[state];
    m_state = state;
    m_desiredState = state;
    emit stateChanged(state);
}

void SIS3153ReadoutWorkerIRQPolling::logError(const QString &message)
{
    logMessage(QString("SIS3153 Error: %1").arg(message));
}

void SIS3153ReadoutWorkerIRQPolling::logMessage(const QString &message)
{
    m_workerContext.logMessage(message);
}

void SIS3153ReadoutWorkerIRQPolling::processReadoutResults(EventConfig *event, s32 eventConfigIndex, const vme_script::ResultList &results)
{
    using LF = listfile_v1;
    /* generate a mvme format buffer here and pass it on to the analysis */
    // TODO: write to listfile

    DataBuffer *outputBuffer = dequeue(getFreeQueue());

    if (!outputBuffer)
    {
        outputBuffer = &m_localEventBuffer;
    }


    outputBuffer->used = 0;

    size_t resultOutputBytes = calculate_result_size(results);
    outputBuffer->ensureCapacity(2 * resultOutputBytes);

    u32 *eventHeader = outputBuffer->asU32();
    outputBuffer->used += sizeof(u32);
    *eventHeader = (ListfileSections::SectionType_Event << LF::SectionTypeShift) & LF::SectionTypeMask;
    *eventHeader |= (eventConfigIndex << LF::EventTypeShift) & LF::EventTypeMask;

    /* EventHeader
     * ModuleHeader
     *  data
     *  EndMarker
     * ModuleHeader
     *  data
     *  EndMarker
     * EndMarker
     */


    s32 eventSize = 0;                  // size of the event section in 32-bit words.
    s32 moduleSize = 0;                 // size of the module section in 32-bit words.
    auto modules = event->modules;
    s32 moduleIndex = 0;
    u32 *moduleHeader = nullptr;

    for (auto &result: results)
    {
        if (result.error.isError())
            continue;

        if (moduleIndex >= modules.size())
            break;

        if (!moduleHeader)
        {
            moduleHeader = outputBuffer->asU32();
            outputBuffer->used += sizeof(u32);
            auto moduleConfig = modules[moduleIndex];
            *moduleHeader = (((u32)moduleConfig->getModuleMeta().typeId) << LF::ModuleTypeShift) & LF::ModuleTypeMask;
            ++eventSize;
            moduleSize = 0;
        }

        switch (result.command.type)
        {
            case CommandType::Read:
                {
                    *outputBuffer->asU32() = result.value;
                    outputBuffer->used += sizeof(u32);
                    ++eventSize;
                    ++moduleSize;
                } break;

            case CommandType::BLT:
            case CommandType::BLTFifo:
            case CommandType::MBLT:
            case CommandType::MBLTFifo:
                {
                    u32 *dst = reinterpret_cast<u32 *>(outputBuffer->data + outputBuffer->used);

                    for (u32 data: result.valueVector)
                    {
                        *dst++ = data;
                    }

                    s32 vecSize = result.valueVector.size();
                    outputBuffer->used += vecSize * sizeof(u32);
                    eventSize += vecSize;
                    moduleSize += vecSize;
                } break;

            case CommandType::Marker:
                {
                    *outputBuffer->asU32() = result.value;
                    outputBuffer->used += sizeof(u32);
                    ++eventSize;
                    ++moduleSize;

                    if (result.value == EndMarker)
                    {
                        Q_ASSERT(moduleHeader);
                        *moduleHeader |= (moduleSize << LF::SubEventSizeShift) & LF::SubEventSizeMask;
                        moduleHeader = nullptr;
                        ++moduleIndex;
                    }
                } break;

            default:
                Q_ASSERT(!"unhandled command type");
                break;
        }
    }

    // add a final EndMarker to finish the event section
    // then write the size to the event section header
    *outputBuffer->asU32() = EndMarker;
    outputBuffer->used += sizeof(u32);
    ++eventSize;

    *eventHeader |= (eventSize << LF::SectionSizeShift) & LF::SectionSizeMask;

#if SIS_READOUT_BUFFER_DEBUG_PRINT
    qDebug() << __PRETTY_FUNCTION__ << "raw outputBuffer dump:";
    debugOutputBuffer(outputBuffer->data, outputBuffer->used);

    qDebug() << __PRETTY_FUNCTION__ << "outputBuffer dump as mvme buffer";
    QTextStream out(stderr);
    dump_mvme_buffer(out, outputBuffer, true);
#endif

    if (outputBuffer != &m_localEventBuffer)
    {
        enqueue_and_wakeOne(getFullQueue(), outputBuffer);
    }
}

//
// SIS3153ReadoutWorker
//
SIS3153ReadoutWorker::SIS3153ReadoutWorker(QObject *parent)
    : VMEReadoutWorker(parent)
    , m_localEventBuffer(LocalBufferSize)
    , m_readBuffer(ReadBufferSize)
{
}

SIS3153ReadoutWorker::~SIS3153ReadoutWorker()
{
}

void SIS3153ReadoutWorker::start(quint32 cycles)
{
    qDebug() << __PRETTY_FUNCTION__ << "cycles =" << cycles;

    if (m_state != DAQState::Idle)
        return;

    auto sis = qobject_cast<SIS3153 *>(m_workerContext.controller);
    if (!sis)
    {
        logError(QSL("SIS3153 controller required"));
        return;
    }

    m_sis = sis;
    m_cyclesToRun = cycles;
    VMEError error;
    setState(DAQState::Starting);

    try
    {
        logMessage(QString(QSL("SIS3153 readout starting on %1"))
                   .arg(QDateTime::currentDateTime().toString())
                   );

        validate_vme_config(m_workerContext.vmeConfig); // throws on error

        //
        // Read and log firmware version
        //
        {
            u32 fwReg;
            error = sis->readRegister(SIS3153Registers::ModuleIdAndFirmware, &fwReg);
            if (error.isError())
                throw QString("Error reading SIS3153 firmware version: %1").arg(error.toString());

            u32 fwMajor = (fwReg & 0xff00) >> 8;
            u32 fwMinor = (fwReg & 0x00ff);

            u32 serReg;
            error = sis->readRegister(SIS3153Registers::SerialNumber, &serReg);
            if (error.isError())
                throw QString("Error reading SIS3153 serial number: %1").arg(error.toString());

            logMessage(QString(QSL("SIS3153 (SerialNumber=%1, Firmware=%2.%3)\n"))
                       .arg(serReg)
                       .arg(fwMajor, 2, 16, QLatin1Char('0'))
                       .arg(fwMinor, 2, 16, QLatin1Char('0'))
                      );
        }

        //
        // General Setup
        //

        // FIXME: both methods do not check for errors internally!
#if SIS_READOUT_ENABLE_JUMBO_FRAMES
        sis->getImpl()->set_UdpSocketEnableJumboFrame();
#else
        sis->getImpl()->set_UdpSocketDisableJumboFrame();
#endif

        //
        // Reset StackList configuration registers
        //
        static const s32 SIS3153Eth_NumberOfStackLists = 8;

        for (s32 stackIndex = 0;
             stackIndex < SIS3153Eth_NumberOfStackLists;
             ++stackIndex)
        {
            s32 regAddr = SIS3153ETH_STACK_LIST1_CONFIG + 2 * stackIndex;
            error = sis->writeRegister(regAddr, 0);
            if (error.isError())
                throw error.toString();

            regAddr = SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE + 2 * stackIndex;
            error = sis->writeRegister(regAddr, 0);
            if (error.isError())
                throw error.toString();
        }

        //
        // Build IRQ Readout Scripts
        //

        m_irqEventConfigs.fill(nullptr);
        m_irqEventConfigIndex.fill(-1);

        s32 stackIndex = 0;
        u32 stackLoadAddress = SIS3153ETH_STACK_RAM_START_ADDR;
        u32 stackListControlValue = 0;
        u32 nextTimerTriggerSource = SIS3153Registers::TriggerSourceTimer1;

        for (s32 eventIndex = 0;
             eventIndex < m_workerContext.vmeConfig->eventConfigs.size();
             ++eventIndex)
        {
            auto event = m_workerContext.vmeConfig->eventConfigs[eventIndex];

            // build the command stack list
            auto readoutCommands = build_event_readout_script(event);
            QVector<u32> stackList = build_stackList(sis, readoutCommands);

            qDebug() << __PRETTY_FUNCTION__ << event << ", #commands =" << readoutCommands.size();
            qDebug() << __PRETTY_FUNCTION__ << ">>>>> begin sis stackList for event" << event << ":";
            debugOutputBuffer(reinterpret_cast<u8 *>(stackList.data()), stackList.size() * sizeof(u32));
            qDebug() << __PRETTY_FUNCTION__ << "<<<<< end sis stackList";

            if (event->triggerCondition == TriggerCondition::Interrupt)
            {
                qDebug() << __PRETTY_FUNCTION__ << event << ", irq =" << event->irqLevel;
                m_irqEventConfigs[event->irqLevel] = event;
                m_irqEventConfigIndex[event->irqLevel] = eventIndex;

                // set write start address to  SIS3153ETH_STACK_LIST*_CONFIG
                // set trigger condition in SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE
                // increment stackIndex and stackLoadAddress

                // upload
                u32 wordsWritten = 0;
                s32 resultCode = sis->getImpl()->udp_sis3153_register_dma_write(stackLoadAddress, stackList.data(), stackList.size() - 1, &wordsWritten);
                error = make_sis_error(resultCode);
                if (error.isError()) throw error;

                qDebug() << __PRETTY_FUNCTION__
                    << "uploaded stackList to offset 0x" << QString::number(stackLoadAddress, 16)
                    << ", wordsWritten =" << wordsWritten;

                // stack list config

                // 13 bit stack start address which should be relative to the
                // stack RAM start address. So it's an offset into the stack
                // memory area.
                Q_ASSERT(stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR <= (1 << 13));

                u32 configValue = ((stackList.size() - 1) << 16) | (stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR);
                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_CONFIG + 2 * stackIndex, configValue);
                if (error.isError()) throw error;

                // stack list trigger source
                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE + 2 * stackIndex,
                                           event->irqLevel);
                if (error.isError()) throw error;

                ++stackIndex;
                stackLoadAddress += stackList.size();
                stackListControlValue |= SIS3153Registers::StackListControlValues::StackListEnable;
            }
            else if (event->triggerCondition == TriggerCondition::Periodic)
            {
                // TODO: move this check into validate_vme_config()
                if (nextTimerTriggerSource > SIS3153Registers::TriggerSourceTimer2)
                    throw QString("SIS3153 supports no more than 2 periodic events!");

                // TODO: implement support for both timers

                double period_secs          = event->scalerReadoutPeriod * 0.5;
                double period_usecs         = period_secs * 1e6;
                double period_100usecs      = period_usecs / 100.0;
                u32 timerValue              = period_100usecs - 1.0;

                qDebug() << __PRETTY_FUNCTION__ << event
                    << ", period_secs =" << period_secs
                    << ", period_100usecs =" << period_100usecs
                    << ", timerValue =" << timerValue;

                if (timerValue > 0xffff)
                {
                    throw QString("Maximum timer period exceeded for event %1").arg(event->objectName());
                }

                // upload
                u32 wordsWritten = 0;
                s32 resultCode = sis->getImpl()->udp_sis3153_register_dma_write(stackLoadAddress, stackList.data(), stackList.size() - 1, &wordsWritten);
                error = make_sis_error(resultCode);
                if (error.isError()) throw error;

                qDebug() << __PRETTY_FUNCTION__ << "uploaded stackList to offset" << stackLoadAddress << ", wordsWritten =" << wordsWritten;

                // stack list config

                // 13 bit stack start address which should be relative to the
                // stack RAM start address. So it's an offset into the stack
                // memory area.
                Q_ASSERT(stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR <= (1 << 13));

                u32 configValue = ((stackList.size() - 1) << 16) | (stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR);
                u32 timerConfigRegister = SIS3153Registers::StackListTimer1Config;

#if 0
// FIXME: TESTING ANOTHER HACK: enable watchdog bit here
                if (nextTimerTriggerSource == SIS3153Registers::TriggerSourceTimer2)
                {
                    // 100 us steps
                    timerValue = 100 * 500;
                    timerValue -= 1;
                    timerValue |= SIS3153Registers::StackListTimerWatchdogEnable;
                    timerConfigRegister = SIS3153Registers::StackListTimer2Config;
                }
#endif

                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_CONFIG + 2 * stackIndex, configValue);
                if (error.isError()) throw error;

                // stack list trigger source
                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE + 2 * stackIndex,
                                           nextTimerTriggerSource);
                if (error.isError()) throw error;

                // timer setup
                error = sis->writeRegister(timerConfigRegister, timerValue);
                if (error.isError()) throw error;

                ++stackIndex;
                stackLoadAddress += stackList.size();
                stackListControlValue |= SIS3153Registers::StackListControlValues::StackListEnable;

                if (nextTimerTriggerSource == SIS3153Registers::TriggerSourceTimer1)
                    stackListControlValue |= SIS3153Registers::StackListControlValues::Timer1Enable;

                if (nextTimerTriggerSource == SIS3153Registers::TriggerSourceTimer2)
                    stackListControlValue |= SIS3153Registers::StackListControlValues::Timer2Enable;

                ++nextTimerTriggerSource;
            }
            else if (event->triggerCondition == TriggerCondition::Input1RisingEdge
                     || event->triggerCondition == TriggerCondition::Input1FallingEdge
                     || event->triggerCondition == TriggerCondition::Input2RisingEdge
                     || event->triggerCondition == TriggerCondition::Input2FallingEdge)
            {
                // upload
                u32 wordsWritten = 0;
                s32 resultCode = sis->getImpl()->udp_sis3153_register_dma_write(stackLoadAddress, stackList.data(), stackList.size() - 1, &wordsWritten);
                error = make_sis_error(resultCode);
                if (error.isError()) throw error;

                qDebug() << __PRETTY_FUNCTION__
                    << "uploaded stackList to offset 0x" << QString::number(stackLoadAddress, 16)
                    << ", wordsWritten =" << wordsWritten;

                // stack list config

                // 13 bit stack start address which should be relative to the
                // stack RAM start address. So it's an offset into the stack
                // memory area.
                Q_ASSERT(stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR <= (1 << 13));

                u32 configValue = ((stackList.size() - 1) << 16) | (stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR);
                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_CONFIG + 2 * stackIndex, configValue);
                if (error.isError()) throw error;

                // stack list trigger source
                u32 triggerSourceValue = 0;
                switch (event->triggerCondition)
                {
                    case TriggerCondition::Input1RisingEdge:
                        triggerSourceValue = SIS3153Registers::TriggerSourceInput1RisingEdge;
                        break;
                    case TriggerCondition::Input1FallingEdge:
                        triggerSourceValue = SIS3153Registers::TriggerSourceInput1FallingEdge;
                        break;
                    case TriggerCondition::Input2RisingEdge:
                        triggerSourceValue = SIS3153Registers::TriggerSourceInput2RisingEdge;
                        break;
                    case TriggerCondition::Input2FallingEdge:
                        triggerSourceValue = SIS3153Registers::TriggerSourceInput2FallingEdge;
                        break;

                    InvalidDefaultCase;
                }
                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE + 2 * stackIndex,
                                           triggerSourceValue);
                if (error.isError()) throw error;

                ++stackIndex;
                stackLoadAddress += stackList.size();
                stackListControlValue |= SIS3153Registers::StackListControlValues::StackListEnable;
            }
        }

        //
        // DAQ Init
        //
        vme_daq_init(m_workerContext.vmeConfig, sis, [this] (const QString &msg) { logMessage(msg); });

        //
        // Debug Dump of SIS3153 registers
        //
        logMessage(QSL(""));
        dump_registers(sis, [this] (const QString &line) { this->logMessage(line); });

#if SIS_READOUT_DEBUG && SIS_READOUT_BUFFER_DEBUG_FILE
        m_debugFile = new QFile("buffer.log", this);
        m_debugFile->open(QIODevice::WriteOnly);
#endif

        m_packetCountsByStack.fill(0);

        // enter DAQ mode
        error = sis->writeRegister(SIS3153ETH_STACK_LIST_CONTROL, stackListControlValue);
        if (error.isError()) throw error;

        //
        // Readout
        //
        logMessage(QSL(""));
        logMessage(QSL("Entering readout loop"));
        m_workerContext.daqStats->start();

        readoutLoop();

        m_workerContext.daqStats->stop();
        logMessage(QSL("Leaving readout loop"));
        logMessage(QSL(""));

        //
        // DAQ Stop
        //
        vme_daq_shutdown(m_workerContext.vmeConfig, sis, [this] (const QString &msg) { logMessage(msg); });

        logMessage(QString(QSL("SIS3153 readout stopped on %1"))
                   .arg(QDateTime::currentDateTime().toString())
                   );

#if SIS_READOUT_DEBUG && SIS_READOUT_BUFFER_DEBUG_FILE
        delete m_debugFile;
        m_debugFile = nullptr;
#endif

    }
    catch (const std::runtime_error &e)
    {
        logError(e.what());
    }
    catch (const VMEError &e)
    {
        logError(e.toString());
    }
    catch (const QString &e)
    {
        logError(e);
    }

    setState(DAQState::Idle);
    emit daqStopped();
}

void SIS3153ReadoutWorker::readoutLoop()
{
    auto sis = m_sis;
    auto daqStats = m_workerContext.daqStats;
    VMEError error;


    setState(DAQState::Running);

    QTime elapsedTime;
    elapsedTime.start();
    //m_bufferProcessor->timetick(); // FIXME

    while (true)
    {
        // Qt event processing to handle queued slots invocations (stop, pause, resume)
        // TODO: maybe optimize by calling this only every N loops or get rid
        // of it completely and instead test the values of (atomic) ints
        // that are set by stop() and pause().
        // The second solution would still need to call processQtEvents() after
        // the state changed to emit daqStopped() and stateChanged()
        processQtEvents();

        // One timetick for every elapsed second.
        s32 elapsedSeconds = elapsedTime.elapsed() / 1000;

        if (elapsedSeconds >= 1)
        {
            do
            {
                //m_bufferProcessor->timetick(); // FIXME
            } while (--elapsedSeconds);
            elapsedTime.restart();

            for (size_t i=0; i<m_packetCountsByStack.size(); ++i)
            {
                if (m_packetCountsByStack[i])
                {
                    qDebug("packetCountsByStack[%lu] = %lu", i, m_packetCountsByStack[i]);
                }
            }
        }

        // stay in running state
        if (m_state == DAQState::Running && m_desiredState == DAQState::Running)
        {
            // udp_read_list_packet() does no processing
#if 0
            s32 bytesRead = sis->getImpl()->udp_read_list_packet(reinterpret_cast<char *>(m_readBuffer.data));
            if (bytesRead < 0)
            {
                qDebug() << __PRETTY_FUNCTION__ << "recvfrom returned < 0!";
            }
            else
            {
                m_readBuffer.used = bytesRead;

                qDebug() << __PRETTY_FUNCTION__ << "received" << bytesRead << " bytes:";
                debugOutputBuffer(m_readBuffer.asU32(0), m_readBuffer.used / sizeof(u32));
            }
#else

            // list_read_event() puts bytes 0, 1, 2 in packetAck, packetIdent
            // and packetStatus and then copies the rest of the data into the
            // buffer using memcpy()
            u8 packetAck, packetIdent, packetStatus;
            u32 longwordsRead;
            s32 bytesRead = sis->getImpl()->list_read_event(&packetAck, &packetIdent, &packetStatus,
                                                            m_readBuffer.asU32(0), &longwordsRead);


            if (bytesRead > 0)
            {
                daqStats->addBuffersRead(1);
                daqStats->addBytesRead(bytesRead);
                const double alpha = 0.1;
                daqStats->avgReadSize = bytesRead; // FIXME: not average, just snapshot of last read
                //daqStats->avgReadSize = (alpha * bytesRead) + (1.0 - alpha) * daqStats->avgReadSize;

                ++m_packetCountsByStack[packetAck & 0x7];
            }

            if (bytesRead < 0)
            {
                qDebug() << __PRETTY_FUNCTION__ << "list_read_event returned < 0!";
            }
            else if (bytesRead < 3)
            {
                qDebug() << __PRETTY_FUNCTION__ << "list_read_event returned < 3!";
            }
            else
            {
                m_readBuffer.used = longwordsRead * sizeof(u32);

#if SIS_READOUT_DEBUG
                // begin debug ====================

                auto debug_print = [this](const char *str)
                {
#if SIS_READOUT_BUFFER_DEBUG_PRINT
                    qDebug("%s", str);
#endif

#if SIS_READOUT_BUFFER_DEBUG_FILE
                    QTextStream qout(m_debugFile);
                    qout << str << endl;
#endif
                };

                char sbuf[1024];

                sprintf(sbuf, "received %u words, packetAck=0x%x, packetIdent=0x%x, packetStatus=0x%x",
                        longwordsRead, (u32)packetAck, (u32)packetIdent, (u32)packetStatus);

                debug_print(sbuf);

                if (longwordsRead >= 4)
                {
                    sprintf(sbuf, "first two words: 0x%08x, 0x%08x",
                            m_readBuffer.asU32(0)[0], m_readBuffer.asU32(0)[1]);
                    debug_print(sbuf);

                    sprintf(sbuf, "last two words:  0x%08x, 0x%08x",
                            m_readBuffer.asU32(0)[longwordsRead-2], m_readBuffer.asU32(0)[longwordsRead-1]);
                    debug_print(sbuf);
                }

#if SIS_READOUT_BUFFER_DEBUG_PRINT
                debugOutputBuffer(m_readBuffer.data, m_readBuffer.used);
#endif

#if SIS_READOUT_BUFFER_DEBUG_FILE
                QTextStream qout(m_debugFile);
                debugOutputBuffer(qout, m_readBuffer.data, m_readBuffer.used);
#endif

                // end debug ====================
#endif // SIS_READOUT_DEBUG
            }
#endif

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
        // pause
        else if (m_state == DAQState::Running && m_desiredState == DAQState::Paused)
        {
            // TODO: needs 2nd socket to stop and needs to read remaining buffers
            error = sis->writeRegister(SIS3153ETH_STACK_LIST_CONTROL, 1 << 16); // clear is 16 bits higher!!!!
            if (error.isError()) throw error;

            setState(DAQState::Paused);
            logMessage(QSL("SIS3153 readout paused"));
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            // TODO: needs 2nd socket to stop and needs to read remaining buffers
            // TODO: needs to restore the correct register value
            error = sis->writeRegister(SIS3153ETH_STACK_LIST_CONTROL, 1);
            if (error.isError()) throw error;

            setState(DAQState::Running);
            logMessage(QSL("SIS3153 readout resumed"));
        }
        // stop
        else if (m_desiredState == DAQState::Stopping)
        {
            logMessage(QSL("SIS3153 readout stopping"));
            break;
        }
        // paused
        else if (m_state == DAQState::Paused)
        {
            // In paused state process Qt events for a maximum of 1s, then run
            // another iteration of the loop to handle timeticks.
            processQtEvents(1000);
        }
        else
        {
            InvalidCodePath;
        }
    }

    setState(DAQState::Stopping);
    processQtEvents();

    error = sis->writeRegister(SIS3153ETH_STACK_LIST_CONTROL, 1 << 16); // clear is 16 bits higher!!!!
    if (error.isError()) throw error;

    qDebug() << __PRETTY_FUNCTION__ << "left readoutLoop, TODO: read remaining data";

}

void SIS3153ReadoutWorker::stop()
{
    qDebug() << __PRETTY_FUNCTION__;
    if (m_state == DAQState::Running || m_state == DAQState::Paused)
        m_desiredState = DAQState::Stopping;
}

void SIS3153ReadoutWorker::pause()
{
    qDebug() << __PRETTY_FUNCTION__;
    if (m_state == DAQState::Running)
        m_desiredState = DAQState::Paused;
}

void SIS3153ReadoutWorker::resume()
{
    qDebug() << __PRETTY_FUNCTION__;
    if (m_state == DAQState::Paused)
        m_desiredState = DAQState::Running;
}

bool SIS3153ReadoutWorker::isRunning() const
{
    return m_state != DAQState::Idle;
}

void SIS3153ReadoutWorker::setState(DAQState state)
{
    qDebug() << __PRETTY_FUNCTION__ << DAQStateStrings[m_state] << "->" << DAQStateStrings[state];
    m_state = state;
    m_desiredState = state;
    emit stateChanged(state);
}

void SIS3153ReadoutWorker::logError(const QString &message)
{
    logMessage(QString("SIS3153 Error: %1").arg(message));
}

void SIS3153ReadoutWorker::logMessage(const QString &message)
{
    m_workerContext.logMessage(message);
}

void SIS3153ReadoutWorker::processReadoutResults(EventConfig *event, s32 eventConfigIndex, const vme_script::ResultList &results)
{
    using LF = listfile_v1;
    /* generate a mvme format buffer here and pass it on to the analysis */
    // TODO: write to listfile

    DataBuffer *outputBuffer = dequeue(getFreeQueue());

    if (!outputBuffer)
    {
        outputBuffer = &m_localEventBuffer;
    }


    outputBuffer->used = 0;

    size_t resultOutputBytes = calculate_result_size(results);
    outputBuffer->ensureCapacity(2 * resultOutputBytes);

    u32 *eventHeader = outputBuffer->asU32();
    outputBuffer->used += sizeof(u32);
    *eventHeader = (ListfileSections::SectionType_Event << LF::SectionTypeShift) & LF::SectionTypeMask;
    *eventHeader |= (eventConfigIndex << LF::EventTypeShift) & LF::EventTypeMask;

    /* EventHeader
     * ModuleHeader
     *  data
     *  EndMarker
     * ModuleHeader
     *  data
     *  EndMarker
     * EndMarker
     */


    s32 eventSize = 0;                  // size of the event section in 32-bit words.
    s32 moduleSize = 0;                 // size of the module section in 32-bit words.
    auto modules = event->modules;
    s32 moduleIndex = 0;
    u32 *moduleHeader = nullptr;

    for (auto &result: results)
    {
        if (result.error.isError())
            continue;

        if (moduleIndex >= modules.size())
            break;

        if (!moduleHeader)
        {
            moduleHeader = outputBuffer->asU32();
            outputBuffer->used += sizeof(u32);
            auto moduleConfig = modules[moduleIndex];
            *moduleHeader = (((u32)moduleConfig->getModuleMeta().typeId) << LF::ModuleTypeShift) & LF::ModuleTypeMask;
            ++eventSize;
            moduleSize = 0;
        }

        switch (result.command.type)
        {
            case CommandType::Read:
                {
                    *outputBuffer->asU32() = result.value;
                    outputBuffer->used += sizeof(u32);
                    ++eventSize;
                    ++moduleSize;
                } break;

            case CommandType::BLT:
            case CommandType::BLTFifo:
            case CommandType::MBLT:
            case CommandType::MBLTFifo:
                {
                    u32 *dst = reinterpret_cast<u32 *>(outputBuffer->data + outputBuffer->used);

                    for (u32 data: result.valueVector)
                    {
                        *dst++ = data;
                    }

                    s32 vecSize = result.valueVector.size();
                    outputBuffer->used += vecSize * sizeof(u32);
                    eventSize += vecSize;
                    moduleSize += vecSize;
                } break;

            case CommandType::Marker:
                {
                    *outputBuffer->asU32() = result.value;
                    outputBuffer->used += sizeof(u32);
                    ++eventSize;
                    ++moduleSize;

                    if (result.value == EndMarker)
                    {
                        Q_ASSERT(moduleHeader);
                        *moduleHeader |= (moduleSize << LF::SubEventSizeShift) & LF::SubEventSizeMask;
                        moduleHeader = nullptr;
                        ++moduleIndex;
                    }
                } break;

            default:
                Q_ASSERT(!"unhandled command type");
                break;
        }
    }

    // add a final EndMarker to finish the event section
    // then write the size to the event section header
    *outputBuffer->asU32() = EndMarker;
    outputBuffer->used += sizeof(u32);
    ++eventSize;

    *eventHeader |= (eventSize << LF::SectionSizeShift) & LF::SectionSizeMask;

#if SIS_READOUT_BUFFER_DEBUG_PRINT
    qDebug() << __PRETTY_FUNCTION__ << "raw outputBuffer dump:";
    debugOutputBuffer(outputBuffer->data, outputBuffer->used);

    qDebug() << __PRETTY_FUNCTION__ << "outputBuffer dump as mvme buffer";
    QTextStream out(stderr);
    dump_mvme_buffer(out, outputBuffer, true);
#endif

    if (outputBuffer != &m_localEventBuffer)
    {
        enqueue_and_wakeOne(getFullQueue(), outputBuffer);
    }
}
