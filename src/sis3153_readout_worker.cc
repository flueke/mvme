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

#include "mvme_listfile.h"
#include "sis3153/sis3153eth.h"
#include "sis3153/sis3153ETH_vme_class.h"
#include "sis3153_util.h"
#include "util/perf.h"
#include "vme_daq.h"

#define SIS_READOUT_DEBUG               1   // enable debugging code
#define SIS_READOUT_BUFFER_DEBUG_PRINT  0   // print buffers to console

#ifndef NDEBUG
#define sis_trace(msg)\
do\
{\
    auto dbg(qDebug());\
    dbg.nospace().noquote() << __PRETTY_FUNCTION__ << " " << __FILE__ << ":" << __LINE__ << " " << msg;\
} while (0)
#else
#define sis_trace(msg)
#endif

#define sis_log(msg)\
do\
{\
    logMessage(msg);\
    auto dbg(qDebug());\
    dbg.nospace().noquote() << __PRETTY_FUNCTION__ << " " << __FILE__ << ":" << __LINE__ << " " << msg;\
} while (0)

using namespace vme_script;

namespace
{
#if 0 // TODO: implement validate_vme_config() for sis3153
    void validate_vme_config(VMEConfig *vmeConfig)
    {
    }
#endif

    /* Size of the read buffer in bytes. This is where the data from recvfrom()
     * ends up. The maximum possible size should be 1500 or 8000 bytes
     * depending on the jumbo frame setting.
     */
    static const size_t ReadBufferSize  = Kilobytes(16);

    /* Size of the local event assembly buffer. Used in case there are no free
     * buffers available from the shared queue. */
    static const size_t LocalBufferSize = Megabytes(1);

    static const size_t TimetickBufferSize = sizeof(u32);

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

    u32 timer_value_from_seconds(double s)
    {
        const double period_usecs    = s * 1e6;
        const double period_100usecs = period_usecs / 100.0;
        u32 timerValue               = period_100usecs - 1.0;
        return timerValue;
    }
}

