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

#include "sis3153_readout_worker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QThread>
#include <QUdpSocket>

#include "mvme_listfile.h"
#include "sis3153/sis3153eth.h"
#include "sis3153/sis3153ETH_vme_class.h"
#include "sis3153_util.h"
#include "util/perf.h"
#include "vme_analysis_common.h"
#include "vme_daq.h"

#define SIS_READOUT_DEBUG               0   // enable debugging code (uses sis_trace())
#define SIS_READOUT_BUFFER_DEBUG_PRINT  0   // print buffers to console

//#ifndef NDEBUG
#if 0
#define sis_trace(msg)\
do\
{\
    auto dbg(qDebug());\
    dbg.nospace().noquote() << __PRETTY_FUNCTION__ << " " << msg;\
} while (0)
#else
#define sis_trace(msg)
#endif

#ifndef QT_NO_DEBUG_OUTPUT
#define sis_log(msg)\
do\
{\
    logMessage(msg);\
    qDebug().nospace().noquote() << __PRETTY_FUNCTION__ << " " << msg;\
} while (0)
#else
#define sis_log(msg)\
do\
{\
    logMessage(msg);\
} while (0)
#endif

using namespace vme_script;

namespace
{
    static void validate_event_readout_script(const VMEScript &script)
    {
        for (auto cmd: script)
        {
            switch (cmd.type)
            {
                case CommandType::BLT:
                case CommandType::BLTFifo:
                    if (cmd.transfers > SIS3153Constants::BLTMaxTransferCount)
                        throw (QString("Maximum number of BLT transfers exceeded in '%1'").arg(to_string(cmd)));
                    break;

                case CommandType::MBLT:
                case CommandType::MBLTFifo:
                    if (cmd.transfers > SIS3153Constants::MBLTMaxTransferCount)
                        throw (QString("Maximum number of MBLT transfers exceeded in '%1'").arg(to_string(cmd)));
                    break;

                default:
                    break;
            }
        }
    }

    /* Size of the read buffer in bytes. This is where the data from recvfrom()
     * ends up. The maximum possible size should be 1500 or 8000 bytes
     * depending on the jumbo frame setting.
     */
    static const size_t ReadBufferSize  = Kilobytes(16);

    /* Size of the local event assembly buffer. Used in case there are no free
     * buffers available from the shared queue. */
    static const size_t LocalBufferSize = Megabytes(1);

    size_t calculate_stackList_size(const vme_script::VMEScript &commands)
    {
        size_t size = 2 + 2; // header and trailer

        for (const auto &command: commands)
        {
            switch (command.type)
            {
                case  CommandType::Read:
                case  CommandType::ReadAbs:
                    size += 3;
                    break;

                case  CommandType::Write:
                case  CommandType::WriteAbs:
                    size += 4;
                    break;

                case  CommandType::Marker:
                    size += 2;
                    break;

                case CommandType::BLT:
                case CommandType::BLTFifo:
                case CommandType::MBLT:
                case CommandType::MBLTFifo:
                case CommandType::MBLTSwapped:
                    size += 3;
                    break;

                case  CommandType::Wait:
                    InvalidCodePath;
                    break;

                case CommandType::SetBase:
                case CommandType::ResetBase:
                case CommandType::VMUSB_ReadRegister:
                case CommandType::VMUSB_WriteRegister:
                case CommandType::Blk2eSST64:
                case CommandType::Blk2eSST64Swapped:
                case CommandType::MVLC_WriteSpecial:
                case CommandType::MetaBlock:
                case CommandType::SetVariable:
                case CommandType::Print:
                case CommandType::MVLC_Custom:
                case CommandType::MVLC_Wait:
                case CommandType::MVLC_SignalAccu:
                case CommandType::MVLC_MaskShiftAccu:
                case CommandType::MVLC_SetAccu:
                case CommandType::MVLC_ReadToAccu:
                case CommandType::MVLC_CompareLoopAccu:
                case CommandType::MVLC_InlineStack:
                case CommandType::Accu_Set:
                case CommandType::Accu_MaskAndRotate:
                case CommandType::Accu_Test:
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

    struct BlockFlags
    {
        static const u8 FIFO            = 1u << 0;
        static const u8 MBLT            = 1u << 1;
        static const u8 MBLTWordSwap    = 1u << 2;
    };

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

    void stackList_add_block_read(u32 *list_ptr, u32 *list_buffer, u32 vme_addr, u32 vme_nof_bytes, u8 flags)
    {
        unsigned int vme_write_flag  = 0;
        unsigned int vme_fifo_mode   = (flags & BlockFlags::FIFO) ? 1 : 0;
        unsigned int vme_access_size = (flags & BlockFlags::MBLT) ? AccessSize64 : AccessSize32;
        unsigned int vme_am_mode     = (flags & BlockFlags::MBLT) ? 0x8 : 0xb;

        /* The MBLT address modifier would be 0x8. Setting bit 10 (-> 0x0408)
         * makes the SIS3153 swap the two 32-bit words of the 64-bit word read
         * via MBLT.
         * See sis3153eth::list_generate_add_vmeA32MBLT64_swapDWord_read() for
         * Tinos implementation. */
        if (flags & BlockFlags::MBLTWordSwap)
        {
            vme_am_mode |= 1u << 10;
        }

        list_buffer[*list_ptr + 0] = 0xAAAA4000 | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes >> 16) & 0xFF);
        list_buffer[*list_ptr + 1] = ((vme_am_mode & 0xFFFF) << 16) | (vme_nof_bytes & 0xFFFF);
        list_buffer[*list_ptr + 2] = vme_addr;
        *list_ptr = *list_ptr + 3;
    }

#if 0
    void stackList_add_register_write(u32 *list_ptr, u32 *list_buffer, u32 reg_addr, u32 reg_data)
    {
        list_buffer[*list_ptr + 0] = 0xAAAA1A00; // internal register / write / 4byte-size
        list_buffer[*list_ptr + 1] = 0x00000001; // 4 Bytes
        list_buffer[*list_ptr + 2] = reg_addr; //
        list_buffer[*list_ptr + 3] = reg_data; // data
        *list_ptr = *list_ptr + 4;
    }

    void stackList_add_save_count_read(u32 *list_ptr, u32 *list_buffer, u32 vme_addr, u32 vme_access_size, u32 vme_am_mode)
    {
        unsigned int vme_write_flag = 0;
        unsigned int vme_fifo_mode  = 0;
        unsigned int vme_nof_bytes  = (1u << vme_access_size);

        // set bit 13 to make the controller save the value that was read
        vme_am_mode |= (1u << 13);

        list_buffer[*list_ptr + 0] = 0xAAAA4000 | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes >> 16) & 0xFF);
        list_buffer[*list_ptr + 1] = ((vme_am_mode & 0xFFFF) << 16) | (vme_nof_bytes & 0xFFFF);
        list_buffer[*list_ptr + 2] = vme_addr;
        *list_ptr = *list_ptr + 3;

        sis_trace(QString("vme_addr=0x%1, vme_nof_bytes=%2, vme_am_mode=0x%3")
                  .arg(vme_addr, 8, 16, QLatin1Char('0'))
                  .arg(vme_nof_bytes)
                  .arg(vme_am_mode, 4, 16, QLatin1Char('0'))
                 );
    }

    void stackList_add_count_block_read(u32 *list_ptr, u32 *list_buffer, u32 vme_addr, u8 flags)
    {
        unsigned int vme_write_flag  = 0;
        unsigned int vme_fifo_mode   = (flags & BlockFlags::FIFO) ? 1 : 0;
        unsigned int vme_access_size = (flags & BlockFlags::MBLT) ? AccessSize64 : AccessSize32;
        unsigned int vme_am_mode     = (flags & BlockFlags::MBLT) ? 0x8 : 0xb;
        /* The dummy value here was taken from Tinos
         * stack_list_buffer_example.cpp. The value should not matter. */
        unsigned int vme_nof_bytes   = 0x10;

        if (flags & BlockFlags::MBLTWordSwap)
        {
            vme_am_mode |= 1u << 10;
        }

        vme_am_mode |= (1u << 12);  // bit 12=1 -> use saved blockread length

        list_buffer[*list_ptr + 0] = 0xAAAA4000 | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes >> 16) & 0xFF);
        list_buffer[*list_ptr + 1] = ((vme_am_mode & 0xFFFF) << 16) | (vme_nof_bytes & 0xFFFF); // 4 Bytes
        list_buffer[*list_ptr + 2] = (vme_addr & 0xfffffffc); // force 4-byte boundary addressing
        *list_ptr = *list_ptr + 3;

        sis_trace(QString("vme_addr=0x%1, vme_nof_bytes=%2, vme_am_mode=0x%3")
                  .arg(vme_addr, 8, 16, QLatin1Char('0'))
                  .arg(vme_nof_bytes)
                  .arg(vme_am_mode, 4, 16, QLatin1Char('0'))
                 );
    }
