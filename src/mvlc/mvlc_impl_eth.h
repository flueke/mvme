/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVLC_IMPL_UDP_H__
#define __MVLC_IMPL_UDP_H__

#include <array>
#include <string>
#include <unordered_map>

#ifndef __WIN32
#include <netinet/ip.h> // sockaddr_in
#else
#include <winsock2.h>
#endif

#include "libmvme_mvlc_export.h"
#include "mvlc/mvlc_impl_abstract.h"
#include "mvlc/mvlc_threading.h"

namespace mesytec
{
namespace mvlc
{
namespace eth
{

using PacketSizeMap = std::unordered_map<u16, u64>; // size -> count
using HeaderTypeMap = std::unordered_map<u8, u64>;  // header type byte -> count

struct LIBMVME_MVLC_EXPORT PipeStats
{
    // Number of calls to read_packet() for the specified pipe.
    u64 receiveAttempts = 0u;

    // Total number of received UDP packets.
    u64 receivedPackets = 0u;

    // Total number of received bytes including MVLC protocol overhead. This is
    // the sum of the payload sizes of the received UDP packets.
    u64 receivedBytes = 0u;

    // Packets shorther than the header size (2 * 32 bit).
    u64 shortPackets = 0u;

    // Packets where (len % sizeof(u32) != 0), meaning there are residual bytes
    // at the end.
    u64 packetsWithResidue = 0u;

    u64 noHeader = 0u;          // Packets where nextHeaderPointer = 0xffff
    u64 headerOutOfRange = 0u;  // Header points outside the packet data
    u64 packetChannelOutOfRange = 0u;
    u64 lostPackets = 0u;

    PacketSizeMap packetSizes;
    HeaderTypeMap headerTypes;
};

struct LIBMVME_MVLC_EXPORT PacketChannelStats
{
    u64 receivedPackets = 0u;
    u64 receivedBytes = 0u;
    u64 lostPackets = 0u;
    u64 noHeader = 0u;          // Packets where nextHeaderPointer = 0xffff
    u64 headerOutOfRange = 0u;  // Header points outside the packet data

    PacketSizeMap packetSizes;
    HeaderTypeMap headerTypes;
};

struct LIBMVME_MVLC_EXPORT PayloadHeaderInfo
{
    u32 header0;
    u32 header1;

    inline u16 packetChannel() const
    {
        return (header0 >> header0::PacketChannelShift) & header0::PacketChannelMask;
    }

    inline u16 packetNumber() const
    {
        return (header0 >> header0::PacketNumberShift)  & header0::PacketNumberMask;
    }

    inline u16 dataWordCount() const
    {
        return (header0 >> header0::NumDataWordsShift)  & header0::NumDataWordsMask;
    }

    inline u16 udpTimestamp() const
    {
        return (header1 >> header1::TimestampShift)     & header1::TimestampMask;
    }

    inline u16 nextHeaderPointer() const
    {
        return (header1 >> header1::HeaderPointerShift) & header1::HeaderPointerMask;
    }

    inline u16 isNextHeaderPointerPresent() const
    {
        return nextHeaderPointer() != header1::NoHeaderPointerPresent;
    }
};

struct LIBMVME_MVLC_EXPORT PacketReadResult
{
    std::error_code ec;
    u8 *buffer;             // Equal to the dest pointer passed to read_packet()
    u16 bytesTransferred;
    s32 lostPackets;        // Loss between the previous and current packets

    inline bool hasHeaders() const { return bytesTransferred >= HeaderBytes; }

    inline u32 header0() const { return reinterpret_cast<u32 *>(buffer)[0]; }
    inline u32 header1() const { return reinterpret_cast<u32 *>(buffer)[1]; }

    inline u16 packetChannel() const
    {
        return PayloadHeaderInfo{header0(), header1()}.packetChannel();
    }

    inline u16 packetNumber() const
    {
        return PayloadHeaderInfo{header0(), header1()}.packetNumber();
    }

    inline u16 dataWordCount() const
    {
        return PayloadHeaderInfo{header0(), header1()}.dataWordCount();
    }

    inline u16 udpTimestamp() const
    {
        return PayloadHeaderInfo{header0(), header1()}.udpTimestamp();
    }

    inline u16 nextHeaderPointer() const
    {
        return PayloadHeaderInfo{header0(), header1()}.nextHeaderPointer();
    }