static const u32 WatchdogPayload = 0xbeefbeef;
static const double WatchdogTimeout_s = 0.050;

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
    m_counters.packetsPerStackList.fill(0);
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
    m_logBuffers = (cycles > 0); // log buffers to GUI if number of cycles has been passed in
    VMEError error;
    setState(DAQState::Starting);

    try
    {
        logMessage(QString(QSL("SIS3153 readout starting on %1"))
                   .arg(QDateTime::currentDateTime().toString())
                   );

        //validate_vme_config(m_workerContext.vmeConfig); // throws on error

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
            //logMessage("Disabling Jumbo Frame Support");
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
            {
                throw QString("Error clearing stackListConfig[%1]: %2")
                    .arg(stackIndex)
                    .arg(error.toString());
            }

            regAddr = SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE + 2 * stackIndex;
            error = sis->writeRegister(regAddr, 0);
            if (error.isError())
            {
                throw QString("Error clearing stackListTriggerSource[%1]: %2")
                    .arg(stackIndex)
                    .arg(error.toString());
            }
        }

        //
        // Build IRQ Readout Scripts
        //

        m_eventConfigsByStackList.fill(nullptr);
        m_eventIndexByStackList.fill(-1);
        m_watchdogStackListIndex = -1;

        s32 stackListIndex = 0;
        u32 stackLoadAddress = SIS3153ETH_STACK_RAM_START_ADDR;
        u32 stackListControlValue = SIS3153Registers::StackListControlValues::ListBufferEnable;
        //u32 stackListControlValue = 0;
        u32 nextTimerTriggerSource = SIS3153Registers::TriggerSourceTimer1;

        auto eventConfigs = m_workerContext.vmeConfig->getEventConfigs();

        for (s32 eventIndex = 0;
             eventIndex < eventConfigs.size();
             ++eventIndex)
        {
            auto event = eventConfigs[eventIndex];

            // build the command stack list
            auto readoutCommands = build_event_readout_script(event);
            QVector<u32> stackList = build_stackList(sis, readoutCommands);
            u32 stackListConfigValue = 0;   // SIS3153ETH_STACK_LISTn_CONFIG
            u32 stackListTriggerValue = 0;  // SIS3153ETH_STACK_LISTn_TRIGGER_SOURCE

            qDebug() << __PRETTY_FUNCTION__ << event << ", #commands =" << readoutCommands.size();
            qDebug() << __PRETTY_FUNCTION__ << ">>>>> begin sis stackList for event" << event << ":";
            debugOutputBuffer(reinterpret_cast<u8 *>(stackList.data()), stackList.size() * sizeof(u32));
            qDebug() << __PRETTY_FUNCTION__ << "<<<<< end sis stackList";

            if (event->triggerCondition == TriggerCondition::Interrupt)
            {
                stackListTriggerValue = event->irqLevel;
                stackListControlValue |= SIS3153Registers::StackListControlValues::StackListEnable;
            }
            else if (event->triggerCondition == TriggerCondition::Periodic)
            {
                // TODO: move this check into validate_vme_config()
                if (nextTimerTriggerSource > SIS3153Registers::TriggerSourceTimer1)
                    throw QString("SIS3153 readout supports no more than 1 periodic events!");

                double period_secs = event->triggerOptions.value(QSL("sis3153.timer_period"), 0.0).toDouble();

                if (period_secs <= 0.0)
                {
                    throw QString("Invalid timer period for event %1").arg(event->objectName());
                }

                u32 timerValue = timer_value_from_seconds(period_secs);

                if (timerValue > 0xffff)
                {
                    throw QString("Maximum timer period exceeded for event %1").arg(event->objectName());
                }

                u32 timerConfigRegister = SIS3153Registers::StackListTimer1Config;

                logMessage(QString(QSL("Setting up timer for event \"%1\": period=%2 s, timerValue=%3"))
                           .arg(event->objectName())
                           .arg(period_secs)
                           .arg(timerValue)
                           );
                logMessage("");

                if (nextTimerTriggerSource == SIS3153Registers::TriggerSourceTimer2)
                    timerConfigRegister = SIS3153Registers::StackListTimer2Config;

                stackListTriggerValue = nextTimerTriggerSource;

                // timer setup
                error = sis->writeRegister(timerConfigRegister, timerValue);
                if (error.isError())
                {
                    throw QString("Error writing timerConfigRegister (%1): %2")
                        .arg(timerConfigRegister)
                        .arg(error.toString());
                }

                stackListControlValue |= SIS3153Registers::StackListControlValues::StackListEnable;

                if (nextTimerTriggerSource == SIS3153Registers::TriggerSourceTimer1)
                    stackListControlValue |= SIS3153Registers::StackListControlValues::Timer1Enable;

                if (nextTimerTriggerSource == SIS3153Registers::TriggerSourceTimer2)
                    stackListControlValue |= SIS3153Registers::StackListControlValues::Timer2Enable;

                nextTimerTriggerSource++;
            }
            else if (event->triggerCondition == TriggerCondition::Input1RisingEdge
                     || event->triggerCondition == TriggerCondition::Input1FallingEdge
                     || event->triggerCondition == TriggerCondition::Input2RisingEdge
                     || event->triggerCondition == TriggerCondition::Input2FallingEdge)
            {
                switch (event->triggerCondition)
                {
                    case TriggerCondition::Input1RisingEdge:
                        stackListTriggerValue = SIS3153Registers::TriggerSourceInput1RisingEdge;
                        break;
                    case TriggerCondition::Input1FallingEdge:
                        stackListTriggerValue = SIS3153Registers::TriggerSourceInput1FallingEdge;
                        break;
                    case TriggerCondition::Input2RisingEdge:
                        stackListTriggerValue = SIS3153Registers::TriggerSourceInput2RisingEdge;
                        break;
                    case TriggerCondition::Input2FallingEdge:
                        stackListTriggerValue = SIS3153Registers::TriggerSourceInput2FallingEdge;
                        break;

                    InvalidDefaultCase;
                }
                stackListControlValue |= SIS3153Registers::StackListControlValues::StackListEnable;
            }

            if (!stackList.isEmpty())
            {
                auto msg = (QString("Loading stackList for event \"%1\""
                                    ", stackListIndex=%2, size=%3, loadAddress=0x%4")
                            .arg(event->objectName())
                            .arg(stackListIndex)
                            .arg(stackList.size())
                            .arg(stackLoadAddress, 4, 16, QLatin1Char('0'))
                           );

                logMessage(msg);

                for (u32 line: stackList)
                {
                    logMessage(msg.sprintf("  0x%08x", line));
                }
                logMessage("");

                // 13 bit stack start address which should be relative to the
                // stack RAM start address. So it's an offset into the stack
                // memory area.
                Q_ASSERT(stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR <= (1 << 13));

                // upload stackList
                error = uploadStackList(stackLoadAddress, stackList);

                if (error.isError())
                {
                    throw QString("Error uploading stackList for event %1: %2")
                        .arg(eventIndex)
                        .arg(error.toString());
                }


                // write stack list config register
                u32 stackListConfigValue = ((stackList.size() - 1) << 16) | (stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR);

                error = sis->writeRegister(
                    SIS3153ETH_STACK_LIST1_CONFIG + 2 * stackListIndex,
                    stackListConfigValue);

                if (error.isError())
                {
                    throw QString("Error writing stackListConfig[%1]: %2")
                        .arg(stackListIndex)
                        .arg(error.toString());
                }

                // write stack list trigger source register
                error = sis->writeRegister(
                    SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE + 2 * stackListIndex,
                    stackListTriggerValue);


                if (error.isError())
                {
                    throw QString("Error writing stackListTriggerSource[%1]: %2")
                        .arg(stackListIndex)
                        .arg(error.toString());
                }

                m_eventConfigsByStackList[stackListIndex] = event;
                m_eventIndexByStackList[stackListIndex] = eventIndex;
                stackListIndex++;
                stackLoadAddress += stackList.size();
            }
        }

        /* Use timer2 as a watchdog.
         *
         * Each timer has a watchdog bit. If the bit is set the timer is
         * restarted with every packet that the controller sends out.
         *
         * Just setting up the timer configuration register and enabling the
         * timer in the stackList control is not enough though: to actually
         * produce packets the watchdog timer needs to be associated with a
         * stackList (just like for periodic events above).
         *
         * If we still have an unused (and thus empty) stackList we can use it
         * for the watchdog.
         *
         */
        if (stackListIndex <= SIS3153Constants::NumberOfStackLists)
        {
            // Actually have to upload a stacklist for things to work. This
            // list consists of a single marker command.
            vme_script::Command cmd;
            cmd.type = vme_script::CommandType::Marker;
            cmd.value = WatchdogPayload;
            vme_script::VMEScript watchdogScript = { cmd };

            auto stackList = build_stackList(sis, watchdogScript);

            // upload stacklist
            auto msg = (QString("Loading stackList for internal watchdog event"
                                ", stackListIndex=%2, size=%3, loadAddress=0x%4")
                        .arg(stackListIndex)
                        .arg(stackList.size())
                        .arg(stackLoadAddress, 4, 16, QLatin1Char('0'))
                       );

            logMessage(msg);
            for (u32 line: stackList)
            {
                logMessage(msg.sprintf("  0x%08x", line));
            }
            logMessage("");

            Q_ASSERT(stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR <= (1 << 13));

            error = uploadStackList(stackLoadAddress, stackList);

            if (error.isError())
            {
                throw QString("Error uploading stackList for watchdog event: %1")
                    .arg(error.toString());
            }

            u32 stackListConfigValue = ((stackList.size() - 1) << 16) | (stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR);

            error = sis->writeRegister(
                SIS3153ETH_STACK_LIST1_CONFIG + 2 * stackListIndex,
                stackListConfigValue);

            if (error.isError())
            {
                throw QString("Error writing stackListConfig[%1]: %2")
                    .arg(stackListIndex)
                    .arg(error.toString());
            }

            // watchdogPeriod - This has to work together with the udp socket read timeout.
            // If it's higher than the read timeout warnings about not
            // receiving any data will appear in the log.
            // The period should also not be too short as that creates lots of
            // useless packets.

            u32 timerValue = timer_value_from_seconds(WatchdogTimeout_s);
            u32 timerConfigRegister = SIS3153Registers::StackListTimer2Config;

            logMessage(QString(QSL("Setting up watchdog using timer2: timeout=%1 s, timerValue=%2, stackList=%3"))
                       .arg(WatchdogTimeout_s)
                       .arg(timerValue)
                       .arg(stackListIndex)
                       );

            timerValue |= SIS3153Registers::StackListTimerWatchdogEnable;

            error = sis->writeRegister(timerConfigRegister, timerValue);

            if (error.isError())
            {
                throw QString("Error writing timerConfigRegister for watchdog (%1): %2")
                    .arg(timerConfigRegister)
                    .arg(error.toString());
            }

            error = sis->writeRegister(
                SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE + 2 * stackListIndex,
                SIS3153Registers::TriggerSourceTimer2);

            if (error.isError())
            {
                throw QString("Error writing stackListTriggerSource[%1]: %2")
                    .arg(stackListIndex)
                    .arg(error.toString());
            }

            m_watchdogStackListIndex = stackListIndex;
            stackListControlValue |= SIS3153Registers::StackListControlValues::StackListEnable;
            stackListControlValue |= SIS3153Registers::StackListControlValues::Timer2Enable;
        }
        else
        {
            sis_log(QString("SIS3153 warning: No stackList available for use as a watchdog. Disabling watchdog."));
        }

        // All event stacks have been uploaded. stackListControlValue has been set.

        //
        // DAQ Init
        //
        vme_daq_init(m_workerContext.vmeConfig, sis, [this] (const QString &msg) { logMessage(msg); });

        //
        // Debug Dump of SIS3153 registers
        //
        logMessage(QSL(""));
        dump_registers(sis, [this] (const QString &line) { this->logMessage(line); });

        logMessage(QSL(""));
        sis_log(QString("StackListControl: %1")
                .arg(format_sis3153_stacklist_control_value(stackListControlValue & 0xffff)));

        //
        // Debug: record raw buffers to file
        //
        auto ctrlSettings = m_workerContext.vmeConfig->getControllerSettings();;
        if (ctrlSettings.value("DebugRawBuffers").toBool())
        {
            m_rawBufferOut.setFileName("sis3153_raw_buffers.bin");
            if (!m_rawBufferOut.open(QIODevice::WriteOnly))
            {
                auto msg = (QString("Error opening SIS3153 raw buffers file for writing: %1")
                            .arg(m_rawBufferOut.errorString()));
                logMessage(msg);
                qDebug() << __PRETTY_FUNCTION__ << msg;
            }
            else
            {
                auto msg = (QString("Writing raw SIS3153 buffers to %1")
                            .arg(m_rawBufferOut.fileName()));
                logMessage(msg);
                qDebug() << __PRETTY_FUNCTION__ << msg;
            }
        }

        m_counters = {};
        m_counters.packetsPerStackList.fill(0);
        m_counters.watchdogStackList = m_watchdogStackListIndex;

        /* Save the current state of stackListControlValue for
         * leaving/re-entering DAQ mode later on. */
        m_stackListControlRegisterValue = stackListControlValue;

        // enter DAQ mode
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

        readoutLoop();

        m_listfileHelper->endRun();
        m_workerContext.daqStats->stop();
        logMessage(QSL("Leaving readout loop"));
        logMessage(QSL(""));

        //
        // DAQ Stop
        //
        vme_daq_shutdown(m_workerContext.vmeConfig, sis, [this] (const QString &msg) { logMessage(msg); });

        //
        // Debug: close raw buffers file
        //
        if (m_rawBufferOut.isOpen())
        {
            auto msg = (QString("Closing SIS3153 raw buffers file %1")
                        .arg(m_rawBufferOut.fileName()));
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg;

            m_rawBufferOut.close();
        }

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

