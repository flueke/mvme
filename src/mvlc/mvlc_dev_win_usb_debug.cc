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
#include <QDebug>
#include <vector>
#include "mvlc/mvlc_impl_usb.h"
#include "mvlc/mvlc_error.h"

using namespace mesytec::mvme_mvlc;
using namespace mesytec::mvme_mvlc::usb;

static const std::vector<u32> InitData =
{
    0xf1000000,
    0x02042000,
    0xf3000000,
    0x02042004,
    0x230d0001,
    0x02042008,
    0x00006020,
    0x0204200c,
    0x00000080,
    0x02042010,
    0x230d0001,
    0x02042014,
    0x00006024,
    0x02042018,
    0x000000bb,
    0x0204201c,
    0x230d0001,
    0x02042020,
    0x00006010,
    0x02042024,
    0x00000001,
    0x02042028,
    0x230d0001,
    0x0204202c,
    0x00006036,
    0x02042030,
    0x0000000b,
    0x02042034,
    0x230d0001,
    0x02042038,
    0x0000601a,
    0x0204203c,
    0x00000001,
    0x02042040,
    0x230d0001,
    0x02042044,
    0x00006070,
    0x02042048,
    0x00000007,
    0x0204204c,
    0x230d0001,
    0x02042050,
    0xbb00603a,
    0x02042054,
    0x00000000,
    0x02042058,
    0x230d0001,
    0x0204205c,
    0xbb006090,
    0x02042060,
    0x00000003,
    0x02042064,
    0x230d0001,
    0x02042068,
    0xbb00603c,
    0x0204206c,
    0x00000001,
    0x02042070,
    0x230d0001,
    0x02042074,
    0xbb00603a,
    0x02042078,
    0x00000001,
    0x0204207c,
    0x230d0001,
    0x02042080,
    0xbb006034,
    0x02042084,
    0x00000001,
    0x02042088,
    0xf4000000,
    0x02041200,
    0x00000000,
    0x02041100,
    0x00000100,
    0xf2000000,
};

constexpr u8 get_endpoint(mesytec::mvme_mvlc::Pipe pipe, EndpointDirection dir)
{
    u8 result = 0;

    switch (pipe)
    {
        case mesytec::mvme_mvlc::Pipe::Command:
            result = 0x2;
            break;

        case mesytec::mvme_mvlc::Pipe::Data:
            result = 0x3;
            break;
    }

    if (dir == EndpointDirection::In)
        result |= 0x80;

    return result;
}

/* Observations of the driver function behavior:
 * When setting streaming mode and using the non-ex functions reads can block
 * forever. With the ex functions the reads return FT_OK after the pipe timeout
 * even if 0 bytes have been transferred.
 * Without streaming mode both the ex and non-ex functions work.
 *
 * With streaming mode reads must always be of specified stream size. Otherwise
 * the read after a different sized read will return FT_OTHER_ERROR.
 *
 * When reading less bytes than are available from the controller the
 * additional bytes will be completely lost! There's no software layer doing
 * any buffering. What I do not understand is how to determine the read size
 * then. There's no way to ask the device how many bytes are available to read
 * nor did I find any information about the max transfer size possible from the
 * FT600. The largest packets I've seen with wireshark are 188k bytes large.
 * For now I'll settle with a read and stream size of 1MB and hope that all
 * data will fit.
 *
 * New strategy to implement to make existing MVLCDialog code work as before:
 * Use a single 1MB buffer and always read in chunks of 1MB. Do a device read
 * once a client size read can't be satisfied from buffered data alone.
 * Steps:
 * if data left in buffer: copy min(requested_size, buffered_size) to client buffer.
 * either request is satisfied now or buffer is empty.
 * if more data requested and buffer is empty: perform a 1MB device read into the buffer.
 * copy data from buffer to user dest until request is satisfied or no more
 * data left. if no more data to copy return FT_OK but set transfered size to
 * whatever amount we copied to the dest buffer. This case means there's just
 * no more data available and we returned everything we had.
 */

#define USE_EX_FUNCTIONS 1
#define USE_READ_STREAMPIPES 0
#define READ_STREAMPIPE_SIZE 1024 * 1024
#define USE_BUFFERED_READS 1

std::error_code mvlc_open(void *&handle)
{
    auto st = FT_Create(reinterpret_cast<void *>(0), FT_OPEN_BY_INDEX, &handle);
    if (auto ec = make_error_code(st))
        return ec;

#if USE_READ_STREAMPIPES
    qDebug("Setting read pipes to use streaming mode");
    st = FT_SetStreamPipe(handle, false, true, 0, READ_STREAMPIPE_SIZE);
    if (auto ec = make_error_code(st))
        return ec;
#endif

    return {};
}