#endif

    /* Note: if lemoIOControlBaseValue is non-negative this functions adds writes to the
     * LemoIOControl register at the beginning and end of the stacklist. The first write
     * enables OUT2, the second at the end of the list disables OUT2. */
    QVector<u32> build_stackList(SIS3153 *sis, const vme_script::VMEScript &commands,
                                 s64 lemoIOControlBaseValue = -1)
    {
        static const size_t RegisterWriteCommandSize = 4u;
        using namespace SIS3153Registers;

        size_t stackListSize = calculate_stackList_size(commands);

        if (lemoIOControlBaseValue > 0)
        {
            stackListSize += 2 * RegisterWriteCommandSize;
        }

        // Preallocate as the sis lib uses pointers and offsets into existing memory.
        QVector<u32> result(stackListSize);
        u32 resultOffset = 0;

        auto impl = sis->getImpl();
        impl->list_generate_add_header(&resultOffset, result.data());

        if (lemoIOControlBaseValue > 0)
        {
            impl->list_generate_add_register_write(
                &resultOffset, result.data(),
                SIS3153Registers::LemoIOControl,
                static_cast<u32>(lemoIOControlBaseValue) | LemoIOControlValues::OUT2);
        }

        for (const auto &command: commands)
        {
            switch (command.type)
            {
                case  CommandType::Read:
                case  CommandType::ReadAbs:

                    stackList_add_single_read(
                        &resultOffset, result.data(),
                        command.address,
                        get_access_size(command.dataWidth),
                        command.addressMode);

                    break;

                case  CommandType::Write:
                case  CommandType::WriteAbs:

                    stackList_add_single_write(
                        &resultOffset, result.data(),
                        command.address, command.value,
                        get_access_size(command.dataWidth),
                        command.addressMode);

                    break;

                case  CommandType::Marker:
                    impl->list_generate_add_marker(&resultOffset, result.data(),
                                                   command.value);
                    break;

                case  CommandType::BLT:
                    stackList_add_block_read(&resultOffset, result.data(),
                                             command.address, command.transfers * sizeof(u32), 0);
                    break;

                case  CommandType::BLTFifo:
                    stackList_add_block_read(&resultOffset, result.data(),
                                             command.address, command.transfers * sizeof(u32), BlockFlags::FIFO);
                    break;

                case  CommandType::MBLT:
                    stackList_add_block_read(&resultOffset, result.data(),
                                             command.address, command.transfers * sizeof(u64),
                                             BlockFlags::MBLT | BlockFlags::MBLTWordSwap);
                    break;

                case  CommandType::MBLTFifo:
                    stackList_add_block_read(&resultOffset, result.data(),
                                             command.address, command.transfers * sizeof(u64),
                                             BlockFlags::FIFO | BlockFlags::MBLT | BlockFlags::MBLTWordSwap);
                    break;

                case  CommandType::MBLTSwapped:
                    stackList_add_block_read(&resultOffset, result.data(),
                                             command.address, command.transfers * sizeof(u64),
                                             BlockFlags::FIFO | BlockFlags::MBLT);
                    break;

                case  CommandType::Wait:
                    InvalidCodePath;
                    break;

                case CommandType::SetBase:
                case CommandType::ResetBase:
                case CommandType::VMUSB_ReadRegister:
                case CommandType::VMUSB_WriteRegister:
                case CommandType::Blk2eSST64:
                case CommandType::Blk2eSST64Swapped:
                case CommandType::MVLC_WriteSpecial:
                case CommandType::MetaBlock:
                case CommandType::SetVariable:
                case CommandType::Print:
                case CommandType::MVLC_Custom:
                case CommandType::MVLC_Wait:
                case CommandType::MVLC_SignalAccu:
                case CommandType::MVLC_MaskShiftAccu:
                case CommandType::MVLC_SetAccu:
                case CommandType::MVLC_ReadToAccu:
                case CommandType::MVLC_CompareLoopAccu:
                case CommandType::MVLC_InlineStack:
                case CommandType::Accu_Set:
                case CommandType::Accu_MaskAndRotate:
                case CommandType::Accu_Test:
                    break;

                case  CommandType::Invalid:
                    InvalidCodePath;
                    break;
            }
        }

        if (lemoIOControlBaseValue > 0)
        {
            impl->list_generate_add_register_write(
                &resultOffset, result.data(),
                SIS3153Registers::LemoIOControl,
                (static_cast<u32>(lemoIOControlBaseValue)
                 | (LemoIOControlValues::OUT2 << LemoIOControlValues::DisableShift)));
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

    struct ProcessorAction
    {
        static const u32 NoneSet     = 0u;
        static const u32 KeepState   = 1u << 0; // Keep the ProcessorState. If unset resets the state.
        static const u32 FlushBuffer = 1u << 1; // Flush the current output buffer and acquire a new one
        static const u32 SkipInput   = 1u << 2; // Skip the current input buffer.
                                                // Implies state reset and reuses the output buffer without
                                                // flusing it.

    };

    /* Repeating the flags here is required when compiling debug builds without
     * any optimization. If not set the build will fail with undefined
     * references to the flags. */
    const u32 ProcessorAction::NoneSet;
    const u32 ProcessorAction::KeepState;
    const u32 ProcessorAction::FlushBuffer;
    const u32 ProcessorAction::SkipInput;

    static const QHash<u32, QString> ProcessorActionStrings =
    {
        { ProcessorAction::NoneSet,     QSL("NoneSet") },
        { ProcessorAction::KeepState,   QSL("KeepState") },
        { ProcessorAction::FlushBuffer, QSL("FlushBuffer") },
        { ProcessorAction::SkipInput,   QSL("SkipInput") },
    };


    void update_endHeader_counters(SIS3153ReadoutWorker::Counters &counters, u32 endHeader, int stacklist)
    {
        counters.stackListBerrCounts_Block[stacklist] +=
            (endHeader & SIS3153Constants::EndEventBerrBlockMask) >> SIS3153Constants::EndEventBerrBlockShift;

        counters.stackListBerrCounts_Read[stacklist] +=
            (endHeader & SIS3153Constants::EndEventBerrReadMask) >> SIS3153Constants::EndEventBerrReadShift;

        counters.stackListBerrCounts_Write[stacklist] +=
            (endHeader & SIS3153Constants::EndEventBerrWriteMask) >> SIS3153Constants::EndEventBerrWriteShift;
    }

    static const double WatchdogTimeout_s = 0.050;
    static const double PauseMaxSleep_ms = 125.0;

} // end anon namespace

u64 SIS3153ReadoutWorker::Counters::receivedEventsExcludingWatchdog() const
{
    u64 result = 0u;

    for (s32 sli = 0; sli < static_cast<s32>(stackListCounts.size()); sli++)
    {
        if (sli != watchdogStackList)
            result += stackListCounts[sli];
    }

    return result;
}

u64 SIS3153ReadoutWorker::Counters::receivedEventsIncludingWatchdog() const
{
    return std::accumulate(std::begin(stackListCounts), std::end(stackListCounts),
                           static_cast<u64>(0u));
}

u64 SIS3153ReadoutWorker::Counters::receivedWatchdogEvents() const
{
    return watchdogStackList < 0 ? 0u : stackListCounts[watchdogStackList];
}

/* FIXME 18.1.2018
 * ==========================================================================
 * There are problems when doing the full "leave daq mode sequence" which
 * includes using the FlushBufferEnable bit under high data rates. The
 * workaround for now is to just stop the daq and read any udp packets that are
 * in-flight. This means parts of a buffer (<= 1500 bytes, <= 8000 bytes if
 * jumbo frames are used)) can still be queued up inside the controller. This
 * stale data will be the first data to be sent out when starting the daq
 * again. That's the reason why the 0xbb-header sequence numbers often start at
 * a number value > 1 instead of at 1 and then later on drop to 1.
 * Until I find a clean and reliable way to flush the SIS buffer I'm just
 * going to implemented a workaround: throw away all data packets at the start of
 * a run until the packet number 1 appears.
 *
 * related code: leaveDAQMode() and EventLossCounter
 */

SIS3153ReadoutWorker::EventLossCounter::EventLossCounter(Counters *counters,
                                                         VMEReadoutWorkerContext *rdoContext)
    : counters(counters)
    , rdoContext(rdoContext)
    , lastReceivedSequenceNumber(0)
    , currentFlags(Flag_IsStaleData)
{
    Q_ASSERT(counters);
    Q_ASSERT(rdoContext);
}

SIS3153ReadoutWorker::EventLossCounter::Flags
SIS3153ReadoutWorker::EventLossCounter::handleEventSequenceNumber(s32 seqNum, u64 bufferNumber)
{
    if (unlikely(currentFlags & Flag_IsStaleData))
    {
        if (seqNum == 1)
        {
#if SIS_READOUT_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "buffer #" << bufferNumber
                << ", first non-stale seqNum =" << seqNum;
#else
            (void) bufferNumber;
#endif
            currentFlags &= ~Flag_IsStaleData;
        }
        lastReceivedSequenceNumber = seqNum;
        return currentFlags;
    }

    s32 diff = seqNum - lastReceivedSequenceNumber;

    if (likely(diff == 1))
    {
        // all good, nothing lost
    }
    else if (diff > 1 && !isLeavingDAQ())
    {
        // increment lost count
        s32 lost = diff - 1;
#if SIS_READOUT_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "buffer #" << bufferNumber << ", lost" << lost << "events";
#endif
        counters->lostEvents += lost;
    }
    else if (diff < 0 && !isLeavingDAQ())
    {
        // seqNum < lastReceivedSequenceNumber which should only
        // happen once the seqNum has overflowed.
        // Perfect overflow without loss:
        // old: 0x00ffffff, new: 0 -> will yield an adjustedDiff of 0

        s32 adjustedDiff = (SIS3153Constants::BeginEventSequenceNumberMask + diff);

#if SIS_READOUT_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "buffer #" << bufferNumber << ", seqNum diff is < 0:" << diff
            << ", lastReceivedSequenceNumber =" << lastReceivedSequenceNumber
            << ", current seqNum =" << seqNum
            << ", adjustedDiff =" << adjustedDiff;
#endif

        Q_ASSERT(adjustedDiff >= 0);

        if (adjustedDiff > 0)
        {
            s32 lost = adjustedDiff - 1;
            counters->lostEvents += lost;
        }
    }
    else if (!isLeavingDAQ())
    {
        // The difference is 0. This should never happen. Either the
        // controller sends duplicate packet numbers or we lost so many
        // that an overflow occured and we ended up at exactly the same
        // packet number as before.
#if SIS_READOUT_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "buffer #" << bufferNumber << ", seqNum diff is 0!"
            << " seqNum =" << seqNum
            << ", lastReceivedSequenceNumber =" << lastReceivedSequenceNumber
            << ", currentFlags =" << static_cast<u32>(currentFlags);
#endif
        Q_ASSERT(false);
    }

    if (unlikely(isLeavingDAQ()))
    {
        // HACK: during shutdown remembers the max packet number received so
        // far. This works around the single out-of-order packet the controller
        // sends out during shutdown.
        lastReceivedSequenceNumber = std::max(lastReceivedSequenceNumber, seqNum);
#if SIS_READOUT_DEBUG
        qDebug() << __PRETTY_FUNCTION__
            << "buffer #" << bufferNumber
            << ", stored max seqNum =" << lastReceivedSequenceNumber;
#endif
    }
    else
    {
        lastReceivedSequenceNumber = seqNum;
    }

    return Flag_None;
}

void SIS3153ReadoutWorker::EventLossCounter::beginLeavingDAQ()
{
    assert(!(currentFlags & Flag_LeavingDAQ));
    currentFlags |= Flag_LeavingDAQ;
}

void SIS3153ReadoutWorker::EventLossCounter::endLeavingDAQ()
{
    assert((currentFlags & Flag_LeavingDAQ));
    currentFlags &= ~Flag_LeavingDAQ;
}

static const QVector<TriggerCondition> TriggerPriorityList =
{
    /* Highest priority to the Periodic trigger so that it is executed even if other
     * methods trigger rapidly and to avoid jitter. */
    TriggerCondition::Periodic,
    TriggerCondition::Input1RisingEdge,
    TriggerCondition::Input1FallingEdge,
    TriggerCondition::Input2RisingEdge,
    TriggerCondition::Input2FallingEdge,
    TriggerCondition::Interrupt,
};

struct EventConfigAndIndex
{
    EventConfig *config;
    int index;
};

using EventAndIndexList = QVector<EventConfigAndIndex>;

EventAndIndexList stacklist_trigger_prio_sort(QList<EventConfig *> eventConfigs)
{
    EventAndIndexList result;

    for (int ei = 0; ei < eventConfigs.size(); ei++)
    {
        result.push_back({ eventConfigs[ei], ei });
    }

    // return true if e1 < e2, false otherwise
    auto prio_cmp = [] (const EventConfigAndIndex &e1, const EventConfigAndIndex &e2) -> bool
    {
        int prio1 = TriggerPriorityList.indexOf(e1.config->triggerCondition);
        int prio2 = TriggerPriorityList.indexOf(e2.config->triggerCondition);

        if (prio1 == prio2)
            return e1.index < e2.index;

        return prio1 < prio2;
    };

    std::stable_sort(result.begin(), result.end(), prio_cmp);

    return result;
}

//
// SIS3153ReadoutWorker
//
SIS3153ReadoutWorker::SIS3153ReadoutWorker(QObject *parent)
    : VMEReadoutWorker(parent)
    , m_state(DAQState::Idle)
    , m_desiredState(DAQState::Idle)
    , m_readBuffer(ReadBufferSize)
    , m_localEventBuffer(LocalBufferSize)
    , m_listfileHelper(nullptr)
    , m_lossCounter(&m_counters, &m_workerContext)
{
}

SIS3153ReadoutWorker::~SIS3153ReadoutWorker()
{
}

VMEError SIS3153ReadoutWorker::uploadStackList(u32 stackLoadAddress, QVector<u32> stackList)
{
    auto sis = qobject_cast<SIS3153 *>(m_workerContext.controller);

    u32 wordsWritten = 0;

    auto error = make_sis_error(sis->getImpl()->udp_sis3153_register_dma_write(
            stackLoadAddress, stackList.data(), stackList.size() - 1, &wordsWritten));

    sis_trace(QString("uploaded stackList to offset 0x%1, wordsWritten=%2")
              .arg(stackLoadAddress, 8, 16, QLatin1Char('0'))
              .arg(wordsWritten)
             );

    return error;
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
        //
        // Make sure the controller is not in DAQ mode already so that we do
        // not abort someone elses DAQ run.
        //
        u32 controlReg = 0;
        error = sis->readRegister(SIS3153Registers::StackListControl, &controlReg);

        if (error.isError())
        {
            throw QString("Error reading StackListControl register: %1").arg(error.toString());
        }

        if (controlReg & SIS3153Registers::StackListControlValues::StackListEnable)
        {
            sis->close();

            throw QString("Error starting DAQ: SIS3153 already is in autonomous DAQ mode."
                          " Use \"force reset\" to attempt to reset the controller.");
        }


        sis_log(QString(QSL("SIS3153 readout starting on %1"))
                .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
               );

        //
        // Reset controller state by writing KeyResetAll register
        //
        {
            error = sis->writeRegister(SIS3153Registers::KeyResetAll, 1);
            if (error.isError())
                throw QString("Error writing SIS3153 KeyResetAll register: %1").arg(error.toString());
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

            sis_log(QString(QSL("SIS3153 (SerialNumber=%1, Firmware=%2.%3)\n"))
                    .arg(serReg)
                    .arg(fwMajor, 2, 16, QLatin1Char('0'))
                    .arg(fwMinor, 2, 16, QLatin1Char('0'))
                   );
        }

        //
        // General Setup
        //
        QVariantMap controllerSettings = m_workerContext.vmeConfig->getControllerSettings();

        if (controllerSettings.value(QSL("JumboFrames")).toBool())
        {
            sis_log("Enabling Jumbo Frame Support");
            error = make_sis_error(sis->getImpl()->set_UdpSocketEnableJumboFrame());
        }
        else
        {
            error = make_sis_error(sis->getImpl()->set_UdpSocketDisableJumboFrame());
        }

        if (error.isError())
        {
            throw QString("Error setting SIS3153 jumbo frame option: %1").arg(error.toString());
        }

        //
        // Reset StackList and other configuration registers
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

        for (u32 reg: { SIS3153Registers::StackListTimer1Config, SIS3153Registers::StackListTimer2Config })
        {
            error = sis->writeRegister(reg, 0);

            if (error.isError())
            {
                throw QString("Error clearing StackListTimerConfig (0x%1): %2")
                    .arg(reg, 8, 16, QLatin1Char('0'))
                    .arg(error.toString());
            }
        }

        //
        // Build IRQ Readout Scripts, create stacklists, upload and setup triggers
        //
        m_eventConfigsByStackList.fill(nullptr);
        m_eventIndexByStackList.fill(-1);
        m_watchdogStackListIndex = -1;
        m_lemoIORegDAQBaseValue = SIS3153Registers::LemoIOControlValues::OUT1;

        // next stack list index to use
        s32 stackListIndex = 0;

        // index of the "main" stacklist which enables OUT2 during execution or -1 if not
        // set yet.
        s32 stackListIndex_OUT2 = -1;

        u32 stackLoadAddress = SIS3153ETH_STACK_RAM_START_ADDR;
        u32 stackListControlValue = 0;

        stackListControlValue |= SIS3153Registers::StackListControlValues::DisableBerrStatus;

        if (!controllerSettings.value("DisableBuffering").toBool())
        {
            stackListControlValue |= SIS3153Registers::StackListControlValues::ListBufferEnable;
        }
        else
        {
            sis_log("Disabling packet buffering (Debug option)!");
        }

        u32 nextTimerTriggerSource = SIS3153Registers::TriggerSourceTimer1;

        auto sortedEvents = stacklist_trigger_prio_sort(m_workerContext.vmeConfig->getEventConfigs());

        for (auto eventAndIndex: sortedEvents)
        {
            EventConfig *event = eventAndIndex.config;
            int eventIndex     = eventAndIndex.index;

            /* Decision point on whether to enable OUT2 during stacklist execution of this
             * event. Basically the first non-periodic event will be used (the "main"
             * event). */
            s64 lemoIORegBaseValue = -1;

            if (stackListIndex_OUT2 < 0)
            {
                switch (event->triggerCondition)
                {
                    case TriggerCondition::Input1RisingEdge:
                    case TriggerCondition::Input1FallingEdge:
                    case TriggerCondition::Input2RisingEdge:
                    case TriggerCondition::Input2FallingEdge:
                    case TriggerCondition::Interrupt:
                        lemoIORegBaseValue = m_lemoIORegDAQBaseValue;
                        stackListIndex_OUT2 = stackListIndex;
                        break;

                    default:
                        break;
                }
            }

            // build the command stack list
            auto readoutCommands = build_event_readout_script(event);
            validate_event_readout_script(readoutCommands);
            QVector<u32> stackList = build_stackList(sis, readoutCommands, lemoIORegBaseValue);
            u32 stackListTriggerValue = 0;  // SIS3153ETH_STACK_LISTn_TRIGGER_SOURCE

            qDebug() << __PRETTY_FUNCTION__ << event << ", #commands =" << readoutCommands.size();
            qDebug() << __PRETTY_FUNCTION__ << ">>>>> begin sis stackList for event" << event << ":";
            qDebugOutputBuffer(reinterpret_cast<u8 *>(stackList.data()), stackList.size() * sizeof(u32));
            qDebug() << __PRETTY_FUNCTION__ << "<<<<< end sis stackList";

            if (event->triggerCondition == TriggerCondition::Interrupt)
            {
                stackListTriggerValue = event->irqLevel;
                stackListControlValue |= SIS3153Registers::StackListControlValues::StackListEnable;
            }
            else if (event->triggerCondition == TriggerCondition::Periodic)
            {
                if (nextTimerTriggerSource > SIS3153Registers::TriggerSourceTimer2)
                    throw QString("SIS3153 readout supports no more than 2 periodic events!");

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

                sis_log(QString(QSL("Setting up timer for event \"%1\": period=%2 s, timerValue=%3"))
                        .arg(event->objectName())
                        .arg(period_secs)
                        .arg(timerValue)
                       );
                sis_log("");

                u32 timerConfigRegister = SIS3153Registers::StackListTimer1Config;

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

                if (stackListIndex == stackListIndex_OUT2)
                {
                    msg += ", OUT2";
                }

                sis_log(msg);

                for (u32 line: stackList)
                {
                    sis_log(QStringLiteral("  0x%1").arg(line, 8, 16, QLatin1Char('0')));
                }

                sis_log("");

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

        /* Use timer2 as a watchdog if it is still available.
         *
         * Each timer has a watchdog bit. If the bit is set the timer is
         * restarted with every packet that the controller sends out.
         *
         * To force the controller to send out a packet the
         * StackListTriggerCommandFlushBuffer command is used in the stackList
         * that's associated with the watchdog.
         */
        if (!controllerSettings.value(QSL("DisableWatchdog")).toBool())
        {
            if (stackListIndex >= SIS3153Constants::NumberOfStackLists)
            {
                sis_log(QString("SIS3153 warning: No stackList available for use as a watchdog. Disabling watchdog."));
            }
            else if (nextTimerTriggerSource > SIS3153Registers::TriggerSourceTimer2)
            {
                sis_log(QString("SIS3153 warning: No timer available for use as a watchdog. Disabling watchdog."));
            }
            else
            {
                auto sisImpl = sis->getImpl();

                // [ header, reg_write, trailer ]
                QVector<u32> stackList(2 + 4 + 2);
                u32 stackListOffset = 0;

                sisImpl->list_generate_add_header(&stackListOffset, stackList.data());

                sisImpl->list_generate_add_register_write(&stackListOffset, stackList.data(),
                                                          SIS3153ETH_STACK_LIST_TRIGGER_CMD,
                                                          SIS3153Registers::StackListTriggerCommandFlushBuffer);

                sisImpl->list_generate_add_trailer(&stackListOffset, stackList.data());

                Q_ASSERT(stackListOffset == (u32)(stackList.size()));

                // upload stacklist
                auto msg = (QString("Loading stackList for internal watchdog event"
                                    ", stackListIndex=%2, size=%3, loadAddress=0x%4")
                            .arg(stackListIndex)
                            .arg(stackList.size())
                            .arg(stackLoadAddress, 4, 16, QLatin1Char('0'))
                           );

                sis_log(msg);
                for (u32 line: stackList)
                {
                    sis_log(QStringLiteral("  0x%1").arg(line, 8, 16, QLatin1Char('0')));
                }
                sis_log("");

                Q_ASSERT(stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR <= (1 << 13));

                error = uploadStackList(stackLoadAddress, stackList);

                if (error.isError())
                {
                    throw QString("Error uploading stackList for watchdog event: %1")
                        .arg(error.toString());
                }

                u32 stackListConfigValue = (((stackList.size() - 1) << 16)
                                            | (stackLoadAddress - SIS3153ETH_STACK_RAM_START_ADDR));

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

                sis_log(QString(QSL("Setting up watchdog using timer2: timeout=%1 s, timerValue=%2, stackList=%3"))
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
        }
        else
        {
            sis_log(QString("SIS3153 watchdog disabled by user setting."));
        }

        // UDP configuration register (manual section 4.2.1)
        {
            u32 packetGapValue = controllerSettings.value(QSL("UDP_PacketGap"), 0u).toUInt();
            u32 udpConfValue   = 0;

            assert(packetGapValue < SIS3153Registers::UDPConfigurationValues::GapTimeValueCount);

            sis_log(QString(QSL("Setting UDP packet gap time to %1."))
                    .arg(SIS3153Registers::UDPConfigurationValues::GapTimeValues[packetGapValue])
                   );

            error = sis->readRegister(SIS3153Registers::UDPConfiguration, &udpConfValue);

            if (error.isError())
            {
                throw QString("Error reading UDPConfiguration register (%1): %2")
                    .arg(SIS3153Registers::UDPConfiguration)
                    .arg(error.toString());
            }

            // Clear old gap value and set the new one, keeping other bits intact.
            udpConfValue &= ~SIS3153Registers::UDPConfigurationValues::GapTimeMask;
            udpConfValue |= packetGapValue & SIS3153Registers::UDPConfigurationValues::GapTimeMask;

            error = sis->writeRegister(SIS3153Registers::UDPConfiguration, udpConfValue);

            if (error.isError())
            {
                throw QString("Error writing UDPConfiguration register (%1): %2")
                    .arg(SIS3153Registers::UDPConfiguration)
                    .arg(error.toString());
            }

        }

        // All event stacks have been uploaded. stackListControlValue has been computed.

        //
        // DAQ Init
        //
        if (!do_VME_DAQ_Init(sis))
        {
            setState(DAQState::Idle);
            return;
        }

        //
        // Debug Dump of SIS3153 registers
        //
        sis_log(QSL(""));
        dump_registers(sis, [this] (const QString &line) { sis_log(line); });
        sis_log(QSL(""));

        sis_log(QString("StackListControl: %1")
                .arg(format_sis3153_stacklist_control_value(stackListControlValue & 0xffff)));

        //
        // Debug: record raw buffers to file
        //
        if (controllerSettings.value("DebugRawBuffers").toBool())
        {
            m_rawBufferOut.setFileName("sis3153_raw_buffers.bin");
            if (!m_rawBufferOut.open(QIODevice::WriteOnly))
            {
                auto msg = (QString("Error opening SIS3153 raw buffers file for writing: %1")
                            .arg(m_rawBufferOut.errorString()));
                sis_log(msg);
                qDebug() << __PRETTY_FUNCTION__ << msg;
            }
            else
            {
                auto msg = (QString("Writing raw SIS3153 buffers to %1")
                            .arg(m_rawBufferOut.fileName()));
                sis_log(msg);
                qDebug() << __PRETTY_FUNCTION__ << msg;
            }
        }

        setupUDPForwarding();

        m_listfileHelper = std::make_unique<DAQReadoutListfileHelper>(m_workerContext);
        m_processingState = {};
        m_counters = {};
        m_counters.watchdogStackList = m_watchdogStackListIndex;
        m_lossCounter = EventLossCounter(&m_counters, &m_workerContext);

        // Save the current state of stackListControlValue for
        // leaving/re-entering DAQ mode later on.
        m_stackListControlRegisterValue = stackListControlValue;

        // enter DAQ mode
        enterDAQMode(m_stackListControlRegisterValue);

        //
        // Readout
        //
        sis_log(QSL(""));
        sis_log(QSL("Entering readout loop"));
        m_workerContext.daqStats.start();
        m_listfileHelper->beginRun();

        readoutLoop();

        sis_log(QSL("Leaving readout loop"));
        sis_log(QSL(""));

        //
        // DAQ Stop
        //
        vme_daq_shutdown(m_workerContext.vmeConfig, sis, [this] (const QString &msg) { sis_log(msg); });

        //
        // Debug: close raw buffers file
        //
        if (m_rawBufferOut.isOpen())
        {
            auto msg = (QString("Closing SIS3153 raw buffers file %1")
                        .arg(m_rawBufferOut.fileName()));
            sis_log(msg);

            m_rawBufferOut.close();
        }

        sis_log(QSL(""));
        sis_log(QString(QSL("SIS3153 readout stopped on %1"))
                .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
               );

        // Note: endRun() collects the log contents, which means it should be one of the
        // last actions happening in here. Log messages generated after this point won't
        // show up in the listfile.
        m_listfileHelper->endRun();
        m_workerContext.daqStats.stop();
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
    catch (const vme_script::ParseError &e)
    {
        logError(QSL("VME Script parse error: ") + e.toString());
    }

    setState(DAQState::Idle);
}

void SIS3153ReadoutWorker::readoutLoop()
{
    VMEError error;

    setState(DAQState::Running);
    QElapsedTimer elapsedTime;
    QElapsedTimer logReadErrorTimer;
    static const int LogInterval_ReadError_ms = 5000;
    u32 readErrorCount = 0;

    elapsedTime.start();
    logReadErrorTimer.start();

    using vme_analysis_common::TimetickGenerator;

    TimetickGenerator timetickGen;
    timetick(); // immediately write out the very first timetick

    while (true)
    {
        int elapsedSeconds = timetickGen.generateElapsedSeconds();

        while (elapsedSeconds >= 1)
        {
            timetick();
            elapsedSeconds--;
        }

        // stay in running state
        if (likely(m_state == DAQState::Running && m_desiredState == DAQState::Running))
        {
            auto readResult = readAndProcessBuffer();

            if (readResult.bytesRead <= 0)
            {
                qDebug() << __PRETTY_FUNCTION__ << "Running state: got <= 0 bytes:" << readResult.error.toString();

                readErrorCount++;

                if (logReadErrorTimer.elapsed() >= LogInterval_ReadError_ms)
                {
                    auto msg = (QString("SIS313 Warning: received no data with the past %1 socket read operations")
                                .arg(readErrorCount)
                               );
                    sis_log(msg);
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
            leaveDAQMode();
            m_listfileHelper->writePauseSection();

            setState(DAQState::Paused);
            sis_log(QString(QSL("SIS3153 readout paused")));
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            m_lossCounter.currentFlags = EventLossCounter::Flag_IsStaleData;

            enterDAQMode(m_stackListControlRegisterValue);
            m_listfileHelper->writeResumeSection();

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
            QThread::msleep(std::min(PauseMaxSleep_ms, timetickGen.getTimeToNextTick_ms()));
        }
        else
        {
            InvalidCodePath;
        }
    }

    setState(DAQState::Stopping);
    leaveDAQMode();
    maybePutBackBuffer();
}

namespace
{
    void err_wrap(int sis_return_code, const QString &fmt = QString())
    {
        auto error = make_sis_error(sis_return_code);

        if (error.isError())
        {
            if (fmt.isNull() || fmt.isEmpty())
                throw error.toString();

            auto str = fmt.arg(error.toString());

            qDebug() << ">>>> SIS Error: " << str;
            throw str;
        }
    }
} // end anon namespace

void SIS3153ReadoutWorker::enterDAQMode(u32 stackListControlValue)
{
    using namespace SIS3153Registers;

    auto sis = qobject_cast<SIS3153 *>(m_workerContext.controller);

    // sis3153eth instance using the control socket.
    auto ctrlSocket = sis->getCtrlImpl();

    // Taken from sis3153/stack_list_buffer_example.cpp:
    // clear List execution counter (List-Event number)
    // This makes the next event sequence number be '1'.
    auto error = sis->writeRegister(SIS3153ETH_STACK_LIST_TRIGGER_CMD, 8);
    if (error.isError()) throw error;

    // Enter DAQ mode using the main socket. This socket will receive the
    // readout data packets.
    sis_log("Activating autonomous readout mode");
    error = sis->writeRegister(SIS3153ETH_STACK_LIST_CONTROL, stackListControlValue);
    if (error.isError()) throw error;

    // SIS should be ready to react to triggers now.

    // Turn on LED_A
    sis_log("Activating LED_A");
    err_wrap(ctrlSocket->udp_sis3153_register_write(
            USBControlAndStatus,
            USBControlAndStatusValues::LED_A));

    // Activate OUT1.
    // Note: the choice between NIM and TTL should be made via the jumpers
    // (Section 7.4 in the manual).
    sis_log("Activating OUT1");
    err_wrap(ctrlSocket->udp_sis3153_register_write(
            LemoIOControl,
            m_lemoIORegDAQBaseValue));
}

void SIS3153ReadoutWorker::leaveDAQMode()
{
    /* IMPORTANT: These operations use the control socket instead of the main socket
     * so that DAQ data packets and the register read/write packets do not get
     * mixed up. */

    using namespace SIS3153Registers;

    // sis3153eth instance using the control socket.
    auto ctrl = m_sis->getCtrlImpl();

    // Turn off LED_A
    sis_log("Deactivating LED_A");
    err_wrap(ctrl->udp_sis3153_register_write(
            USBControlAndStatus,
            USBControlAndStatusValues::LED_A << USBControlAndStatusValues::DisableShift));

    // Clear OUT1
    sis_log("Deactivating OUT1");
    err_wrap(ctrl->udp_sis3153_register_write(
            LemoIOControl,
            m_lemoIORegDAQBaseValue << LemoIOControlValues::DisableShift));


    // Turn off all DAQ mode features that where enabled in start()
    err_wrap(ctrl->udp_sis3153_register_write(
            SIS3153ETH_STACK_LIST_CONTROL,
            m_stackListControlRegisterValue << SIS3153Registers::StackListControlValues::DisableShift),
        QSL("Error leaving SIS3153 DAQ mode: %1"));

    u32 leaveDAQPacketCount = 0;
#if 0 // See the FIXME near the top for why this is disabled
    if (m_stackListControlRegisterValue & StackListControlValues::ListBufferEnable)
    {
        u32 wordsLeftInBuffer = 0;
        err_wrap(ctrl->udp_sis3153_register_read(
                SIS3153ETH_STACK_LIST_CONTROL, &wordsLeftInBuffer));
        wordsLeftInBuffer = ((wordsLeftInBuffer >> 16) & 0xffff);

        qDebug() << __PRETTY_FUNCTION__ << "initial wordsLeftInBuffer=" << wordsLeftInBuffer;

        if (wordsLeftInBuffer)
        {
            qDebug() << "sis3153: setting FlushBufferEnable bit";
            err_wrap(ctrl->udp_sis3153_register_write(
                    SIS3153ETH_STACK_LIST_CONTROL,
                    StackListControlValues::FlushBufferEnable));

            qDebug() << "sis3153: clearing FlushBufferEnable bit";
            err_wrap(ctrl->udp_sis3153_register_write(
                    SIS3153ETH_STACK_LIST_CONTROL,
                    StackListControlValues::FlushBufferEnable << StackListControlValues::DisableShift));
        }

        // FIXME: This is a hack to work around buffer ordering issues I
        // encountered when trying to cleanly leave SIS3153 DAQ mode.
        m_lossCounter.m_leavingDAQ = true;

        while (wordsLeftInBuffer)
        {
            qDebug() << ">>>> begin reading final buffers";
            while(readAndProcessBuffer().bytesRead > 0) leaveDAQPacketCount++;
            qDebug() << "<<<< end reading final buffers";

            // update the wordcount
            err_wrap(ctrl->udp_sis3153_register_read(
                    SIS3153ETH_STACK_LIST_CONTROL, &wordsLeftInBuffer));
            wordsLeftInBuffer = ((wordsLeftInBuffer >> 16) & 0xffff);
            qDebug() << __PRETTY_FUNCTION__ << "loop wordsLeftInBuffer=" << wordsLeftInBuffer;
        }

        m_lossCounter.m_leavingDAQ = false;
    }
    else
    {
        qDebug() << ">>>> begin reading final buffers (buffering not enabled)";
        while(readAndProcessBuffer().bytesRead > 0) leaveDAQPacketCount++;
        qDebug() << "<<<< end reading final buffers (buffering not enabled)";
    }
#else

        m_lossCounter.beginLeavingDAQ();
        qDebug() << ">>>> begin reading final buffers";
        while(readAndProcessBuffer().bytesRead > 0) leaveDAQPacketCount++;
        qDebug() << "<<<< end reading final buffers";
        m_lossCounter.endLeavingDAQ();
#endif

    sis_log(QString(QSL("SIS3153 readout left DAQ mode (%1 remaining packets received, last sequenceNumber=%2)"))
            .arg(leaveDAQPacketCount)
            .arg(m_lossCounter.lastReceivedSequenceNumber)
           );
}

SIS3153ReadoutWorker::ReadBufferResult SIS3153ReadoutWorker::readAndProcessBuffer()
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
#ifdef Q_OS_WIN
        /*
        DWORD WINAPI FormatMessage(
            _In_     DWORD   dwFlags,
            _In_opt_ LPCVOID lpSource,
            _In_     DWORD   dwMessageId,
            _In_     DWORD   dwLanguageId,
            _Out_    LPTSTR  lpBuffer,
            _In_     DWORD   nSize,
            _In_opt_ va_list *Arguments
            );
        */
        char strBuffer[1024];
        strBuffer[0] = '\0';

        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL,
                      wsaError,
                      0,
                      strBuffer,
                      sizeof(strBuffer),
                      NULL);

        result.error = VMEError(VMEError::ReadError, wsaError, strBuffer);
#else
        result.error = VMEError(VMEError::ReadError, errno, std::strerror(errno));
#endif
        // EAGAIN is not an error as it's used for the timeout case
        if (errno != EAGAIN)
        {
            auto msg = QString(QSL("SIS3153 Warning: data packet read failed: %1").arg(result.error.toString()));
            logMessage(msg, true);
#if SIS_READOUT_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "error from recvfrom(): " << result.error.toString();
#endif
        }
        return result;
    }

    m_workerContext.daqStats.totalBytesRead += result.bytesRead;
    m_workerContext.daqStats.totalBuffersRead++;


    // UDP Datagram Forwarding
    if (m_forward.socket)
    {
        auto sendResult = m_forward.socket->writeDatagram(
            reinterpret_cast<const char *>(m_readBuffer.data + 1),
            result.bytesRead,
            m_forward.host,
            m_forward.port);

        if (sendResult != result.bytesRead)
        {
            auto msg = QString((QSL("SIS3153 Warning: forwarding packet failed: %1 (returnCode=%2)")
                                .arg(m_forward.socket->errorString())
                                .arg(m_forward.socket->error())
                                ));
            logMessage(msg, true);
        }
    }


    if (result.bytesRead < 3)
    {
        result.error = VMEError(VMEError::CommError, -1,
                                QSL("sis3153 read < packetHeaderSize (3 bytes)!"));
        auto msg = QString(QSL("SIS3153 Warning: %1").arg(result.error.toString()));
        logMessage(msg, true);

#if 0
        qDebug() << __PRETTY_FUNCTION__ << "got < 3 bytes; returning " << result.error.toString()
            << result.bytesRead << std::strerror(errno);
#endif
        m_workerContext.daqStats.buffersWithErrors++;
        return result;
    }

    m_readBuffer.used = result.bytesRead + 1; // account for the padding byte

    u8 packetAck, packetIdent, packetStatus;
    packetAck    = m_readBuffer.data[1];
    packetIdent  = m_readBuffer.data[2];
    packetStatus = m_readBuffer.data[3];

    const auto bufferNumber = m_workerContext.daqStats.totalBuffersRead;

    (void) bufferNumber;

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

    processBuffer(packetAck, packetIdent, packetStatus, dataPtr, dataSize);

    return result;
}

void SIS3153ReadoutWorker::processBuffer(
    u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size)
{
    const auto bufferNumber = m_workerContext.daqStats.totalBuffersRead;

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
        sis_log(QString(">>> Begin sis3153 buffer #%1").arg(bufferNumber));

        logBuffer(BufferIterator(data, size), [this](const QString &str) {
            sis_log(str);
            qDebug().noquote() << str;
        });

        sis_log(QString("<<< End sis3153 buffer #%1") .arg(bufferNumber));
    }

    u32 action = 0;

    try
    {
        // Dispatch depending on the Ack byte and the current processing state.

        // multiple events per packet
        if (packetAck == SIS3153Constants::MultiEventPacketAck)
        {
            sis_trace(QString("buffer #%1 -> multi event buffer").arg(bufferNumber));
            m_counters.multiEventPackets++;
            action = processMultiEventData(packetAck, packetIdent, packetStatus, data, size);
        }
        else
        {
            /* If the isLastPacket bit is set the packet is either a single
             * event packet or it is the end of a sequence of partial fragments.
             * If the bit is not set the packet is a partial fragment and may
             * either be the beginning of a new partial event or the
             * continuation of a partial event. */
            bool isLastPacket = packetAck & SIS3153Constants::AckIsLastPacketMask;

            // start or continue partial event processing
            if (m_processingState.stackList >= 0 || !isLastPacket)
            {
                sis_trace(QString("buffer #%1 -> partial event buffer (isLastPacket=%2)")
                          .arg(bufferNumber)
                          .arg(isLastPacket));

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
                m_workerContext.daqStats.buffersWithErrors++;
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
        m_workerContext.daqStats.buffersWithErrors++;
        auto msg = QString(QSL("SIS3153 Warning: (buffer #%1) end of input reached unexpectedly! Skipping buffer."))
            .arg(m_workerContext.daqStats.totalBuffersRead);
        logMessage(msg, true);

        maybePutBackBuffer();
    }

    sis_trace(QString("sis3153 buffer #%1 done")
              .arg(bufferNumber));
}

u32 SIS3153ReadoutWorker::processMultiEventData(
    u8 packetAck, u8 /*packetIdent*/, u8 /*packetStatus*/, u8 *data, size_t size)
{
#if SIS_READOUT_DEBUG
    qDebug() << __PRETTY_FUNCTION__;
#endif
    Q_ASSERT(packetAck == SIS3153Constants::MultiEventPacketAck);

    const auto bufferNumber = m_workerContext.daqStats.totalBuffersRead;

    if (m_processingState.stackList >= 0)
    {
        auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) got multievent packet while partial event processing is in progress!"
                                " Skipping buffer."))
                    .arg(bufferNumber));
        logMessage(msg, true);
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
    seems to be reversed: header=0x00250058
    -> [status] len[0] len[1] ack
    -> 0x00     0x25   0x00   0x58
      */
        u32 eventHeader    = iter.extractU32();
        u8  internalStatus = (eventHeader & 0xff000000) >> 24;
        u16 length         = ((eventHeader & 0x00ff0000) >> 16) | (eventHeader & 0x0000ff00); // length in 32-bit words
        u8  internalAck    = eventHeader & 0xff;    // same as packetAck in non-buffered mode

        sis_trace(QString("buffer #%1: embedded ack=0x%2, status=0x%3, length=%4 (%5 bytes), header=0x%6")
                  .arg(bufferNumber)
                  .arg((u32)internalAck, 2, 16, QLatin1Char('0'))
                  .arg((u32)internalStatus, 2, 16, QLatin1Char('0'))
                  .arg(length)
                  .arg(length * sizeof(u32))
                  .arg(eventHeader, 8, 16, QLatin1Char('0')));

        int stacklist = internalAck & SIS3153Constants::AckStackListMask;
        m_counters.embeddedEvents[stacklist]++;

        // Forward the embedded event to processSingleEventData
        action = processSingleEventData(internalAck, 0, internalStatus, iter.buffp, length * sizeof(u32));

        if (action & ProcessorAction::SkipInput)
        {
            /* Processing the internal event did not succeed. Skip the rest of
             * the data and throw away the partially generated output buffer. */
#if SIS_READOUT_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "processSingleEventData returned SkipInput!";
#endif

            maybePutBackBuffer();

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
        logMessage(msg, true);
    }

#if SIS_READOUT_DEBUG
    sis_trace(QString("end processing. returning 0x%1 (%2)")
              .arg(action, 0, 16)
              .arg(ProcessorActionStrings.value(action)));
#endif

    return action;
}

/* Handles the case where no partial event assembly is in progress and
 * the buffer contains a single complete event.
 * Also called by processMultiEventData() for each embedded event! */
u32 SIS3153ReadoutWorker::processSingleEventData(
    u8 packetAck, u8 /*packetIdent*/, u8 /*packetStatus*/, u8 *data, size_t size)
{
#if SIS_READOUT_DEBUG
    qDebug() << __PRETTY_FUNCTION__;
#endif
    Q_ASSERT(packetAck != SIS3153Constants::MultiEventPacketAck);
    Q_ASSERT(packetAck & SIS3153Constants::AckIsLastPacketMask);
    Q_ASSERT(m_processingState.stackList < 0);

    int stacklist = packetAck & SIS3153Constants::AckStackListMask;
    m_counters.stackListCounts[stacklist]++;
    const auto bufferNumber = m_workerContext.daqStats.totalBuffersRead;

    BufferIterator iter(data, size);

    // check beginHeader (0xbb...)
    {
        u32 beginHeader = iter.extractU32();

        if ((beginHeader & SIS3153Constants::BeginEventMask) != SIS3153Constants::BeginEventResult)
        {
            auto msg = QString(QSL("SIS3153 Warning: (buffer #%1) Invalid beginHeader 0x%2 (singleEvent). Skipping buffer."))
                .arg(bufferNumber)
                .arg(beginHeader, 8, 16, QLatin1Char('0'));
            logMessage(msg, true);

            return ProcessorAction::SkipInput;
        }

        // The packetNumber will fit into an s32 as the top 8 bits are always
        // zero.
        s32 seqNum = (beginHeader & SIS3153Constants::BeginEventSequenceNumberMask);

        if (m_lossCounter.handleEventSequenceNumber(seqNum, bufferNumber)
            & EventLossCounter::Flag_IsStaleData)
        {
            m_counters.staleEvents++;

            auto msg = QString(QSL(
                    "(buffer #%1) received stale data (single event)."
                    " seqNum=%2. Skipping event."))
                .arg(bufferNumber)
                .arg(seqNum);
            //logMessage(msg, true);
            qDebug() << __PRETTY_FUNCTION__ << msg;

            // FIXME: why is NoneSet returned? shouldn't it be SkipInput?
            return ProcessorAction::NoneSet;
        }
    }

    // Watchdog packet handling
    if (stacklist == m_watchdogStackListIndex)
    {
        // Check to make sure there's no event config for this stacklist. If
        // there was a config the stacklist can not be used for the watchdog.
        Q_ASSERT(m_eventConfigsByStackList[packetAck & SIS3153Constants::AckStackListMask] == nullptr);

        u32 endHeader = iter.extractU32();
        if ((endHeader & SIS3153Constants::EndEventMask) != SIS3153Constants::EndEventResult)
        {
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) Invalid watchdog endHeader: 0x%2"))
                        .arg(bufferNumber)
                        .arg(endHeader, 8, 16, QLatin1Char('0')));
            logMessage(msg, true);
            return ProcessorAction::SkipInput;
        }

        update_endHeader_counters(m_counters, endHeader, stacklist);

        if (iter.bytesLeft() > 0)
        {
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) %2 bytes left at end of watchdog packet"))
                        .arg(bufferNumber)
                        .arg(iter.bytesLeft()));
            logMessage(msg, true);
            return ProcessorAction::SkipInput;
        }

        u32 ret = ProcessorAction::KeepState;