VMEError SIS3153ReadoutWorker::uploadStackList(u32 stackLoadAddress, QVector<u32> stackList)
{
    auto sis = qobject_cast<SIS3153 *>(m_workerContext.controller);

    u32 wordsWritten = 0;

    auto error = make_sis_error(sis->getImpl()->udp_sis3153_register_dma_write(
            stackLoadAddress, stackList.data(), stackList.size() - 1, &wordsWritten));

    qDebug() << __PRETTY_FUNCTION__
        << "uploaded stackList to offset 0x" << QString::number(stackLoadAddress, 16)
        << ", wordsWritten =" << wordsWritten;

    return error;
}

void SIS3153ReadoutWorker::readoutLoop()
{
    auto sis = m_sis;
    DAQStats *daqStats = m_workerContext.daqStats;
    VMEError error;

    setState(DAQState::Running);
    QTime elapsedTime;
    QTime logReadErrorTimer;
    static const int LogInterval_ReadError_ms = 5000;
    u32 readErrorCount = 0;


    elapsedTime.start();
    logReadErrorTimer.start();

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
        }

        // stay in running state
        if (likely(m_state == DAQState::Running && m_desiredState == DAQState::Running))
        {
            auto readResult = readBuffer();

            if (readResult.bytesRead <= 0)
            {
                qDebug() << __PRETTY_FUNCTION__ << "Running state: got <= 0 bytes:" << readResult.error.toString();

                readErrorCount++;

                if (logReadErrorTimer.elapsed() >= LogInterval_ReadError_ms)
                {
                    auto msg = (QString("SIS313 Warning: received no data with the past %1 reads")
                                .arg(readErrorCount)
                               );
                    logMessage(msg);
                    qDebug() << msg;

                    logReadErrorTimer.restart();
                    readErrorCount = 0;
                }
            }

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
            // This is a send/receive operation. The method internally retries
            // UDP_REQUEST_RETRY times (3 by default).
            int res = sis->getCtrlImpl()->udp_sis3153_register_write(
                SIS3153ETH_STACK_LIST_CONTROL,
                m_stackListControlRegisterValue << SIS3153Registers::StackListControlValues::DisableShift);

            auto error = make_sis_error(res);
            if (error.isError())
                throw QString("Error leaving SIS3153 DAQ mode: %1").arg(error.toString());

            // read remaining buffers
            u32 packetCount = 0;
            while(readBuffer().bytesRead > 0) packetCount++;

            sis_log(QString(QSL("SIS3153 readout left DAQ mode (%1 remaining packets received)"))
                       .arg(packetCount));

            // XXX: testing if reading from the ctrl socket yields any more data
            {
                char testBuffer[10240];
                auto res = sis->getCtrlImpl()->udp_read_list_packet(testBuffer);
                auto error = make_sis_error(res);
                sis_trace(QString("running -> pause: ctrl_socket read result: %1, %2")
                          .arg(res)
                          .arg(error.toString()));
            }

            setState(DAQState::Paused);
            sis_log(QString(QSL("SIS3153 readout paused")));
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            // TODO: probably have to reset m_processingState state here
#if 0
            int res = sis->getImpl()->udp_sis3153_register_write(
                SIS3153ETH_STACK_LIST_CONTROL,
                m_stackListControlRegisterValue);

            auto error = make_sis_error(res);
#else
            // This uses the main socket again
            auto error = sis->writeRegister(SIS3153ETH_STACK_LIST_CONTROL, m_stackListControlRegisterValue);
#endif
            if (error.isError())
                throw QString("Error entering SIS3153 DAQ mode: %1").arg(error.toString());

            setState(DAQState::Running);
            sis_log(QSL("SIS3153 readout resumed"));
        }
        // stop
        else if (m_desiredState == DAQState::Stopping)
        {
            sis_log(QSL("SIS3153 readout stopping"));
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

        u32 packetCount = 0;
        while(readBuffer().bytesRead > 0) packetCount++;

        logMessage(QString(QSL("SIS3153 readout left DAQ mode (%1 remaining packets received)"))
                   .arg(packetCount));
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
    m_readBuffer.data[0] = 0;
    result.bytesRead = m_sis->getImpl()->udp_read_list_packet(
        reinterpret_cast<char *>(m_readBuffer.data + 1));

    int readErrno = errno;

#ifdef Q_OS_WIN
    int wsaError = WSAGetLastError();
#else
    int wsaError = 0;
#endif

#if SIS_READOUT_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "bytesRead =" << result.bytesRead
        << ", errno =" << errno << ", strerror =" << std::strerror(errno)
#ifdef Q_OS_WIN
        << ", WSAGetLastError()=" << wsaError
#endif
        ;
#endif

    /* Raw buffer output for debugging purposes.
     * The file consists of a sequence of entries with each entry having the following format:
     *   s32 VMEError::errorCode (== errno)
     *   s32 wsaError from WSAGetLastError(). 0 on linux
     *   s32 dataBytes
     *   u8 data[dataBytes]
     *
     * If dataBytes is <= 0 the data entry will be of size 0. No byte order
     * conversion is done so the format is architecture dependent!
     *
     */
    if (m_rawBufferOut.isOpen())
    {
        // adjust for the padding byte
        s32 bytesRead = result.bytesRead + 1;

        m_rawBufferOut.write(reinterpret_cast<const char *>(&readErrno), sizeof(readErrno));
        m_rawBufferOut.write(reinterpret_cast<const char *>(&wsaError), sizeof(wsaError));
        m_rawBufferOut.write(reinterpret_cast<const char *>(&bytesRead), sizeof(bytesRead));
        if (bytesRead > 0)
        {
            m_rawBufferOut.write(reinterpret_cast<const char *>(m_readBuffer.data), bytesRead);
        }
    }

    if (result.bytesRead < 0)
    {
        result.error = VMEError(VMEError::ReadError, errno, std::strerror(errno));
        // EAGAIN is not an error as it's used for the timeout case
        if (errno != EAGAIN)
        {
            auto msg = QString(QSL("SIS3153 Warning: %1").arg(result.error.toString()));
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg;
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
                                QSL("sis3153 read < packetHeaderSize (3 bytes)!"));
        auto msg = QString(QSL("SIS3153 Warning: %1").arg(result.error.toString()));
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;

#if 0
        qDebug() << __PRETTY_FUNCTION__ << "got < 3 bytes; returning " << result.error.toString()
            << result.bytesRead << std::strerror(errno);
#endif
        m_workerContext.daqStats->buffersWithErrors++;
        return result;
    }

    m_readBuffer.used = result.bytesRead + 1; // account for the padding byte

    u8 packetAck, packetIdent, packetStatus;
    packetAck    = m_readBuffer.data[1];
    packetIdent  = m_readBuffer.data[2];
    packetStatus = m_readBuffer.data[3];

    const auto bufferNumber = m_workerContext.daqStats->totalBuffersRead;

    sis_trace(QString("buffer #%1: ack=0x%2, ident=0x%3, status=0x%4, bytesRead=%5, wordsRead=%6")
              .arg(bufferNumber)
              .arg((u32)packetAck, 2, 16, QLatin1Char('0'))
              .arg((u32)packetIdent, 2, 16, QLatin1Char('0'))
              .arg((u32)packetStatus, 2, 16, QLatin1Char('0'))
              .arg(result.bytesRead)
              .arg(result.bytesRead / sizeof(u32)));

    // Compensate for the first word which contains the ack, ident and status
    // bytes and a fillbyte.
    u8 *dataPtr     = m_readBuffer.data + sizeof(u32);
    size_t dataSize = m_readBuffer.used - sizeof(u32);

#if 0
    /* Special handling for the watchdog stackList (for which there exists no
     * event config). */
    if ((packetAck & SIS3153Constants::AckStackListMask) == m_watchdogStackListIndex)
    {
        Q_ASSERT(m_eventConfigsByStackList[packetAck & SIS3153Constants::AckStackListMask] == nullptr);
        m_counters.packetsPerStackList[packetAck & SIS3153Constants::AckStackListMask]++;

#if SIS_READOUT_DEBUG
        u32 *data = reinterpret_cast<u32 *>(dataPtr);
        size_t dataWords = dataSize/sizeof(u32);
        Q_ASSERT(dataWords == 3);
        Q_ASSERT((data[0] & SIS3153Constants::BeginEventMask) == SIS3153Constants::BeginEventResult);
        Q_ASSERT(data[1] == 0xbeefbeef);
        Q_ASSERT((data[2] & SIS3153Constants::EndEventMask) == SIS3153Constants::EndEventResult);

        for (size_t i=0; i<dataSize/sizeof(u32); i++)
        {
            qDebug("watchdog brought something home: 0x%08x", data[i]);
        }
#endif
    }
    else
    {
        processBuffer(packetAck, packetIdent, packetStatus, dataPtr, dataSize);
    }
#else
    processBuffer(packetAck, packetIdent, packetStatus, dataPtr, dataSize);
#endif

    return result;
}

