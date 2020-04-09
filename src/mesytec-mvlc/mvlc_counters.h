#ifndef __MESYTEC_MVLC_MVLC_COUNTERS_H__
#define __MESYTEC_MVLC_MVLC_COUNTERS_H__

#include <unordered_map>

#include "mesytec-mvlc_export.h"
#include "util/int_types.h"

namespace mesytec
{
namespace mvlc
{
namespace eth
{

using PacketSizeMap = std::unordered_map<u16, u64>; // size -> count
using HeaderTypeMap = std::unordered_map<u8, u64>;  // header type byte -> count

struct MESYTEC_MVLC_EXPORT PipeStats
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

struct MESYTEC_MVLC_EXPORT PacketChannelStats
{
    u64 receivedPackets = 0u;
    u64 receivedBytes = 0u;
    u64 lostPackets = 0u;
    u64 noHeader = 0u;          // Packets where nextHeaderPointer = 0xffff
    u64 headerOutOfRange = 0u;  // Header points outside the packet data

    PacketSizeMap packetSizes;
    HeaderTypeMap headerTypes;
};

}


}
}

#endif /* __MESYTEC_MVLC_MVLC_COUNTERS_H__ */