#if SIS_READOUT_DEBUG
        sis_trace(QString("got watchdog event, endHeader=0x%1, returning %2")
                  .arg(endHeader, 8, 16, QLatin1Char('0'))
                  .arg(ProcessorActionStrings.value(ret)));
#endif

        return ret;
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
        logMessage(msg, true);
        return ProcessorAction::SkipInput;
    }

    DataBuffer *outputBuffer = getOutputBuffer();
    outputBuffer->ensureFreeSpace(size * 2);
    MVMEStreamWriterHelper streamWriter(outputBuffer);
    int writerFlags = 0;

    streamWriter.openEventSection(eventIndex);

    auto moduleConfigs = eventConfig->getModuleConfigs();
    const s32 moduleCount = moduleConfigs.size();

    for (s32 moduleIndex = 0; moduleIndex < moduleCount; moduleIndex++)
    {
#if SIS_READOUT_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "moduleIndex =" << moduleIndex << ", moduleCount =" << moduleCount;
#endif
        // TODO: store module metas for each (event, module) index to avoid having to deref the module pointer here
        auto moduleConfig = moduleConfigs.at(moduleIndex);
        writerFlags |= streamWriter.openModuleSection((u32)moduleConfig->getModuleMeta().typeId);

        // copy module data to output
        while (true)
        {
            u32 data = iter.extractU32();

            // NOTE: could handle the 0x02110211 status word here just like the EndMarker below

            if (streamWriter.hasOpenModuleSection())
            {
                writerFlags |= streamWriter.writeModuleData(data);
            }

            if (data == EndMarker)
            {
                break;
            }
        }

        if (streamWriter.hasOpenModuleSection())
        {
            /*u32 moduleSectionBytes =*/ streamWriter.closeModuleSection().sectionBytes;
        }
    }

    writerFlags |= streamWriter.writeEventData(EndMarker);

    // Close the event section. This should not throw/cause an error.
    streamWriter.closeEventSection();

    // check endHeader (0xee...)
    u32 endHeader = iter.extractU32();

    if ((endHeader & SIS3153Constants::EndEventMask) != SIS3153Constants::EndEventResult)
    {
        auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) Invalid endHeader: 0x%2"))
                    .arg(bufferNumber)
                    .arg(endHeader, 8, 16, QLatin1Char('0')));
        logMessage(msg, true);
        maybePutBackBuffer();
        return ProcessorAction::SkipInput;
    }

    warnIfStreamWriterError(bufferNumber, writerFlags, eventIndex);

    update_endHeader_counters(m_counters, endHeader, stacklist);

    sis_trace(QString("sis3153 buffer #%1, endHeader=0x%2")
              .arg(bufferNumber)
              .arg(endHeader, 8, 16, QLatin1Char('0')));

    return ProcessorAction::FlushBuffer;
}