    inline u16 availablePayloadWords() const
    {
        return (bytesTransferred - HeaderBytes) / sizeof(u32);
    }

    inline u16 leftoverBytes() const
    {
        return bytesTransferred % sizeof(u32);
    }

    inline u32 *payloadBegin() const
    {
        return reinterpret_cast<u32 *>(buffer + HeaderBytes);
    }

    inline u32 *payloadEnd() const
    {
        return payloadBegin() + availablePayloadWords();
    }

    inline bool isNextHeaderPointerValid() const
    {
        const u16 nhp = nextHeaderPointer();

        if (nhp != header1::NoHeaderPointerPresent)
            return payloadBegin() + nhp < payloadEnd();

        return true;
    }
};

class LIBMVME_MVLC_EXPORT Impl: public AbstractImpl
{
    public:
        explicit Impl(const std::string &host = {});
        ~Impl();

        std::error_code connect() override;
        std::error_code disconnect() override;
        bool isConnected() const override;

        std::error_code setWriteTimeout(Pipe pipe, unsigned ms) override;
        std::error_code setReadTimeout(Pipe pipe, unsigned ms) override;

        unsigned getWriteTimeout(Pipe pipe) const override;
        unsigned getReadTimeout(Pipe pipe) const override;

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred) override;

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred) override;

        PacketReadResult read_packet(Pipe pipe, u8 *buffer, size_t size);

        ConnectionType connectionType() const override { return ConnectionType::ETH; }
        std::string connectionInfo() const override;

        std::error_code getReadQueueSize(Pipe pipe, u32 &dest) override;

        std::array<PipeStats, PipeCount> getPipeStats() const;
        std::array<PacketChannelStats, NumPacketChannels> getPacketChannelStats() const;
        void resetPipeAndChannelStats();

        // These methods return the remote IPv4 address used for the command
        // and data sockets respectively. This is the address resolved from the
        // host string given to the constructor.
        u32 getCmdAddress() const;
        u32 getDataAddress() const;

        // Returns the host/IP string given to the constructor.
        std::string getHost() const { return m_host; }

        sockaddr_in getCmdSockAddress() const { return m_cmdAddr; }
        sockaddr_in getDataSockAddress() const { return m_dataAddr; }

        void setDisableTriggersOnConnect(bool b)
        {
            m_disableTriggersOnConnect = b;
        }

        bool disableTriggersOnConnect() const
        {
            return m_disableTriggersOnConnect;
        }

    private:
        int getSocket(Pipe pipe) { return pipe == Pipe::Command ? m_cmdSock : m_dataSock; }

        std::string m_host;
        int m_cmdSock = -1;
        int m_dataSock = -1;
        struct sockaddr_in m_cmdAddr = {};
        struct sockaddr_in m_dataAddr = {};

        std::array<unsigned, PipeCount> m_writeTimeouts = {
            DefaultWriteTimeout_ms, DefaultWriteTimeout_ms
        };

        std::array<unsigned, PipeCount> m_readTimeouts = {
            DefaultReadTimeout_ms, DefaultReadTimeout_ms
        };

        // Used internally for buffering in read()
        struct ReceiveBuffer
        {
            std::array<u8, JumboFrameMaxSize> buffer;
            u8 *start = nullptr; // start of unconsumed payload data
            u8 *end = nullptr; // end of packet data

            u32 header0() { return reinterpret_cast<u32 *>(buffer.data())[0]; }
            u32 header1() { return reinterpret_cast<u32 *>(buffer.data())[1]; }

            // number of bytes available
            size_t available() { return end - start; }
            void reset() { start = end = nullptr; }
        };

        std::array<ReceiveBuffer, PipeCount> m_receiveBuffers;
        std::array<PipeStats, PipeCount> m_pipeStats;
        std::array<PacketChannelStats, NumPacketChannels> m_packetChannelStats;
        std::array<s32, NumPacketChannels> m_lastPacketNumbers;
        bool m_disableTriggersOnConnect = false;
        mutable Mutex m_statsMutex;
};

// Given the previous and current packet numbers returns the number of lost
// packets in-between, taking overflow into account.
s32 calc_packet_loss(u16 lastPacketNumber, u16 packetNumber);

} // end namespace eth
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_IMPL_UDP_H__ */