void SIS3153ReadoutWorker::processBuffer(
    u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size)
{
    const auto bufferNumber = m_workerContext.daqStats->totalBuffersRead;


#if SIS_READOUT_BUFFER_DEBUG_PRINT
    sis_trace(QString("buffer #%1, buffer_size=%2, contents:")
              .arg(bufferNumber)
              .arg(size));

    debugOutputBuffer(m_readBuffer.data, m_readBuffer.used);

    sis_trace(QString("end of buffer contents (buffer #%1)")
              .arg(bufferNumber));
#else
    sis_trace(QString("buffer #%1, buffer_size=%2")
              .arg(bufferNumber)
              .arg(size));
#endif

    if (m_logBuffers)
    {
        sis_log(QString(">>> Begin buffer #%1").arg(bufferNumber));

        logBuffer(BufferIterator(data, size), [this](const QString &str) { logMessage(str); });

        sis_log(QString("<<< End buffer #%1") .arg(bufferNumber));
    }

    u32 action = 0;

    try
    {
        // dispatch


        // multiple events per packet
        if (packetAck == SIS3153Constants::MultiEventPacketAck)
        {
            sis_trace(QString("buffer #%1 -> multi event buffer").arg(bufferNumber));
            m_counters.multiEventPackets++;
            /* Asserts that no partial event assembly is in progress. */
            Q_ASSERT(m_processingState.stackList < 0);
            action = processMultiEventData(packetAck, packetIdent, packetStatus, data, size);
        }
        else
        {
            bool isLastPacket = packetAck & SIS3153Constants::AckIsLastPacketMask;

            // start or continue partial event processing
            if (m_processingState.stackList >= 0 || !isLastPacket)
            {
                sis_trace(QString("buffer #%1 -> partial event buffer").arg(bufferNumber));
                action = processPartialEventData(packetAck, packetIdent, packetStatus, data, size);
            }
            else // the single event per packet case
            {
                sis_trace(QString("buffer #%1 -> single event buffer").arg(bufferNumber));
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

            if (action & ProcessorAction::SkipInput)
            {
                // One of the processing functions gave up and told us to skip
                // the input buffer.
                m_workerContext.daqStats->buffersWithErrors++;
            }
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
        m_workerContext.daqStats->buffersWithErrors++;
        auto msg = QString(QSL("SIS3153 Warning: (buffer #%1) end of input reached unexpectedly! Skipping buffer."))
            .arg(m_workerContext.daqStats->totalBuffersRead);
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;
    }

    if (m_outputBuffer && m_outputBuffer != &m_localEventBuffer)
    {
        // We still hold onto one of the buffers from obtained from the free
        // queue. This can happen for the SkipInput case. Put the buffer back
        // into the free queue.
        enqueue(m_workerContext.freeBuffers, m_outputBuffer);
        m_outputBuffer = nullptr;
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

    const auto bufferNumber = m_workerContext.daqStats->totalBuffersRead;

    if (m_processingState.stackList >= 0)
    {
        auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) got multievent packet while partial event processing is in progress!"
                                " Skipping buffer."))
                    .arg(bufferNumber));
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;
        return ProcessorAction::SkipInput;
    }

    u32 action = ProcessorAction::NoneSet;
    BufferIterator iter(data, size);