u32 SIS3153ReadoutWorker::processPartialEventData(
    u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size)
{
    const auto bufferNumber = m_workerContext.daqStats.totalBuffersRead;
    (void) packetIdent;

    sis_trace(QString("sis3153 buffer #%1, packetAck=0x%2, packetIdent=0x%3, packetStatus=0x%4, data@0x%6, size=%7")
              .arg(bufferNumber)
              .arg((u32)packetAck, 2, 16, QLatin1Char('0'))
              .arg((u32)packetIdent, 2, 16, QLatin1Char('0'))
              .arg((u32)packetStatus, 2, 16, QLatin1Char('0'))
              .arg((uintptr_t)data, 8, 16, QLatin1Char('0'))
              .arg(size));

    Q_ASSERT(packetAck != SIS3153Constants::MultiEventPacketAck);

    int stacklist = packetAck & SIS3153Constants::AckStackListMask;
    bool isLastPacket = packetAck & SIS3153Constants::AckIsLastPacketMask;
    bool partialInProgress = m_processingState.stackList >= 0;

    Q_ASSERT(partialInProgress || !isLastPacket);
    Q_ASSERT(stacklist != m_watchdogStackListIndex);

    if (partialInProgress)
    {
        /* We should still have an output buffer around from processing the
         * previous partial input buffers. */
        Q_ASSERT(m_outputBuffer);
    }

    m_counters.partialFragments[stacklist]++;

    BufferIterator iter(data, size);

    DataBuffer *outputBuffer = getOutputBuffer();
    outputBuffer->ensureFreeSpace(size * 2);
    m_processingState.streamWriter.setOutputBuffer(outputBuffer);

    if (!partialInProgress)
    {
        // check beginHeader (0xbb...)
        {
            u32 beginHeader = iter.extractU32();

            if ((beginHeader & SIS3153Constants::BeginEventMask) != SIS3153Constants::BeginEventResult)
            {
                auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) Invalid beginHeader 0x%2 (partialEvent). Skipping buffer."))
                            .arg(bufferNumber)
                            .arg(beginHeader, 8, 16, QLatin1Char('0')));
                logMessage(msg, true);
                maybePutBackBuffer();
                return ProcessorAction::SkipInput;
            }

            // The sequenceNumber will fit into an s32 as the top 8 bits are always
            // zero.
            s32 seqNum = (beginHeader & SIS3153Constants::BeginEventSequenceNumberMask);

            if (m_lossCounter.handleEventSequenceNumber(seqNum, bufferNumber) & EventLossCounter::Flag_IsStaleData)
            {
                m_counters.staleEvents++;

                auto msg = QString(QSL(
                        "(buffer #%1) received stale data (partialEvent)."
                        " seqNum=%2. Skipping buffer."))
                    .arg(bufferNumber)
                    .arg(seqNum);
                //logMessage(msg, true);
                qDebug() << __PRETTY_FUNCTION__ << msg;

                return ProcessorAction::SkipInput;
            }
        }

        // initial value is 0x00 or 0x80. the low nibble will then be
        // incremented by 1 for each partial packet (Manual 5.1.4).
        m_processingState.expectedPacketStatus = (packetStatus & 0x80);

        EventConfig *eventConfig = m_eventConfigsByStackList[stacklist];
        int eventIndex = m_eventIndexByStackList[stacklist];

        if (!eventConfig)
        {
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) No eventConfig for stackList=%2 -> eventIndex=%3 (partialEvent)."
                                    " Skipping buffer."))
                        .arg(bufferNumber)
                        .arg(stacklist)
                        .arg(eventIndex));
            logMessage(msg, true);
            maybePutBackBuffer();
            return ProcessorAction::SkipInput;
        }

        m_processingState.stackList = stacklist;
        m_processingState.streamWriter.openEventSection(eventIndex);
        m_processingState.moduleIndex = 0;
    }

    Q_ASSERT(m_processingState.streamWriter.eventHeaderOffset() >= 0);
    Q_ASSERT(m_processingState.moduleIndex >= 0);

    if (stacklist != m_processingState.stackList)
    {
        QString msg = (QString(QSL("SIS3153 Warning: (buffer #%1) stackList mismatch during partialEvent processing"
                                   " (stackList=%2, expected=%3). Skipping buffer."))
                       .arg(bufferNumber)
                       .arg(stacklist)
                       .arg(m_processingState.stackList));
        logMessage(msg, true);
        maybePutBackBuffer();
        return ProcessorAction::SkipInput;
    }

    if ((packetStatus != m_processingState.expectedPacketStatus))
    {
        auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) (partialEvent) Unexpected packetStatus: "
                                "0x%2, expected 0x%3. Skipping buffer."))
                    .arg(bufferNumber)
                    .arg((u32)packetStatus, 2, 16, QLatin1Char('0'))
                    .arg((u32)m_processingState.expectedPacketStatus)
                   );
        logMessage(msg, true);
        maybePutBackBuffer();
        return ProcessorAction::SkipInput;
    }
    else
    {
        // FIXME: this sometimes doesn't wrap properly.
        u8 seqNum = (m_processingState.expectedPacketStatus & 0xf) + 1; // increment
        if (seqNum > 0xf) seqNum = 0; // and wrap
        m_processingState.expectedPacketStatus &= 0xf0; // keep upper bits
        m_processingState.expectedPacketStatus |= (seqNum & 0xf); // replace lower bits
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
        logMessage(msg, true);

        maybePutBackBuffer();
        return ProcessorAction::SkipInput;
    }

    auto moduleConfigs = eventConfig->getModuleConfigs();
    const s32 moduleCount = moduleConfigs.size();
    int writerFlags = 0;

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
            logMessage(msg, true);

            maybePutBackBuffer();
            return ProcessorAction::SkipInput;
        }

        if (!m_processingState.streamWriter.hasOpenModuleSection())
        {
            auto moduleConfig = moduleConfigs[m_processingState.moduleIndex];

            if (!moduleConfig)
            {
                auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) no moduleConfig for eventIndex=%2, moduleIndex=%3."
                                        " Skipping buffer."))
                            .arg(bufferNumber)
                            .arg(eventIndex)
                            .arg(m_processingState.moduleIndex));
                logMessage(msg, true);

                maybePutBackBuffer();
                return ProcessorAction::SkipInput;
            }

            writerFlags |= m_processingState.streamWriter.openModuleSection((u32)moduleConfig->getModuleMeta().typeId);
        }

        // copy module data to output
        const unsigned minwords = isLastPacket ? 1 : 0;

        while (iter.longwordsLeft() > minwords)
        {
            u32 data = iter.extractU32();

            writerFlags |= m_processingState.streamWriter.writeModuleData(data);

            if (data == EndMarker)
            {
                if (m_processingState.streamWriter.hasOpenModuleSection())
                {
                    /*u32 moduleSectionBytes =*/ m_processingState.streamWriter.closeModuleSection().sectionBytes;
                    m_processingState.moduleIndex++;
                }
                break;
            }
        }
    }

    warnIfStreamWriterError(bufferNumber, writerFlags, eventIndex);

    if (isLastPacket)
    {
        Q_ASSERT(iter.longwordsLeft() == 1);
        u32 endHeader = iter.extractU32();
        if ((endHeader & SIS3153Constants::EndEventMask) != SIS3153Constants::EndEventResult)
        {
            auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) Invalid endHeader 0x%2 (partialEvent). Skipping buffer."))
                        .arg(bufferNumber)
                        .arg(endHeader, 8, 16, QLatin1Char('0')));
            logMessage(msg, true);
            maybePutBackBuffer();
            return ProcessorAction::SkipInput;
        }

        update_endHeader_counters(m_counters, endHeader, stacklist);

        // Write the final EndMarker into the current event section
        writerFlags |= m_processingState.streamWriter.writeEventData(EndMarker);

        // Close the event section. This should not throw.
        m_processingState.streamWriter.closeEventSection();

        m_counters.reassembledPartials[stacklist]++;
        m_counters.stackListCounts[stacklist]++;

        warnIfStreamWriterError(bufferNumber, writerFlags, eventIndex);

        return ProcessorAction::FlushBuffer;
    }

    sis_trace(QString("sis3153 buffer #%1, returning KeepState")
              .arg(bufferNumber)
             );

    return ProcessorAction::KeepState;
}

