#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <quazip.h>
#include <quazipfile.h>

#include "data_buffer_queue.h"

using std::cout;
using std::endl;

struct Context
{
    std::vector<std::unique_ptr<DataBuffer>> bufferStore;
    ThreadSafeDataBufferQueue emptyBuffers;
    ThreadSafeDataBufferQueue filledBuffers;

    std::atomic<bool> quit = {};

    std::atomic<size_t> bytesRead = {};
    std::atomic<size_t> readCount = {};

    std::atomic<size_t> bytesWritten = {};
    std::atomic<size_t> writeCount = {};

    std::atomic<size_t> writeErrorCount;
};

void reader(QIODevice &input, Context &ctx)
{
    while (!input.atEnd())
    {
        auto buffer = ctx.emptyBuffers.dequeue(std::chrono::milliseconds(100));

        if (!buffer)
            continue;

        qint64 readLen = input.read(reinterpret_cast<char *>(buffer->data), buffer->size);
        if (readLen < 0)
            break;

        buffer->used = readLen;
        ctx.bytesRead += readLen;
        ctx.readCount++;

        ctx.filledBuffers.enqueue(buffer);

        if (readLen == 0)
            break;
    }

    ctx.quit = true;
    qDebug() << __PRETTY_FUNCTION__ << "reader quitting";
}

void writer(QIODevice *outdev, Context &ctx)
{
    DataBuffer *buffer = nullptr;

    while (!(ctx.quit && ctx.filledBuffers.empty()))
    {
        if ((buffer = ctx.filledBuffers.dequeue(std::chrono::milliseconds(100))))
        {
            if (buffer->used == 0)
                break;

            if (outdev && outdev->isOpen())
            {
                qint64 bytesWritten = outdev->write(
                    reinterpret_cast<const char *>(buffer->data), buffer->used);

                if (bytesWritten != static_cast<qint64>(buffer->used))
                    ++ctx.writeErrorCount;

                ctx.bytesWritten += bytesWritten;
                ctx.writeCount++;
            }

            ctx.emptyBuffers.enqueue(buffer);
        }
        else
        {
            qDebug() << __PRETTY_FUNCTION__ << "no work!";
        }
    }

    if (buffer)
        ctx.emptyBuffers.enqueue(buffer);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cout << "Usage: " << argv[0] << " <out_zipfile> <in_file>" << endl;
        return 1;
    }

    QString outFilename = argv[1];
    QString inFilename = argv[2];

    QFile input(inFilename);

    if (!input.open(QIODevice::ReadOnly))
        return 2;

    QuaZip archive;
    archive.setZipName(outFilename);

    if (!archive.open(QuaZip::mdCreate))
        return 3;

    QuaZipNewInfo outFileInfo("testfile");
    QuaZipFile outFile(&archive);
    if (!outFile.open(QIODevice::WriteOnly, outFileInfo,
                      nullptr, 0, // password, crc (must be 0)
                      Z_DEFLATED, // compression method
                      1)) // compression level
    {
        return 4;
    }

    Context ctx;

    static const size_t BufferCount = 10;
    static const size_t BufferSize = 1024 * 1024;

    for (size_t i=0; i<BufferCount;i++)
    {
        ctx.bufferStore.emplace_back(std::make_unique<DataBuffer>(BufferSize));
        ctx.emptyBuffers.enqueue(ctx.bufferStore.back().get());
    }

    auto tStart = std::chrono::steady_clock::now();

    std::thread writerThread(writer, &outFile, std::ref(ctx));
    std::thread readerThread(reader, std::ref(input), std::ref(ctx));

    writerThread.join();
    readerThread.join();

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = tEnd - tStart;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    auto megaBytesPerSecond = (ctx.bytesWritten / (1024 * 1024)) / seconds;

    cout << "Wrote the listfile in " << ctx.writeCount << " iterations, totalBytes=" << ctx.bytesWritten << endl;
    cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
    cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;
}
