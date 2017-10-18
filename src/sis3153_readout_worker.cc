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
#include "util/perf.h"

#define SIS_READOUT_DEBUG               0   // enable debugging code
#define SIS_READOUT_BUFFER_DEBUG_PRINT  0   // print buffers to console
#define SIS_READOUT_BUFFER_DEBUG_FILE   0   // print buffers to buffer.log

using namespace vme_script;

namespace
{
    void validate_vme_config(VMEConfig *vmeConfig)
    {
    }

    static const size_t LocalBufferSize = Megabytes(1);
    static const size_t ReadBufferSize  = Megabytes(1);
    static const size_t TimetickBufferSize = sizeof(u32);

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

        // TODO: maybe make MBLT word swap optional
        /* The MBLT address modifier would be 0x8. Setting bit 10 (-> 0x0408)
         * makes the SIS3153 swap the two 32-bit words of the 64-bit word read
         * via MBLT. */
        unsigned int vme_am_mode     = mblt ? 0x0408 : 0xB;

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

//
// SIS3153ReadoutWorker
//
SIS3153ReadoutWorker::SIS3153ReadoutWorker(QObject *parent)
    : VMEReadoutWorker(parent)
    , m_readBuffer(ReadBufferSize)
    , m_localEventBuffer(LocalBufferSize)
    , m_localTimetickBuffer(TimetickBufferSize)
    , m_listfileHelper(nullptr)
{
}

SIS3153ReadoutWorker::~SIS3153ReadoutWorker()
{
}

void SIS3153ReadoutWorker::start(quint32 cycles)
{
    qDebug() << __PRETTY_FUNCTION__ << "cycles to run =" << cycles;

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
        QVariantMap controllerSettings = m_workerContext.vmeConfig->getControllerSettings();

        if (controllerSettings.value("useJumboFrames").toBool())
        {
            logMessage("Enabling Jumbo Frame Support");
            error = make_sis_error(sis->getImpl()->set_UdpSocketEnableJumboFrame());
        }
        else
        {
            logMessage("Disabling Jumbo Frame Support");
            error = make_sis_error(sis->getImpl()->set_UdpSocketDisableJumboFrame());
        }

        if (error.isError())
        {
            throw QString("Error setting SIS3153 jumbo frame option: %1").arg(error.toString());
        }

        //
        // Reset StackList configuration registers
        //
        for (s32 stackIndex = 0;
             stackIndex < SIS3153Constants::NumberOfStackLists;
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

        m_eventConfigsByStackList.fill(nullptr);

        s32 stackListIndex = 0;
        u32 stackLoadAddress = SIS3153ETH_STACK_RAM_START_ADDR;
        //u32 stackListControlValue = SIS3153Registers::StackListControlValues::ListBufferEnable;
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

                // set write start address to  SIS3153ETH_STACK_LIST*_CONFIG
                // set trigger condition in SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE
                // increment stackListIndex and stackLoadAddress

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
                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_CONFIG + 2 * stackListIndex, configValue);
                if (error.isError()) throw error;

                // stack list trigger source
                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE + 2 * stackListIndex,
                                           event->irqLevel);
                if (error.isError()) throw error;