void SIS3153ReadoutWorker::timetick()
{
    Q_ASSERT(m_listfileHelper);
    m_listfileHelper->writeTimetickSection();
}

void SIS3153ReadoutWorker::flushCurrentOutputBuffer()
{
    auto outputBuffer = m_outputBuffer;

    if (outputBuffer)
    {
        m_listfileHelper->writeBuffer(outputBuffer);

        if (outputBuffer != &m_localEventBuffer)
        {
            m_workerContext.fullBuffers->enqueue(outputBuffer);
        }
        else
        {
            m_workerContext.daqStats.droppedBuffers++;
        }
        sis_trace("resetting current output buffer");
        m_outputBuffer = nullptr;
        m_processingState.streamWriter.setOutputBuffer(nullptr);
    }
    else
    {
        sis_trace("no output buffer to flush!");
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

void SIS3153ReadoutWorker::resume(quint32 cycles)
{
    qDebug() << __PRETTY_FUNCTION__ << cycles;

    if (m_state == DAQState::Paused)
    {
        m_cyclesToRun = cycles;
        m_logBuffers = (cycles > 0); // log buffers to GUI if number of cycles has been passed in
        m_desiredState = DAQState::Running;
    }
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

void SIS3153ReadoutWorker::logError(const QString &message)
{
    sis_log(QString("SIS3153 Error: %1").arg(message));
}

DataBuffer *SIS3153ReadoutWorker::getOutputBuffer()
{
    DataBuffer *outputBuffer = m_outputBuffer;

    if (!outputBuffer)
    {
        outputBuffer = m_workerContext.freeBuffers->dequeue();

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

void SIS3153ReadoutWorker::maybePutBackBuffer()
{
    if (m_outputBuffer && m_outputBuffer != &m_localEventBuffer)
    {
        // We still hold onto one of the buffers obtained from the free queue.
        // This can happen for the SkipInput case. Put the buffer back into the
        // free queue.
        m_workerContext.freeBuffers->enqueue(m_outputBuffer);
        sis_trace("resetting current output buffer");
    }

    m_outputBuffer = nullptr;
}

void SIS3153ReadoutWorker::warnIfStreamWriterError(u64 bufferNumber, int writerFlags, u16 eventIndex)
{
    if (writerFlags & MVMEStreamWriterHelper::ModuleSizeExceeded)
    {
        auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) maximum module data size exceeded. "
                                "Data will be truncated! (eventIndex=%2)"))
                    .arg(bufferNumber)
                    .arg(eventIndex));
        logMessage(msg, true);
    }

    if (writerFlags & MVMEStreamWriterHelper::EventSizeExceeded)
    {
        auto msg = (QString(QSL("SIS3153 Warning: (buffer #%1) maximum event section size exceeded. "
                                "Data will be truncated! (eventIndex=%2)"))
                    .arg(bufferNumber)
                    .arg(eventIndex));
        logMessage(msg, true);
    }
}

void SIS3153ReadoutWorker::setupUDPForwarding()
{
    m_forward.socket.reset();

    QVariantMap controllerSettings = m_workerContext.vmeConfig->getControllerSettings();

    if (!controllerSettings.value("UDP_Forwarding_Enable").toBool())
        return;

    QString address = controllerSettings.value("UDP_Forwarding_Address").toString();
    u16 port = controllerSettings.value("UDP_Forwarding_Port").toUInt();

    if (address.isEmpty())
    {
        sis_log(QSL("Empty forwarding hostname given. Disabling UDP forwarding."));
        return;
    }

    sis_log(QString("Setting up forwarding of raw readout data to %1:%2")
            .arg(address)
            .arg(port));

    if (!m_forward.host.setAddress(address))
    {
        // A string that could not be parsed as an IP address was given. Use
        // DNS to try to resolve an address.
        auto hostInfo = QHostInfo::fromName(address);

        if (hostInfo.error() != QHostInfo::NoError)
        {
            sis_log(QSL("Error resolving forward host %1: %2. Disabling UDP forwarding.")
                    .arg(address)
                    .arg(hostInfo.errorString()));
            return;
        }

        if (hostInfo.addresses().isEmpty())
        {
            sis_log(QSL("Could not resolve any host addresses for forward host %1. Disabling UDP forwarding.")
                    .arg(address));
            return;
        }

        m_forward.host = hostInfo.addresses().value(0);

        sis_log(QString("Resolved UDP forwarding address to %1")
                .arg(m_forward.host.toString()));
    }

    m_forward.socket = std::make_unique<QUdpSocket>();
    m_forward.port = port;
}