std::error_code mvlc_close(void *&handle)
{
    auto st = FT_Close(handle);
    handle = nullptr;
    return make_error_code(st);
}

std::error_code mvlc_write(void *handle, Pipe pipe, const u8 *buffer, size_t size, size_t &bytesTransferred)
{
    ULONG transferred = 0; // FT API needs a ULONG*

#if USE_EX_FUNCTIONS
    FT_STATUS st = FT_WritePipeEx(handle, get_endpoint(pipe, EndpointDirection::Out),
                                  const_cast<u8 *>(buffer), size,
                                  &transferred,
                                  nullptr);
#else
    FT_STATUS st = FT_WritePipe(handle, get_endpoint(pipe, EndpointDirection::Out),
                                const_cast<u8 *>(buffer), size,
                                &transferred,
                                nullptr);
#endif

    bytesTransferred = transferred;

    return make_error_code(st);
}

std::error_code mvlc_low_level_read(void *handle, Pipe pipe, u8* buffer, size_t size, size_t &bytesTransferred)
{
    ULONG transferred = 0; // FT API wants a ULONG* parameter

#if USE_EX_FUNCTIONS
    FT_STATUS st = FT_ReadPipeEx(handle, get_endpoint(pipe, EndpointDirection::In),
                                 buffer, size,
                                 &transferred,
                                 nullptr);
#else
    FT_STATUS st = FT_ReadPipe(handle, get_endpoint(pipe, EndpointDirection::In),
                               buffer, size,
                               &transferred,
                               nullptr);
#endif

    bytesTransferred = transferred;

    return make_error_code(st);
}

static const size_t BufferSize  = USBSingleTransferMaxBytes;
static const size_t BufferCount = 2;
using ReadBuffers = std::array<ReadBuffer<BufferSize>, BufferCount>;

std::error_code mvlc_read(void *handle, Pipe pipe, u8* dest, size_t size, size_t &bytesTransferred)
{
#if USE_BUFFERED_READS
    static ReadBuffers buffers;
    static int lastReadBufferIndex = -1;

    if (lastReadBufferIndex < 0)
    {
        size_t transferred = 0u;
        auto &buffer = buffers[0];

        auto ec = mvlc_low_level_read(handle, pipe,
                                      buffer.data.data(),
                                      buffer.data.size(),
                                      transferred);

        buffer.first = buffer.data.data();
        buffer.last  = buffer.first + transferred;
        lastReadBufferIndex = 0;

        qDebug("%s: initial read into buffer[0]: ec=%s, max_size=%u, transferred_size=%u",
               __PRETTY_FUNCTION__, ec.message().c_str(), buffer.data.size(), transferred);

        if (ec && ec != ErrorType::Timeout)
            return ec;
    }

    int nextReadBufferIndex = lastReadBufferIndex + 1;
    if (nextReadBufferIndex >= BufferCount) nextReadBufferIndex = 0;

    if (buffers[lastReadBufferIndex].size() >= size)
    {
        auto &buffer = buffers[lastReadBufferIndex];

        qDebug("%s: read request of size %u can be satisfied using buffer %d of size %u",
               __PRETTY_FUNCTION__, size, lastReadBufferIndex, buffer.size());

        memcpy(dest, buffer.first, size);
        buffer.first += size;
        bytesTransferred = size;

        qDebug("%s: %u bytes left in buffer %d",
               __PRETTY_FUNCTION__, buffer.size(), lastReadBufferIndex);


        if (buffer.size() == 0)
            buffer.clear();
    }
    else if (buffers[lastReadBufferIndex].size() + buffers[nextReadBufferIndex].size() >= size)
    {
        auto &buffer0 = buffers[lastReadBufferIndex];
        auto &buffer1 = buffers[nextReadBufferIndex];

        qDebug("%s: read request of size %u could be satisfied using both buffers: total_size=%u",
               __PRETTY_FUNCTION__, size, buffer0.size() + buffer1.size());
        throw "implement me!";
    }
    else
    {
        auto &buffer0 = buffers[lastReadBufferIndex];
        auto &buffer1 = buffers[nextReadBufferIndex];

        qDebug("%s: read request of size %u can not be satisfied from buffered data: buffered_size=%u",
               __PRETTY_FUNCTION__, size, buffer0.size() + buffer1.size());

        if (buffer0.size() != 0 && buffer1.size() != 0)
            throw "no free buffer!";

        if (buffer1.size() == 0)
        {
            qDebug("%s: would now read into buffer %d",
                   __PRETTY_FUNCTION__, nextReadBufferIndex);
            throw "implement me";
        }
    }

    return {};
#else
    auto ec = mvlc_low_level_read(handle, pipe, dest, size, bytesTransferred);
    qDebug("%s: pipe=%u, read of size %u: ec=%s, transferred=%u",
           __PRETTY_FUNCTION__, pipe, size, ec.message().c_str(), bytesTransferred);
    return ec;
#endif
}

