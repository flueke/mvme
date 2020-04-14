#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <mz.h>
#include <mz_zip.h>
#include <mz_strm.h>
#include <mz_zip_rw.h>
#include <mz_strm_os.h>

#include "mesytec-mvlc/util/int_types.h"
#include "mesytec-mvlc/util/threadsafequeue.h"

using std::cout;
using std::endl;
using namespace mesytec::mvlc;

template<size_t Capacity = 1024 * 1024>
struct Buffer
{
    std::array<char, Capacity> storage;
    size_t size = 0;
};

using Queue = ThreadSafeQueue<Buffer<> *>;

struct Context
{
    Queue emptyBuffers;
    Queue fullBuffers;

    std::atomic<size_t> bytesRead = {};
    std::atomic<size_t> readCount = {};

    std::atomic<size_t> bytesWritten = {};
    std::atomic<size_t> writeCount = {};
};

void reader(std::ifstream &input, Context &ctx)
{
    size_t readLen = 0u;

    do
    {
#if 1
        auto buffer = ctx.emptyBuffers.dequeue(std::chrono::milliseconds(50));

        if (!buffer)
            continue;
#else
        auto buffer = ctx.emptyBuffers.dequeue_blocking();
        assert(buffer);
#endif

        input.read(buffer->storage.data(), buffer->storage.size());
        readLen = input.gcount();
        buffer->size = readLen;
        ctx.bytesRead += readLen;
        ctx.readCount++;

        ctx.fullBuffers.enqueue(buffer);
    } while (readLen > 0);
}

void writer(void *zipWriter, Context &ctx)
{
    Buffer<> *buffer = nullptr;

    while (true)
    {
#if 1
        buffer = ctx.fullBuffers.dequeue(std::chrono::milliseconds(50));

        if (!buffer)
            continue;
#else
        buffer = ctx.fullBuffers.dequeue_blocking();
        assert(buffer);
#endif

        if (buffer->size == 0)
            break;

        int written = mz_zip_writer_entry_write(zipWriter, buffer->storage.data(), buffer->size);

        if (written < 0)
            break;

        if (static_cast<size_t>(written) != buffer->size)
            break;

        ctx.bytesWritten += written;
        ctx.writeCount++;

        buffer->size = 0;
        ctx.emptyBuffers.enqueue(buffer);
        buffer = nullptr;
    }

    if (buffer)
        ctx.emptyBuffers.enqueue(buffer);

}

int main(int argc, char *argv[])
{
    std::vector<Buffer<>> bufferStore(10);
    Context ctx;

    for (auto &buffer: bufferStore)
        ctx.emptyBuffers.enqueue(&buffer);

    assert(ctx.emptyBuffers.size() == bufferStore.size());

    if (argc != 3)
    {
        cout << "Usage: " << argv[0] << " <out_zipfile> <in_file>" << endl;
        return 1;
    }

    std::string outFilename = argv[1];
    std::string inFilename = argv[2];

    std::ifstream input(inFilename, std::ios::binary);

    if (!input.is_open())
        return 2;

    void *zipWriter = nullptr;
    void *fileStream = nullptr;

    mz_zip_writer_create(&zipWriter);
    mz_zip_writer_set_compress_method(zipWriter, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(zipWriter, 1);

    mz_stream_os_create(&fileStream);

    if (auto err = mz_stream_os_open(fileStream, outFilename.c_str(), MZ_OPEN_MODE_CREATE | MZ_OPEN_MODE_WRITE))
        return 3;

    if (auto err = mz_zip_writer_open(zipWriter, fileStream))
        return 4;

    mz_zip_file file_info = {};
    file_info.filename = "testfile";
    file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
    file_info.zip64 = MZ_ZIP64_FORCE;

    if (auto err = mz_zip_writer_entry_open(zipWriter, &file_info))
        return 5;

    auto tStart = std::chrono::steady_clock::now();

    std::thread writerThread(writer, zipWriter, std::ref(ctx));
    std::thread readerThread(reader, std::ref(input), std::ref(ctx));

    //reader(input, ctx);

    writerThread.join();
    readerThread.join();

    mz_zip_writer_close(zipWriter);
    mz_stream_os_delete(&fileStream);
    mz_zip_writer_delete(&zipWriter);

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = tEnd - tStart;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    auto megaBytesPerSecond = (ctx.bytesWritten / (1024 * 1024)) / seconds;

    cout << "Wrote the listfile in " << ctx.writeCount << " iterations, totalBytes=" << ctx.bytesWritten << endl;
    cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
    cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;
}