                m_eventConfigsByStackList[stackListIndex] = event;
                m_eventIndexByStackList[stackListIndex] = eventIndex;
                stackListIndex++;
                stackLoadAddress += stackList.size();
                stackListControlValue |= SIS3153Registers::StackListControlValues::StackListEnable;
            }
            else if (event->triggerCondition == TriggerCondition::Periodic)
            {
                // TODO: move this check into validate_vme_config()
                if (nextTimerTriggerSource > SIS3153Registers::TriggerSourceTimer2)
                    throw QString("SIS3153 supports no more than 2 periodic events!");

                // TODO: implement support for both timers
                // TODO: store timer interval in eventConfig

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
                s32 resultCode = sis->getImpl()->udp_sis3153_register_dma_write(
                    stackLoadAddress, stackList.data(), stackList.size() - 1, &wordsWritten);
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

                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_CONFIG + 2 * stackListIndex, configValue);
                if (error.isError()) throw error;

                // stack list trigger source
                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE + 2 * stackListIndex,
                                           nextTimerTriggerSource);
                if (error.isError()) throw error;

                // timer setup
                error = sis->writeRegister(timerConfigRegister, timerValue);
                if (error.isError()) throw error;

                m_eventConfigsByStackList[stackListIndex] = event;
                m_eventIndexByStackList[stackListIndex] = eventIndex;
                stackListIndex++;
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
                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_CONFIG + 2 * stackListIndex, configValue);
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
                error = sis->writeRegister(SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE + 2 * stackListIndex,
                                           triggerSourceValue);
                if (error.isError()) throw error;

                m_eventConfigsByStackList[stackListIndex] = event;
                m_eventIndexByStackList[stackListIndex] = eventIndex;
                stackListIndex++;
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
        m_multiEventPacketCount = 0;

        // enter DAQ mode
        m_stackListControlRegisterValue = stackListControlValue;
        error = sis->writeRegister(SIS3153ETH_STACK_LIST_CONTROL, stackListControlValue);
        if (error.isError()) throw error;

        m_listfileHelper = std::make_unique<DAQReadoutListfileHelper>(m_workerContext);

        //
        // Readout
        //
        logMessage(QSL(""));
        logMessage(QSL("Entering readout loop"));
        m_workerContext.daqStats->start();
        m_listfileHelper->beginRun();

        //readoutLoop();
        readoutLoop_cleanup();

        m_listfileHelper->endRun();
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

