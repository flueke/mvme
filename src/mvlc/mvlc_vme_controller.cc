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
#include "mvlc/mvlc_vme_controller.h"

#include <QDebug>

#include "mvlc/mvlc_util.h"
#include "util/counters.h"


namespace
{

using namespace mesytec;
using namespace mesytec::mvlc;

// Checks for certain MVLCErrorCode values and returns a VMEError containing
// additional information if applicable. Otherwise a VMEError object
// constructed from the given error_code is returned.
VMEError error_wrap(const mvme_mvlc::MVLCObject &/*mvlc*/, const std::error_code &ec)
{
#if 0
    if (ec == MVLCErrorCode::InvalidBufferHeader ||
        ec == MVLCErrorCode::UnexpectedBufferHeader)
    {
        auto buffer = mvlc.getResponseBuffer();

        if (!buffer.empty())
        {
            QStringList strings;

            for (u32 word: buffer)
                strings.append(QString("0x%1").arg(word, 8, 16, QLatin1Char('0')));

            QString msg(ec.message().c_str());
            msg += ": " + strings.join(", ");

            return VMEError(VMEError::CommError, msg);
        }
    }

    return VMEError(ec);
#else
    return VMEError(ec);
#endif
}

} // end anon namespace

namespace mesytec
{
namespace mvme_mvlc
{

MVLC_VMEController::MVLC_VMEController(MVLCObject *mvlc, QObject *parent)
    : VMEController(parent)
    , m_mvlc(mvlc)
    //, m_notificationPoller(*mvlc)
{
    assert(m_mvlc);

    connect(m_mvlc, &MVLCObject::stateChanged,
            this, &MVLC_VMEController::onMVLCStateChanged);

#if 0
    auto debug_print_stack_error_counters = [this] ()
    {
        auto errorCounters = m_mvlc->getStackErrorCounters();

        for (size_t stackId = 0; stackId < errorCounters.stackErrors.size(); ++stackId)
        {
            const auto &errorInfoCounts = errorCounters.stackErrors[stackId];

            if (!errorInfoCounts.empty() || errorCounters.nonErrorFrames > 0)
            {
                qDebug("Stack Error Info Dump:");
                break;
            }
        }

        for (size_t stackId = 0; stackId < errorCounters.stackErrors.size(); ++stackId)
        {
            const auto &errorInfoCounts = errorCounters.stackErrors[stackId];

            if (errorInfoCounts.empty())
                continue;

            for (auto it = errorInfoCounts.begin();
                 it != errorInfoCounts.end();
                 it++)
            {
                const auto &errorInfo = it->first;
                const auto &count = it->second;

                qDebug("  stackId=%lu, line=%u, flags=0x%02x, count=%lu",
                       stackId,
                       errorInfo.line,
                       errorInfo.flags,
                       count
                       );
            }
        }

        if (errorCounters.nonErrorFrames)
            qDebug("nonErrorFrames=%lu", errorCounters.nonErrorFrames);

        for (auto it=errorCounters.nonErrorHeaderCounts.begin();
             it!=errorCounters.nonErrorHeaderCounts.end();
             ++it)
        {
            u32 header = it->first;
            size_t count = it->second;

            qDebug("  0x%08x: %lu", header, count);
        }

        int frameIndex = 0;
        for (const auto &frameCopy: errorCounters.framesCopies)
        {
            qDebug("copy of erroneous error frame %d recevied via polling:", frameIndex++);
            for (u32 word: frameCopy)
            {
                qDebug("  0x%08x", word);
            }
            qDebug("----");
        }
    };

    auto debug_print_eth_stats = [this] ()
    {
        if (this->connectionType() != ConnectionType::ETH)
            return;

        auto implEth = reinterpret_cast<eth::Impl *>(this->getImpl());

        auto pipeStats = implEth->getPipeStats();
        auto packetChannelStats = implEth->getPacketChannelStats();
        double dt_s = 0.0;

        auto now = QDateTime::currentDateTime();

        if (lastUpdateTime.isValid())
            dt_s = lastUpdateTime.msecsTo(now) / 1000.0;

        lastUpdateTime = now;

        qDebug("ETH per pipe counters:");

        for (size_t pipeIndex = 0; pipeIndex < pipeStats.size(); pipeIndex++)
        {
            const auto &stats = pipeStats[pipeIndex];
            const auto &prevStats = prevPipeStats[pipeIndex];

            u64 packetRate = calc_rate0(
                stats.receivedPackets, prevStats.receivedPackets, dt_s);

            u64 lossRate = calc_rate0(
                stats.lostPackets, prevStats.lostPackets, dt_s);

            u64 sumRate = calc_rate0(
                stats.receivedPackets + stats.lostPackets,
                prevStats.receivedPackets + prevStats.lostPackets,
                dt_s);

            qDebug("  pipe=%lu, receiveAttempts=%lu, receivedPackets=%lu, receivedBytes=%lu\n"
                   "    shortPackets=%lu, packetsWithResidue=%lu, noHeader=%lu\n"
                   "    headerOutOfRange=%lu, packetChannelOutOfRange=%lu, lostPackets=%lu\n"
                   "    packetRate=%lu pkts/s, lossRate=%lu pkts/s, sumRate=%lu pkts/s",
                   pipeIndex,
                   stats.receiveAttempts,
                   stats.receivedPackets,
                   stats.receivedBytes,
                   stats.shortPackets,
                   stats.packetsWithResidue,
                   stats.noHeader,
                   stats.headerOutOfRange,
                   stats.packetChannelOutOfRange,
                   stats.lostPackets,
                   packetRate,
                   lossRate,
                   sumRate
                   );
        }

        prevPipeStats = pipeStats;

        qDebug(" ");
    };

    auto dumpTimer = new QTimer(this);
    connect(dumpTimer, &QTimer::timeout, this, debug_print_stack_error_counters);
    //connect(dumpTimer, &QTimer::timeout, this, debug_print_eth_stats);
    (void)debug_print_eth_stats;
    dumpTimer->setInterval(1000);
    dumpTimer->start();
#endif
}

void MVLC_VMEController::onMVLCStateChanged(const MVLCObject::State &,
                                            const MVLCObject::State &newState)
{
    switch (newState)
    {
        case MVLCObject::Disconnected:
            //m_notificationPoller.disablePolling();
            emit controllerClosed();
            emit controllerStateChanged(ControllerState::Disconnected);
            break;

        case MVLCObject::Connected:
            //m_notificationPoller.enablePolling();
            emit controllerOpened();
            emit controllerStateChanged(ControllerState::Connected);
            break;

        case MVLCObject::Connecting:
            emit controllerStateChanged(ControllerState::Connecting);
            break;
    }
}

bool MVLC_VMEController::isOpen() const
{
    return m_mvlc->isConnected();
}

VMEError MVLC_VMEController::open()
{
    return VMEError(m_mvlc->connect());
}

VMEError MVLC_VMEController::close()
{
    return VMEError(m_mvlc->disconnect());
}

ControllerState MVLC_VMEController::getState() const
{
    switch (m_mvlc->getState())
    {
        case MVLCObject::Disconnected:
            return ControllerState::Disconnected;

        case MVLCObject::Connecting:
            return ControllerState::Connecting;

        case MVLCObject::Connected:
            return ControllerState::Connected;
    }

    return ControllerState::Disconnected;
}

QString MVLC_VMEController::getIdentifyingString() const
{
    switch (getType())
    {
        case VMEControllerType::MVLC_USB:
            return "MVLC_USB";

        case VMEControllerType::MVLC_ETH:
            return "MVLC_ETH";

        default:
            break;
    }

    assert(!"invalid vme controller type");
    return "<invalid_vme_controller_type>";
}

VMEControllerType MVLC_VMEController::getType() const
{
    switch (m_mvlc->connectionType())
    {
        case mvlc::ConnectionType::USB:
            return VMEControllerType::MVLC_USB;

        case mvlc::ConnectionType::ETH:
            return VMEControllerType::MVLC_ETH;
    }

    InvalidCodePath;
    return VMEControllerType::MVLC_USB;
}

VMEError MVLC_VMEController::write32(u32 address, u32 value, u8 amod)
{
    auto ec = m_mvlc->vmeWrite(address, value, amod, mvlc::VMEDataWidth::D32);
    return error_wrap(*m_mvlc, ec);
}

VMEError MVLC_VMEController::write16(u32 address, u16 value, u8 amod)
{
    auto ec = m_mvlc->vmeWrite(address, value, amod, mvlc::VMEDataWidth::D16);
    return error_wrap(*m_mvlc, ec);
}


VMEError MVLC_VMEController::read32(u32 address, u32 *value, u8 amod)
{
    auto ec = m_mvlc->vmeRead(address, *value, amod, mvlc::VMEDataWidth::D32);
    return error_wrap(*m_mvlc, ec);
}

VMEError MVLC_VMEController::read16(u32 address, u16 *value, u8 amod)
{
    u32 tmpVal = 0u;
    auto ec = m_mvlc->vmeRead(address, tmpVal, amod, mvlc::VMEDataWidth::D16);
    *value = tmpVal;
    return error_wrap(*m_mvlc, ec);
}


VMEError MVLC_VMEController::blockRead(u32 address, u32 transfers,
                                       QVector<u32> *dest, u8 amod, bool /*fifo*/)
{
    std::vector<u32> buffer;
    auto ec = m_mvlc->vmeBlockRead(address, amod, transfers, buffer);

    dest->clear();
    dest->reserve(buffer.size());
    std::copy(std::begin(buffer), std::end(buffer), std::back_inserter(*dest));

    return error_wrap(*m_mvlc, ec);
}

VMEError MVLC_VMEController::blockRead(u32 address, const mesytec::mvlc::Blk2eSSTRate &rate,
                                       u16 transfers, QVector<u32> *dest)
{
    std::vector<u32> buffer;
    auto ec = m_mvlc->vmeBlockRead(address, rate, transfers, buffer);

    dest->clear();
    dest->reserve(buffer.size());
    std::copy(std::begin(buffer), std::end(buffer), std::back_inserter(*dest));

    return error_wrap(*m_mvlc, ec);
}

VMEError MVLC_VMEController::blockReadSwapped(u32 address, u16 transfers, QVector<u32> *dest)
{
    std::vector<u32> buffer;
    auto ec = m_mvlc->vmeBlockReadSwapped(address, transfers, buffer);

    dest->clear();
    dest->reserve(buffer.size());
    std::copy(std::begin(buffer), std::end(buffer), std::back_inserter(*dest));

    return error_wrap(*m_mvlc, ec);
}

VMEError MVLC_VMEController::blockReadSwapped(u32 address, const mesytec::mvlc::Blk2eSSTRate &rate,
                                              u16 transfers, QVector<u32> *dest)
{
    std::vector<u32> buffer;
    auto ec = m_mvlc->vmeBlockReadSwapped(address, rate, transfers, buffer);

    dest->clear();
    dest->reserve(buffer.size());
    std::copy(std::begin(buffer), std::end(buffer), std::back_inserter(*dest));

    return error_wrap(*m_mvlc, ec);
}

} // end namespace mvme_mvlc
} // end namespace mesytec
