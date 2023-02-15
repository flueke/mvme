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
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <iostream>
#include <system_error>

#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <mesytec-mvlc/mvlc_util.h>
#include "mvlc/mvlc_util.h"
#include "util.h"

using std::cerr;
using std::endl;
using namespace mesytec;

void process_file(u32 *addr, size_t size)
{
    BufferIterator iter(addr, size);

    size_t linearPacketNumber = 0;
    std::array<s32, mvlc::eth::header0::PacketChannelMask> lastPacketNumbers;
    lastPacketNumbers.fill(-1);

    while (!iter.atEnd())
    {
        ptrdiff_t packetOffset = iter.current32BitOffset();
        mvlc::eth::PayloadHeaderInfo ethHeaders{ iter.extractU32(), iter.extractU32() };

        u16 packetChannel = ethHeaders.packetChannel();

        assert(packetChannel < lastPacketNumbers.size());

        s32 packetLoss = 0;
        if (lastPacketNumbers[packetChannel] >= 0)
            packetLoss = mvlc::eth::calc_packet_loss(lastPacketNumbers[packetChannel], ethHeaders.packetNumber());


        printf("pkt %lu @ 32-bit offset %lu:\n", linearPacketNumber, packetOffset);

        printf(" header0=0x%08x, packetChannel=%u, packetNumber=%u, dataWordCount=%u, lossToPrevious=%d\n",
               ethHeaders.header0, ethHeaders.packetChannel(), ethHeaders.packetNumber(), ethHeaders.dataWordCount(),
               packetLoss);

        printf(" header1=0x%08x, udpTimestamp=%u, nextHeaderPointer=%u\n",
               ethHeaders.header1, ethHeaders.udpTimestamp(), ethHeaders.nextHeaderPointer());

        const u8* endOfPacket = iter.buffp + ethHeaders.dataWordCount() * sizeof(u32);
        u16 packetWordIndex = 0u;
        u16 nextHeaderOffset = ethHeaders.nextHeaderPointer();
        u32 frameHeader = 0u;

        while (iter.buffp < endOfPacket)
        {
            if (packetWordIndex == nextHeaderOffset)
            {
                frameHeader = iter.extractU32();
                ++packetWordIndex;
                size_t wordsLeftInPacket = (endOfPacket - iter.buffp) / sizeof(u32);

                auto frameInfo = mvlc::extract_frame_info(frameHeader);

                printf("  frameHeader=0x%08x: %s, wordsLeftInPacket=%lu\n",
                       frameHeader, mvlc::decode_frame_header(frameHeader).c_str(),
                       wordsLeftInPacket);

                const u8* frameEnd = iter.buffp + frameInfo.len * sizeof(u32);

                while (iter.buffp < endOfPacket && iter.buffp < frameEnd)
                {
                    printf("   0x%08x\n", iter.extractU32());
                    ++packetWordIndex;
                }
            }
            else
            {
                printf("  0x%08x\n", iter.extractU32());
                ++packetWordIndex;
            }


        }
        assert(packetWordIndex == ethHeaders.dataWordCount());


        lastPacketNumbers[packetChannel] = ethHeaders.packetNumber();
        ++linearPacketNumber;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        cerr << "Usage: " << argv[0] << " <file>" << endl;
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);

    if (fd == 1)
        throw std::system_error(errno, std::system_category());

    struct stat fileStat;

    if (fstat(fd, &fileStat) == -1)
        throw std::system_error(errno, std::system_category());

    if (fileStat.st_size == 0)
        throw std::runtime_error("empty input file");

    printf("fileStat.st_size=%lu\n", fileStat.st_size);

    void *mapping = mmap(nullptr, fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (mapping == MAP_FAILED)
        throw std::system_error(errno, std::system_category());

    process_file(reinterpret_cast<u32 *>(mapping), fileStat.st_size / sizeof(u32));

    munmap(mapping, fileStat.st_size);

    return 0;
}