#if SIS_READOUT_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "begin processing internal events";
#endif

    while (iter.longwordsLeft())
    {
        /*
    Dann 4 Bytes Single Event Header (anstelle von 3Byte):
	  0x5x   "upper-length-byte"   "lower-length-byte"   "status"
    seems to be: header=0x00250058
    -> 0x00 len[0] len[1] ack
    -> 0x00 0x25   0x00   0x58
      */
        u32 eventHeader    = iter.extractU32();
        u8  internalAck    = eventHeader & 0xff;    // same as packetAck in non-buffered mode
        u8  internalIdent  = (eventHeader >> 24) & 0xff; // Not sure about this byte
        u8  internalStatus = 0; // FIXME: what's up with this?
        u16 length = ((eventHeader & 0xff0000) >> 16) | (eventHeader & 0xff00); // length in 32-bit words

        sis_trace(QString("buffer #%1: embedded ack=0x%2, ident=0x%3, status=0x%4, length=%5 (%6 bytes), header=0x%7")
                  .arg(bufferNumber)
                  .arg((u32)internalAck, 2, 16, QLatin1Char('0'))
                  .arg((u32)internalIdent, 2, 16, QLatin1Char('0'))
                  .arg((u32)internalStatus, 2, 16, QLatin1Char('0'))
                  .arg(length)
                  .arg(length * sizeof(u32))
                  .arg(eventHeader, 8, 16, QLatin1Char('0')));

        // Forward the embedded event to processSingleEventData

        action = processSingleEventData(internalAck, internalIdent, internalStatus, iter.buffp, length * sizeof(u32));

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
            iter.skip(sizeof(u32), length); // advance the local iterator by the embedded events length
            action = ProcessorAction::FlushBuffer;
        }
    }

    if (!(action & ProcessorAction::SkipInput) && iter.bytesLeft())
    {
        auto msg = QString(QSL("SIS3153 Warning: (buffer #%1) %2 bytes left at end of multievent iteration!"))
            .arg(bufferNumber)
            .arg(iter.bytesLeft());
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;
    }