void SIS3153ReadoutWorker::readoutLoop_cleanup()
{
    auto sis = m_sis;
    DAQStats *daqStats = m_workerContext.daqStats;
    VMEError error;

    setState(DAQState::Running);
    QTime elapsedTime;
    elapsedTime.start();
    timetick(); // initial timetick

    while (true)
    {
        processQtEvents();
        s32 elapsedSeconds = elapsedTime.elapsed() / 1000;
        if (elapsedSeconds >= 1)
        {
            do
            {
                timetick();
            } while (--elapsedSeconds);
            elapsedTime.restart();

            // debug print packet counts every second
            for (size_t i=0; i<m_packetCountsByStack.size(); ++i)
            {
                if (m_packetCountsByStack[i])
                {
                    qDebug("packetCountsByStack[%lu] = %lu", i, m_packetCountsByStack[i]);
                }

            }

            if (m_multiEventPacketCount)
            {
                qDebug("multiEventPacketCount = %lu", m_multiEventPacketCount);
            }
        }

        // stay in running state
        if (likely(m_state == DAQState::Running && m_desiredState == DAQState::Running))
        {
            readBuffer();

            if (unlikely(m_cyclesToRun > 0))
            {
                if (m_cyclesToRun == 1)
                {
                    break;
                }
                m_cyclesToRun--;
            }
        }
        // pause
        else if (m_state == DAQState::Running && m_desiredState == DAQState::Paused)
        {
            int res = sis->getCtrlImpl()->udp_sis3153_register_write(
                SIS3153ETH_STACK_LIST_CONTROL,
                m_stackListControlRegisterValue << SIS3153Registers::StackListControlValues::DisableShift);

            auto error = make_sis_error(res);
            if (error.isError())
                throw QString("Error leaving SIS3153 DAQ mode: %1").arg(error.toString());

            // read remaining buffers
            while(readBuffer().bytesRead > 0);

            setState(DAQState::Paused);
            logMessage(QSL("SIS3153 readout paused"));
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            int res = sis->getCtrlImpl()->udp_sis3153_register_write(
                SIS3153ETH_STACK_LIST_CONTROL,
                m_stackListControlRegisterValue);

            auto error = make_sis_error(res);
            if (error.isError())
                throw QString("Error entering SIS3153 DAQ mode: %1").arg(error.toString());

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

    // leave daq mode and read remaining data
    {
        int res = sis->getCtrlImpl()->udp_sis3153_register_write(
            SIS3153ETH_STACK_LIST_CONTROL,
            m_stackListControlRegisterValue << SIS3153Registers::StackListControlValues::DisableShift);

        auto error = make_sis_error(res);
        if (error.isError())
            throw QString("Error leaving SIS3153 DAQ mode: %1").arg(error.toString());

        while(readBuffer().bytesRead > 0);
    }
}

SIS3153ReadoutWorker::ReadBufferResult SIS3153ReadoutWorker::readBuffer()
{
    ReadBufferResult result = {};
    m_readBuffer.used = 0;

    /* SIS3153 sends 3 status bytes. To have the rest of the data be 32-bit
     * aligned use an offset of 1 byte into the buffer as the destination
     * address.
     * Note: udp_read_list_packet() is just a thin wrapper around recvfrom().
     */
    result.bytesRead = m_sis->getImpl()->udp_read_list_packet(
        reinterpret_cast<char *>(m_readBuffer.data + 1));

#if SIS_READOUT_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "bytesRead =" << result.bytesRead
        << ", errno =" << errno << ", strerror =" << std::strerror(errno)
        << ", EAGAIN=" << EAGAIN;
#endif

    if (result.bytesRead < 0)
    {
        // EAGAIN is not an error as it's used for the timeout case
        if (errno != EAGAIN)
        {
            result.error = VMEError(VMEError::ReadError, errno, std::strerror(errno));
            m_workerContext.daqStats->vmeCtrlReadErrors++;
#if SIS_READOUT_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "error from recvfrom(): " << result.error.toString();
#endif
        }
        return result;
    }

    m_workerContext.daqStats->totalBytesRead += result.bytesRead;
    m_workerContext.daqStats->totalBuffersRead++;

    if (result.bytesRead < 3)
    {
        result.error = VMEError(VMEError::CommError, -1,
                                QSL("sis3153 read returned < packetHeaderSize (3 bytes)!"));
#if SIS_READOUT_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "got < 3 bytes; returning " << result.error.toString()
            << result.bytesRead << std::strerror(errno);
#endif
        m_workerContext.daqStats->buffersWithErrors++;
        return result;
    }

    m_readBuffer.used = result.bytesRead + 1;

    u8 packetAck, packetIdent, packetStatus;
    packetAck    = m_readBuffer.data[1];
    packetIdent  = m_readBuffer.data[2];
    packetStatus = m_readBuffer.data[3];

#if SIS_READOUT_DEBUG
    qDebug("ack=0x%x, ident=0x%x, status=0x%x, bytesRead=%d, wordsRead=%ld",
           (u32)packetAck, (u32)packetIdent, (u32)packetStatus, result.bytesRead, result.bytesRead / sizeof(u32));
#endif

    processBuffer(packetAck, packetIdent, packetStatus,
                  m_readBuffer.data + sizeof(u32),
                  m_readBuffer.used - sizeof(u32));

    return result;
}

void SIS3153ReadoutWorker::processBuffer(
    u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size)
{
#if SIS_READOUT_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "buffer size =" << m_readBuffer.used << ", contents:";
    debugOutputBuffer(m_readBuffer.data, m_readBuffer.used);
    qDebug() << __PRETTY_FUNCTION__ << "end buffer contents";
#endif

    u32 action = 0;

    try
    {
        // dispatch
        if (packetAck == SIS3153Constants::MultiEventPacketAck)
        {
            m_multiEventPacketCount++;
            Q_ASSERT(m_processingState.stackList < 0);
            action = processMultiEventData(packetAck, packetIdent, packetStatus, data, size);
        }
        else
        {
            m_packetCountsByStack[packetAck & SIS3153Constants::AckStackListMask]++;
            bool isLastPacket = packetAck & SIS3153Constants::AckIsLastPacketMask;

            if (m_processingState.stackList >= 0 || !isLastPacket)
            {
                action = processPartialEventData(packetAck, packetIdent, packetStatus, data, size);
            }
            else
            {
                Q_ASSERT(isLastPacket);
                Q_ASSERT(m_processingState.stackList < 0);
                action = processSingleEventData(packetAck, packetIdent, packetStatus, data, size);
            }
        }

        // action handling
        if (!(action & ProcessorAction::KeepState) || (action & ProcessorAction::SkipInput))
        {
#if SIS_READOUT_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "resetting ProcessingState";
#endif
            m_processingState = ProcessingState();
        }

        if (action & ProcessorAction::FlushBuffer)
        {
#if SIS_READOUT_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "flushing current output buffer";
#endif
            flushCurrentOutputBuffer();
        }
    }
    catch (const end_of_buffer &)
    {
        auto msg = QString(QSL("SIS3153 Warning: (buffer #%1) end of input reached unexpectedly!"))
            .arg(m_workerContext.daqStats->totalBuffersRead);
        logMessage(msg);
#if SIS_READOUT_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << msg << "skipping input buffer";
#endif
        m_workerContext.daqStats->buffersWithErrors++;
    }
}

u32 SIS3153ReadoutWorker::processMultiEventData(
    u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size)
{
#if SIS_READOUT_DEBUG
    qDebug() << __PRETTY_FUNCTION__;
#endif
    Q_ASSERT(packetAck == SIS3153Constants::MultiEventPacketAck);
    Q_ASSERT(m_processingState.stackList < 0);

// TODO: check and log
#if 0
    if (m_processingState.stackList >= 0)
    {
        qDebug() << __PRETTY_FUNCTION__ << "error: got a multi event packet while a partial event is in progress! skipping input buffer";
        return ProcessorAction::SkipInput;
    }
#endif

    u32 action = ProcessorAction::NoneSet;
    BufferIterator iter(data, size);

#if SIS_READOUT_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "begin processing internal events";
#endif

    while (iter.longwordsLeft())
    {
        u32 eventHeader = iter.extractU32();
        u8  internalAck = eventHeader & 0xff;    // same as packetAck in non-buffered mode
        u8  internalIdent = (eventHeader >> 24) & 0xff; // Not sure about this byte
        u8  internalPacketStatus = 0; // FIXME: what's up with this?
        u16 length = ((eventHeader & 0xff0000) >> 16) | (eventHeader & 0xff00); // length in 32-bit words

#if SIS_READOUT_DEBUG
        char sbuf[256];
        snprintf(sbuf, sizeof(sbuf), "%s: embedded single event: header=0x%08x, internalIdent=0x%x, length=%u, internalAck=0x%x",
                 __PRETTY_FUNCTION__, eventHeader, (u32)internalIdent, (u32)length, (u32)internalAck);
        qDebug("%s", sbuf);
#endif

        m_packetCountsByStack[internalAck & SIS3153Constants::AckStackListMask]++;

        action = processSingleEventData(internalAck, internalIdent, internalPacketStatus, iter.buffp, length * sizeof(u32));
        iter.skip(sizeof(u32), length); // advance the local iterator by the subevent length

        if (action & ProcessorAction::SkipInput)
        {
            /* Processing the internal event did not succeed. Skip the rest of
             * the data and throw away the partially generated output buffer. */
#if SIS_READOUT_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "processSingleEventData returned SkipInput!";
#endif
            break;
        }
        else
        {
            action = ProcessorAction::FlushBuffer;
        }
    }

    if (!(action & ProcessorAction::SkipInput) && iter.bytesLeft())
    {
#if SIS_READOUT_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "Warning: bytes left at end of multievent iteration!";
        // TODO: nice log message with formatted bytes or at least the number of bytes that's left
#endif
    }

#if SIS_READOUT_DEBUG
    qDebug("%s: end processing. returning 0x%x", __PRETTY_FUNCTION__, action);
#endif

    return action;
}

/* Handles the case where no partial event assembly is in progress and
 * the buffer contains a single complete event. */
u32 SIS3153ReadoutWorker::processSingleEventData(
    u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size)
{
#if SIS_READOUT_DEBUG
    qDebug() << __PRETTY_FUNCTION__;
#endif
    Q_ASSERT(packetAck != SIS3153Constants::MultiEventPacketAck);
    Q_ASSERT(packetAck & SIS3153Constants::AckIsLastPacketMask);
    Q_ASSERT(m_processingState.stackList < 0);

    int stacklist = packetAck & SIS3153Constants::AckStackListMask;

    BufferIterator iter(data, size);

    {
        u32 beginHeader = iter.extractU32();

        if ((beginHeader & SIS3153Constants::BeginEventMask) != SIS3153Constants::BeginEventResult)
        {
#if SIS_READOUT_DEBUG
            qDebug() << "invalid beginHeader"; // TODO: log message
#endif
            return ProcessorAction::SkipInput;
        }
    }

    EventConfig *eventConfig = m_eventConfigsByStackList[stacklist];
    int eventIndex = m_eventIndexByStackList[stacklist];
    if (!eventConfig)
    {
#if SIS_READOUT_DEBUG
        qDebug() << "no event config for stacklist" << stacklist; // TODO: log message
#endif
        return ProcessorAction::SkipInput;
    }

    using LF = listfile_v1;

    DataBuffer *outputBuffer = getOutputBuffer();
    outputBuffer->ensureCapacity(size * 2);

    u32 *mvmeEventHeader = outputBuffer->asU32();
    outputBuffer->used += sizeof(u32);

    *mvmeEventHeader = ((ListfileSections::SectionType_Event << LF::SectionTypeShift) & LF::SectionTypeMask)
        | ((eventIndex << LF::EventTypeShift) & LF::EventTypeMask);
    u32 eventSectionSize = 0;

    s32 moduleCount = eventConfig->modules.size();

    for (s32 moduleIndex = 0; moduleIndex < moduleCount; moduleIndex++)
    {
#if SIS_READOUT_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "moduleIndex =" << moduleIndex << ", moduleCount =" << moduleCount;
#endif
        u32 moduleSectionSize = 0;
        eventSectionSize++;
        u32 *moduleHeader = outputBuffer->asU32();
        outputBuffer->used += sizeof(u32);
        auto moduleConfig = eventConfig->modules[moduleIndex];
        *moduleHeader = (((u32)moduleConfig->getModuleMeta().typeId) << LF::ModuleTypeShift) & LF::ModuleTypeMask;

        u32 *outp = outputBuffer->asU32();

        while (true)
        {
            u32 data = iter.extractU32();
            *outp++ = data;
            moduleSectionSize++;

            if (data == EndMarker)
            {
                *moduleHeader |= (moduleSectionSize << LF::SubEventSizeShift) & LF::SubEventSizeMask;
                break;
            }
        }
        outputBuffer->used += moduleSectionSize * sizeof(u32);
        eventSectionSize += moduleSectionSize;
    }

    {
        u32 endHeader = iter.extractU32();

        if ((endHeader & SIS3153Constants::EndEventMask) != SIS3153Constants::EndEventResult)
        {
#if SIS_READOUT_DEBUG
            qDebug() << "invalid endHeader"; // TODO: log message
#endif
            return ProcessorAction::SkipInput;
        }
    }

    // Finish the event section in the output buffer: write an EndMarker
    // and update the mvmeEventHeader with the correct size.
    *(outputBuffer->asU32()) = EndMarker;
    outputBuffer->used += sizeof(u32);
    eventSectionSize++;
    *mvmeEventHeader |= (eventSectionSize << LF::SectionSizeShift) & LF::SectionSizeMask;

    return ProcessorAction::FlushBuffer;
}

u32 SIS3153ReadoutWorker::processPartialEventData(
    u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size)
{
    using LF = listfile_v1;
#if SIS_READOUT_DEBUG
    qDebug() << __PRETTY_FUNCTION__;
#endif
    Q_ASSERT(packetAck != SIS3153Constants::MultiEventPacketAck);

    int stacklist = packetAck & SIS3153Constants::AckStackListMask;
    bool isLastPacket = packetAck & SIS3153Constants::AckIsLastPacketMask;
    bool partialInProgress = m_processingState.stackList >= 0;

    Q_ASSERT(partialInProgress || !isLastPacket);

    BufferIterator iter(data, size);

    DataBuffer *outputBuffer = getOutputBuffer();
    outputBuffer->ensureCapacity(size * 2);

    if (!partialInProgress)
    {
        u32 beginHeader = iter.extractU32();

        if ((beginHeader & SIS3153Constants::BeginEventMask) != SIS3153Constants::BeginEventResult)
        {
#if SIS_READOUT_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "invalid beginHeader"; // TODO: log message
#endif
            return ProcessorAction::SkipInput;
        }

        EventConfig *eventConfig = m_eventConfigsByStackList[stacklist];
        int eventIndex = m_eventIndexByStackList[stacklist];

        if (!eventConfig)
        {
#if SIS_READOUT_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "no event config for stacklist" << stacklist; // TODO: log message
#endif
            return ProcessorAction::SkipInput;
        }

        m_processingState.stackList = packetAck & SIS3153Constants::AckStackListMask;
        m_processingState.eventSize = 0;
        m_processingState.eventHeaderOffset = outputBuffer->used;
        u32 *mvmeEventHeader = outputBuffer->asU32();
        outputBuffer->used += sizeof(u32);

        *mvmeEventHeader = ((ListfileSections::SectionType_Event << LF::SectionTypeShift) & LF::SectionTypeMask)
            | ((eventIndex << LF::EventTypeShift) & LF::EventTypeMask);

        m_processingState.moduleIndex = 0;
    }

    Q_ASSERT(m_processingState.eventHeaderOffset >= 0);
    Q_ASSERT(m_processingState.moduleIndex >= 0);

    if (stacklist != m_processingState.stackList)
    {
#if SIS_READOUT_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "stackList mismatch during partial data processing, skipping input";
#endif
        return ProcessorAction::SkipInput;
    }

    // multiple constraints:
    // - moduleIndex < moduleCount for this event
    // - if isLastPacket then the last data word should be an endHeader (0xbb0...0)
    // - else there should be no end header. just copy data into the current module section
    //
    // our EndMarker is used to find the end of module data and increment the
    // moduleIndex inside the state.

    EventConfig *eventConfig = m_eventConfigsByStackList[stacklist];
    if (!eventConfig)
    {
#if SIS_READOUT_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "no event config for stacklist" << stacklist; // TODO: log message
#endif
        return ProcessorAction::SkipInput;
    }
    const s32 moduleCount = eventConfig->modules.size();

    while (true)
    {
        auto wordsLeft = iter.longwordsLeft();

        if (wordsLeft == 0 || (isLastPacket && wordsLeft == 1))
            break;

        if (m_processingState.moduleIndex >= moduleCount)
        {
#if SIS_READOUT_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "module index out of range during partial event processing";
#endif
            return ProcessorAction::SkipInput;
        }

        if (m_processingState.moduleHeaderOffset < 0) // need a new module header?
        {
            m_processingState.moduleSize  = 0;
            m_processingState.moduleHeaderOffset = outputBuffer->used;
            m_processingState.eventSize++;
            u32 *moduleHeader = outputBuffer->asU32();
            outputBuffer->used += sizeof(u32);

            auto moduleConfig = eventConfig->modules[m_processingState.moduleIndex];

            if (!moduleConfig)
            {
#if SIS_READOUT_DEBUG
                qDebug() << __PRETTY_FUNCTION__ << "no module config for subevent"; // TODO: log message
#endif
                return ProcessorAction::SkipInput;
            }

            *moduleHeader = (((u32)moduleConfig->getModuleMeta().typeId) << LF::ModuleTypeShift) & LF::ModuleTypeMask;
        }

        // copy module data to output
        u32 *outp = outputBuffer->asU32();
        const unsigned minwords = isLastPacket ? 1 : 0;

        while (iter.longwordsLeft() > minwords)
        {
            u32 data = iter.extractU32();
            *outp++ = data;
            outputBuffer->used += sizeof(u32);
            m_processingState.moduleSize++;
            m_processingState.eventSize++;

            if (data == EndMarker)
            {
                u32 *moduleHeader = outputBuffer->asU32(m_processingState.moduleHeaderOffset);
                *moduleHeader |= (m_processingState.moduleSize << LF::SubEventSizeShift) & LF::SubEventSizeMask;
                m_processingState.moduleHeaderOffset = -1;
                m_processingState.moduleIndex++;
                break;
            }
        }
    }

    if (isLastPacket)
    {
        Q_ASSERT(iter.longwordsLeft() == 1);
        u32 endHeader = iter.extractU32();
        if ((endHeader & SIS3153Constants::EndEventMask) != SIS3153Constants::EndEventResult)
        {
#if SIS_READOUT_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "invalid endHeader"; // TODO: log message
#endif
            return ProcessorAction::SkipInput;
        }

        Q_ASSERT(m_processingState.moduleHeaderOffset < 0);
        Q_ASSERT(m_processingState.eventHeaderOffset >= 0);

        // Finish the event section in the output buffer: write an EndMarker
        // and update the mvmeEventHeader with the correct size.
        *(outputBuffer->asU32()) = EndMarker;
        outputBuffer->used += sizeof(u32);
        m_processingState.eventSize++;

        u32 *mvmeEventHeader = outputBuffer->asU32(m_processingState.eventHeaderOffset);
        *mvmeEventHeader |= (m_processingState.eventSize << LF::SectionSizeShift) & LF::SectionSizeMask;

        return ProcessorAction::FlushBuffer;
    }

    return ProcessorAction::KeepState;

#if 0

        s32 moduleCount = eventConfig->modules.size();

        for (s32 moduleIndex = 0; moduleIndex < moduleCount; moduleIndex++)
        {
            m_processingState.moduleSize = 0;
            m_processingState.moduleHeaderOffset = outputBuffer->used;
            m_processingState.eventSectionSize++;
            u32 *moduleHeader = outputBuffer->asU32();
            outputBuffer->used += sizeof(u32);

            auto moduleConfig = eventConfig->modules[moduleIndex];
            *moduleHeader = (((u32)moduleConfig->getModuleMeta().typeId) << LF::ModuleTypeShift) & LF::ModuleTypeMask;

            u32 *outp = outputBuffer->asU32();

            while (iter.longwordsLeft())
            {
                u32 data = iter.extractU32();
                *outp++ = data;
                m_processingState.moduleSectionSize++;

                if (data == EndMarker)
                {
                    *moduleHeader |= (moduleSectionSize << LF::SubEventSizeShift) & LF::SubEventSizeMask;
                    break;
                }
            }
        }
#endif
}

void SIS3153ReadoutWorker::timetick()
{
    using LF = listfile_v1;
    DataBuffer *outputBuffer = dequeue(m_workerContext.freeBuffers);

    if (!outputBuffer)
    {
        outputBuffer = &m_localTimetickBuffer;
    }

    Q_ASSERT(m_listfileHelper);
    Q_ASSERT(outputBuffer->size >= sizeof(u32));

    *outputBuffer->asU32(0) = (ListfileSections::SectionType_Timetick << LF::SectionTypeShift) & LF::SectionTypeMask;
    outputBuffer->used = sizeof(u32);

    m_listfileHelper->writeBuffer(outputBuffer);

    if (outputBuffer != &m_localTimetickBuffer)
    {
        enqueue_and_wakeOne(m_workerContext.fullBuffers, outputBuffer);
    }
    else
    {
        // dropping timetick before analysis as we had to use the local buffer
        m_workerContext.daqStats->droppedBuffers++;
    }
}

void SIS3153ReadoutWorker::flushCurrentOutputBuffer()
{
    auto outputBuffer = m_outputBuffer;

    if (outputBuffer)
    {
        m_listfileHelper->writeBuffer(outputBuffer);

        if (outputBuffer != &m_localEventBuffer)
        {
            enqueue_and_wakeOne(m_workerContext.fullBuffers, outputBuffer);
        }
        else
        {
            m_workerContext.daqStats->droppedBuffers++;
        }
        m_outputBuffer = nullptr;
    }
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

DataBuffer *SIS3153ReadoutWorker::getOutputBuffer()
{
    DataBuffer *outputBuffer = m_outputBuffer;

    if (!outputBuffer)
    {
        outputBuffer = dequeue(m_workerContext.freeBuffers);

        if (!outputBuffer)
        {
            outputBuffer = &m_localEventBuffer;
        }

        // Reset a fresh buffer
        outputBuffer->used = 0;
        m_outputBuffer = outputBuffer;
    }

    return outputBuffer;
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
        // Do Qt event processing to handle queued slots invocations (stop, pause, resume)
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

            // debug print every second
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
            // TODO: replace this with something that doesn't perform a copy of the data
            u8 packetAck, packetIdent, packetStatus;
            u32 longwordsRead;
            s32 bytesRead = sis->getImpl()->list_read_event(&packetAck, &packetIdent, &packetStatus,
                                                            m_readBuffer.asU32(0), &longwordsRead);
            bool listBufferingEnabled = true;


            if (bytesRead > 0)
            {
                daqStats->addBuffersRead(1);
                daqStats->addBytesRead(bytesRead);
                const double alpha = 0.1;

                if (packetAck != SIS3153Constants::MultiEventPacketAck)
                {
                    ++m_packetCountsByStack[packetAck & 0x7];
                }
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

                sprintf(sbuf, "received %u words, %lu bytes, packetAck=0x%x, packetIdent=0x%x, packetStatus=0x%x",
                        longwordsRead, longwordsRead * sizeof(u32), (u32)packetAck, (u32)packetIdent, (u32)packetStatus);

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

                // the ListBufferEnable case
                if (packetAck == SIS3153Constants::MultiEventPacketAck)
                {
                    debug_print("begin of listbuffer iteration");

                    BufferIterator inIter(m_readBuffer.data, m_readBuffer.used);

                    while (inIter.longwordsLeft())
                    {
                        // the buffer contains 4 byte event headers followed by data
                        u32 eventHeader = inIter.extractU32();
                        u8  status = eventHeader & 0xff;
                        u16 length = ((eventHeader & 0xff0000) >> 16) | (eventHeader & 0xff00);
                        u8  idByte = (eventHeader >> 24) & 0xff;
                        sprintf(sbuf, "embedded single event: header=0x%08x, idByte=0x%x, length=%u, status=0x%x",
                                eventHeader, (u32)idByte, (u32)length, (u32)status);
                        debug_print(sbuf);

                        ++m_packetCountsByStack[status & 0x7]; // same as packetAck in non-buffered mode

#if SIS_READOUT_BUFFER_DEBUG_FILE
                        debugOutputBuffer(qout, inIter.asU8(), length * sizeof(u32));
#endif

                        inIter.skip(sizeof(u32), length);
                    }

                    if (inIter.bytesLeft())
                    {
                        debug_print("warning: bytes left at end of listbuffer iteration");
                    }
                    debug_print("end of listbuffer iteration");
                }

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
            // FIXME: needs 2nd socket to stop and needs to read remaining buffers
            // FIXME: needs to restore the correct register value
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