int main(int argc, char *argv[])
{
    try
    {
        FT_STATUS st = FT_OK;
        void *handle = nullptr;

        // 
        // open
        //
        if (auto ec = mvlc_open(handle))
            throw ec;

        qDebug("connected to MVLC");

        auto do_test = [&]()
        {
            //
            // write init stack data
            //
            size_t bytesToTransfer = InitData.size() * sizeof(u32);
            size_t bytesTransferred = 0u;

            qDebug("writing and executing MTDC init stack, size=%u bytes", bytesToTransfer);

            if (auto ec = mvlc_write(handle, Pipe::Command,
                                     reinterpret_cast<const u8 *>(InitData.data()),
                                     bytesToTransfer,
                                     bytesTransferred))
            {
                throw ec;
            }

            if (bytesTransferred != bytesToTransfer)
            {
                qDebug("error writing data: bytesToTransfer=%u != bytesTransferred=%u",
                       bytesToTransfer, bytesTransferred);
                throw 42;
            }

            //
            // read response data
            //

            // read mirror response header
            u32 responseHeader = 0u;
            {
                std::vector<u32> readBuffer(1);
                bytesToTransfer = readBuffer.size() * sizeof(u32);
                bytesTransferred = 0u;

                qDebug("reading response header, max bytes=%u", bytesToTransfer);

                auto ec = mvlc_read(handle, Pipe::Command,
                                    reinterpret_cast<u8 *>(readBuffer.data()),
                                    bytesToTransfer,
                                    bytesTransferred);

                qDebug("read result=%s, bytesTransferred=%u", ec.message().c_str(), bytesTransferred);

                if (ec && ec != ErrorType::Timeout)
                    throw ec;

                responseHeader = readBuffer[0];
            }

            // read mirror response contents
            {
                size_t responseSize = responseHeader & FrameSizeMask;
                std::vector<u32> readBuffer(responseSize);
                bytesToTransfer = readBuffer.size() * sizeof(u32);
                bytesTransferred = 0u;

                qDebug("reading response contents, max bytes=%u", bytesToTransfer);

                auto ec = mvlc_read(handle, Pipe::Command,
                                    reinterpret_cast<u8 *>(readBuffer.data()),
                                    bytesToTransfer,
                                    bytesTransferred);

                qDebug("read result=%s, bytesTransferred=%u", ec.message().c_str(), bytesTransferred);

                if (ec && ec != ErrorType::Timeout)
                    throw ec;
            }

            // read stack response header
            {
                std::vector<u32> readBuffer(1);
                bytesToTransfer = readBuffer.size() * sizeof(u32);
                bytesTransferred = 0u;

                qDebug("reading response header, max bytes=%u", bytesToTransfer);

                auto ec = mvlc_read(handle, Pipe::Command,
                                    reinterpret_cast<u8 *>(readBuffer.data()),
                                    bytesToTransfer,
                                    bytesTransferred);

                qDebug("read result=%s, bytesTransferred=%u", ec.message().c_str(), bytesTransferred);

                if (ec && ec != ErrorType::Timeout)
                    throw ec;

                responseHeader = readBuffer[0];
            }
        };

        //do_test();
        auto t = std::thread(do_test);
        using Clock = std::chrono::high_resolution_clock;
        auto tStart = Clock::now();

#if 1
        while (true)
        {
            auto elapsed = Clock::now() - tStart;

            if (elapsed > std::chrono::seconds(10))
            {
                qDebug("breaking out of main test loop");
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(250));

            qDebug("main test loop iteration");
        }
#endif

        if (t.joinable())
            t.join();

        qDebug("test thread is done, closing mvlc handle");


        //
        // close
        //
        if (auto ec = mvlc_close(handle))
            throw ec;

        qDebug("closed connection to MVLC");
    }
    catch (const std::error_code &ec)
    {
        qDebug("caught std::error_code: %s", ec.message().c_str());
        return 1;
    }

    return 0;
}