#if SIS_READOUT_DEBUG
    qDebug("%s: end processing. returning 0x%x", __PRETTY_FUNCTION__, action);
#endif

    return action;
}

/* Handles the case where no partial event assembly is in progress and
 * the buffer contains a single complete event.
 * Also called by processMultiEventData() for each embedded event! */
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
    m_counters.packetsPerStackList[stacklist]++;
    const auto bufferNumber = m_workerContext.daqStats->totalBuffersRead;

    BufferIterator iter(data, size);

    {
        u32 beginHeader = iter.extractU32();

        if ((beginHeader & SIS3153Constants::BeginEventMask) != SIS3153Constants::BeginEventResult)
        {
            auto msg = QString(QSL("SIS3153 Warning: (buffer #%1) Invalid beginHeader 0x%2 (singleEvent). Skipping buffer."))
                .arg(bufferNumber)
                .arg(beginHeader, 8, 16, QLatin1Char('0'));
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg;

            return ProcessorAction::SkipInput;
        }
    }

    // Watchdog packet handling
    if (stacklist == m_watchdogStackListIndex)
    {
        Q_ASSERT(m_eventConfigsByStackList[packetAck & SIS3153Constants::AckStackListMask] == nullptr);
        u32 watchdogPayload = iter.extractU32();
        if (watchdogPayload != WatchdogPayload)
        {
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) Unexpected watchdog payload: 0x%2"))
                        .arg(bufferNumber)
                        .arg(watchdogPayload, 8, 16, QLatin1Char('0')));
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg;
            return ProcessorAction::SkipInput;
        }

        u32 endHeader = iter.extractU32();
        if ((endHeader & SIS3153Constants::EndEventMask) != SIS3153Constants::EndEventResult)
        {
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) Invalid watchdog endHeader: 0x%2"))
                        .arg(bufferNumber)
                        .arg(endHeader, 8, 16, QLatin1Char('0')));
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg;
            return ProcessorAction::SkipInput;
        }

        if (iter.bytesLeft() > 0)
        {
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) %2 bytes left at end of watchdog packet"))
                        .arg(bufferNumber)
                        .arg(iter.bytesLeft()));
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg;
            return ProcessorAction::SkipInput;
        }

        /* The watchdog packet was well formed. As we did not acquire an output.
         */
        return ProcessorAction::KeepState;
    }


    // Normal single event processing

    EventConfig *eventConfig = m_eventConfigsByStackList[stacklist];
    int eventIndex = m_eventIndexByStackList[stacklist];
    if (!eventConfig)
    {
        auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) No eventConfig for stackList=%2 -> eventIndex=%3 (singleEvent)."
                               " Skipping buffer."))
                    .arg(bufferNumber)
                    .arg(stacklist)
                    .arg(eventIndex));
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;

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
        eventSectionSize++;
        u32 *moduleHeader = outputBuffer->asU32();
        outputBuffer->used += sizeof(u32);
        auto moduleConfig = eventConfig->modules[moduleIndex];
        *moduleHeader = (((u32)moduleConfig->getModuleMeta().typeId) << LF::ModuleTypeShift) & LF::ModuleTypeMask;
        u32 moduleSectionSize = 0;
        u32 *outp = outputBuffer->asU32();

        while (true)
        {
            u32 data = iter.extractU32();
            *outp++ = data;
            moduleSectionSize++;

            if (data == EndMarker)
            {
                break;
            }
        }
        *moduleHeader |= (moduleSectionSize << LF::SubEventSizeShift) & LF::SubEventSizeMask;
        outputBuffer->used += moduleSectionSize * sizeof(u32);
        eventSectionSize += moduleSectionSize;
        m_workerContext.daqStats->totalNetBytesRead += moduleSectionSize * sizeof(u32);
    }

    {
        u32 endHeader = iter.extractU32();

        if ((endHeader & SIS3153Constants::EndEventMask) != SIS3153Constants::EndEventResult)
        {
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) Invalid endHeader: 0x%2"))
                        .arg(bufferNumber)
                        .arg(endHeader, 8, 16, QLatin1Char('0')));
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg;

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
    m_counters.packetsPerStackList[stacklist]++;
    const auto bufferNumber = m_workerContext.daqStats->totalBuffersRead;

    Q_ASSERT(partialInProgress || !isLastPacket);
    Q_ASSERT(stacklist != m_watchdogStackListIndex);

    BufferIterator iter(data, size);

    DataBuffer *outputBuffer = getOutputBuffer();
    outputBuffer->ensureCapacity(size * 2);

    if (!partialInProgress)
    {
        u32 beginHeader = iter.extractU32();

        if ((beginHeader & SIS3153Constants::BeginEventMask) != SIS3153Constants::BeginEventResult)
        {
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) Invalid beginHeader 0x%2 (partialEvent). Skipping buffer."))
                        .arg(bufferNumber)
                        .arg(beginHeader, 8, 16, QLatin1Char('0')));
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg;
            return ProcessorAction::SkipInput;
        }

        EventConfig *eventConfig = m_eventConfigsByStackList[stacklist];
        int eventIndex = m_eventIndexByStackList[stacklist];

        if (!eventConfig)
        {
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) No eventConfig for stackList=%2 -> eventIndex=%3 (partialEvent)."
                                    " Skipping buffer."))
                        .arg(bufferNumber)
                        .arg(stacklist)
                        .arg(eventIndex));
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg;
            return ProcessorAction::SkipInput;
        }

        m_processingState.stackList = stacklist;
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
        QString msg = (QString(QSL("SIS3153 Warning: (buffer #%1) stackList mismatch during partialEvent processing"
                                  " (stackList=%2, expected=%3). Skipping buffer."))
                       .arg(bufferNumber)
                       .arg(stacklist)
                       .arg(m_processingState.stackList));
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;
        return ProcessorAction::SkipInput;
    }

    // multiple constraints:
    // - moduleIndex < moduleCount for this event
    // - if isLastPacket then the last data word should be an endHeader (0xbb0...0)
    // - else there should be no end header. just copy data into the current module section
    //
    // our EndMarker is used to find the end of module data and increment the
    // moduleIndex inside the state.

    int eventIndex = m_eventIndexByStackList[stacklist];
    EventConfig *eventConfig = m_eventConfigsByStackList[stacklist];
    if (!eventConfig)
    {
        auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) No eventConfig for stackList=%2 -> eventIndex=%3 (partialEvent)."
                                " Skipping buffer."))
                    .arg(bufferNumber)
                    .arg(stacklist)
                    .arg(eventIndex));
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;

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
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) moduleIndex out of range"
                                   " (eventIndex=%2, moduleIndex=%3, moduleCount=%4). Skipping buffer."))
                        .arg(bufferNumber)
                        .arg(eventIndex)
                        .arg(m_processingState.moduleIndex)
                        .arg(moduleCount));
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg;

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
                auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) no moduleConfig for eventIndex=%2, moduleIndex=%3."
                                        " Skipping buffer."))
                            .arg(bufferNumber)
                            .arg(eventIndex)
                            .arg(m_processingState.moduleIndex));
                logMessage(msg);
                qDebug() << __PRETTY_FUNCTION__ << msg;

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
                m_workerContext.daqStats->totalNetBytesRead += m_processingState.moduleSize * sizeof(u32);
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
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) Invalid endHeader 0x%2 (partialEvent). Skipping buffer."))
                        .arg(bufferNumber)
                        .arg(endHeader, 8, 16, QLatin1Char('0')));
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg;
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
