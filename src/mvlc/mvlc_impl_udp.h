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

namespace mesytec
{
namespace mvlc
{
namespace udp
{

using PacketSizeMap = std::unordered_map<u16, u64>; // size -> count
using HeaderTypeMap = std::unordered_map<u8, u64>;  // header type byte -> count

struct LIBMVME_MVLC_EXPORT PipeStats
{
    // Total number of received UDP packets.
    u64 receivedPackets = 0u;

    // Total number of received bytes including MVLC protocol overhead. This is
    // the sum of the payload sizes of the received UDP packets.
    u64 receivedBytes = 0u;

    // Packets shorther than the header size (2 * 32 bit).
    u64 shortPackets = 0u;

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
        return (header0() >> header0::PacketChannelShift) & header0::PacketChannelMask;
    }

    inline u16 packetNumber() const
    {
        return (header0() >> header0::PacketNumberShift)  & header0::PacketNumberMask;
    }

    inline u16 dataWordCount() const
    {
        return (header0() >> header0::NumDataWordsShift)  & header0::NumDataWordsMask;
    }

    inline u16 udpTimestamp() const
    {
        return (header1() >> header1::TimestampShift)     & header1::TimestampMask;
    }

    inline u16 nextHeaderPointer() const
    {
        return (header1() >> header1::HeaderPointerShift) & header1::HeaderPointerMask;
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

        ConnectionType connectionType() const override { return ConnectionType::UDP; }

        std::error_code getReadQueueSize(Pipe pipe, u32 &dest) override;

        std::array<PipeStats, PipeCount> getPipeStats() const;
        std::array<PacketChannelStats, NumPacketChannels> getPacketChannelStats() const;

    private:
        int getSocket(Pipe pipe) { return pipe == Pipe::Command ? m_cmdSock : m_dataSock; }

        std::string m_host;
        bool m_isConnected = false;
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
};

// Given the previous and current packet numbers returns the number of lost
// packets in-between taking overflow into account.
s32 calc_packet_loss(u16 lastPacketNumber, u16 packetNumber);

} // end namespace udp
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_IMPL_UDP_H__ */
