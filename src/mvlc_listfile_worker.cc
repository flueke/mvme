#include "mvlc_listfile_worker.h"

#include <cassert>
#include <QDebug>
#include <QThread>

#include "mvlc_listfile.h"
#include "mvlc/mvlc_util.h"
#include "mvlc/mvlc_impl_eth.h"
#include "util_zip.h"

// How long to block in paused state on each iteration of the main loop
static const unsigned PauseSleepDuration_ms = 100;

// How long to block waiting for a buffer from the free queue
static const unsigned FreeBufferWaitTimeout_ms = 100;

// Amount of data to read from the listfile using a single read call. The data
// buffers from the free queue are also resized to at least this size.
static const size_t ListfileSingleReadSize = Megabytes(1);

struct MVLCListfileWorker::Private
{
    DAQStats stats;
    DAQState state;
    std::atomic<DAQState> desiredState;
    u32 eventsToRead = 0;
    bool logBuffers = false;
    QIODevice *input = nullptr;
    ListfileBufferFormat format = ListfileBufferFormat::MVLC_ETH;
    u32 nextOutputBufferNumber = 1u;
    DataBuffer previousData;

    Private()
        : state(DAQState::Idle)
        , desiredState(DAQState::Idle)
        , previousData(ListfileSingleReadSize)
    {}
};

MVLCListfileWorker::MVLCListfileWorker(
    ThreadSafeDataBufferQueue *emptyBufferQueue,
    ThreadSafeDataBufferQueue *filledBufferQueue,
    QObject *parent)
    : ListfileReplayWorker(emptyBufferQueue, filledBufferQueue, parent)
    , d(std::make_unique<Private>())
{
    qDebug() << __PRETTY_FUNCTION__;
}

MVLCListfileWorker::~MVLCListfileWorker()
{
}

void MVLCListfileWorker::setListfile(QIODevice *input)
{
    d->input = input;
}

DAQStats MVLCListfileWorker::getStats() const
{
    return d->stats;
}

bool MVLCListfileWorker::isRunning() const
{
    return getState() == DAQState::Running;
}

DAQState MVLCListfileWorker::getState() const
{
    return d->state;
}

void MVLCListfileWorker::setEventsToRead(u32 eventsToRead)
{
    Q_ASSERT(d->state != DAQState::Running);
    d->eventsToRead = eventsToRead;
    d->logBuffers = (eventsToRead == 1);
}

void MVLCListfileWorker::setState(DAQState newState)
{
    d->state = newState;
    d->desiredState = newState;
    emit stateChanged(newState);

    switch (newState)
    {
        case DAQState::Idle:
            emit replayStopped();
            break;

        case DAQState::Starting:
        case DAQState::Running:
        case DAQState::Stopping:
            break;

        case DAQState::Paused:
            emit replayPaused();
            break;
    }
}

// Dequeues a buffer from the free queue and prepares it for use. On timeout
// nullptr is returned.
DataBuffer *MVLCListfileWorker::getOutputBuffer()
{
    DataBuffer *result = dequeue(getEmptyQueue(), FreeBufferWaitTimeout_ms);

    if (result)
    {
        result->used = 0;
        result->id   = d->nextOutputBufferNumber++;
        result->tag  = static_cast<int>(d->format);
        result->ensureFreeSpace(ListfileSingleReadSize);
    }

    return result;
}

// Blocking call which will perform the work
// TODO: handle eventsToRead
void MVLCListfileWorker::start()
{
    Q_ASSERT(getEmptyQueue());
    Q_ASSERT(getFilledQueue());
    Q_ASSERT(d->state == DAQState::Idle);

    if (d->state != DAQState::Idle || !d->input)
        return;

    setState(DAQState::Starting);

    logMessage(QString("Starting replay from %1.").arg(
            get_filename(d->input)));

    auto fileMagic = mvlc_listfile::read_file_magic(*d->input);

    qDebug() << __PRETTY_FUNCTION__ << fileMagic;

    if (fileMagic == mvlc_listfile::FileMagic_ETH)
        d->format = ListfileBufferFormat::MVLC_ETH;
    else if (fileMagic == mvlc_listfile::FileMagic_USB)
        d->format = ListfileBufferFormat::MVLC_USB;
    else
    {
        logMessage("Error: Unknown listfile format. Aborting replay.");
        setState(DAQState::Idle);
        return;
    }

    d->nextOutputBufferNumber = 1u;
    d->stats.start();
    d->stats.listFileTotalBytes = d->input->size();

    setState(DAQState::Running);

    while (true)
    {
        DAQState state = d->state;
        DAQState desiredState = d->desiredState;

        // pause
        if (state == DAQState::Running && desiredState == DAQState::Paused)
        {
            setState(DAQState::Paused);
        }
        // resume
        else if (state == DAQState::Paused && desiredState == DAQState::Running)
        {
            setState(DAQState::Running);
        }
        // stop
        else if (desiredState == DAQState::Stopping)
        {
            break;
        }
        else if (state == DAQState::Running)
        {
            // Note: getOutputBuffer() blocks for at most
            // FreeBufferWaitTimeout_ms
            if (auto destBuffer = getOutputBuffer())
            {
                qint64 bytesRead = readAndProcessBuffer(destBuffer);

                if (bytesRead <= 0)
                {
                    // On read error or empty read put the buffer back onto the
                    // free queue.
                    enqueue(getEmptyQueue(), destBuffer);

                    if (bytesRead < 0)
                    {
                        logMessage(QSL("Error reading from listfile: %1")
                                   .arg(d->input->errorString()));
                    }
                    break;
                }
                else
                {
                    // Flush the buffer
                    enqueue_and_wakeOne(getFilledQueue(), destBuffer);
                    d->stats.totalBuffersRead++;
                    d->stats.totalBytesRead += destBuffer->used;
                }
            }
        }
        else if (state == DAQState::Paused)
        {
            QThread::msleep(PauseSleepDuration_ms);
        }
        else
        {
            Q_ASSERT(!"Unhandled case in MVLCListfileWorker::mainLoop()!");
        }
    }

    d->stats.stop();
    setState(DAQState::Idle);
}

using namespace mesytec::mvlc;

// Follows the framing structure inside the buffer until a partial frame at the
// end is detected. The partial data is moved over to the tempBuffer so that
// the readBuffer ends with a complete frame.
//
// The input buffer must start with a frame header (skip_count will be called
// with the first word of the input buffer on the first iteration).
//
// The SkipCountFunc must return the number of words to skip to get to the next
// frame header or 0 if there is not enough data left in the input iterator to
// determine the frames size.
// Signature of SkipCountFunc:  u32 skip_count(const BufferIterator &iter);
template<typename SkipCountFunc>
inline void fixup_buffer(DataBuffer &readBuffer, DataBuffer &tempBuffer, SkipCountFunc skip_count)
{
    BufferIterator iter(readBuffer.data, readBuffer.used);

    while (!iter.atEnd())
    {
        if (iter.longwordsLeft())
        {
            u32 wordsToSkip = skip_count(iter);

            if (wordsToSkip == 0 || wordsToSkip > iter.longwordsLeft())
            {
                // Move the trailing data into the temporary buffer. This will
                // truncate the readBuffer to the last complete frame or packet
                // boundary.
                auto trailingBytes = iter.bytesLeft();
                move_bytes(readBuffer, tempBuffer, iter.asU8(), trailingBytes);
                return;
            }

            // Skip over the SystemEvent frame or the ETH packet data.
            iter.skip(wordsToSkip, sizeof(u32));
        }
    }
}

// The listfile contains two types of data:
// - System event sections identified by a header word with 0xFA in the highest
//   byte.
// - ETH packet data starting with the two ETH specific header words followed
//   by the packets payload
// The first ETH packet header can never have the value 0xFA because the
// highest two bits are always 0.
inline void fixup_buffer_eth(DataBuffer &readBuffer, DataBuffer &tempBuffer)
{
    auto skip_func = [](const BufferIterator &iter) -> u32
    {
        // Either a SystemEvent header or the first of the two ETH packet headers
        u32 header = iter.peekU32(0);

        if (get_frame_type(header) == frame_headers::SystemEvent)
            return 1u + extract_frame_info(header).len;

        if (iter.longwordsLeft() > 1)
        {
            u32 header1 = iter.peekU32(1);
            eth::PayloadHeaderInfo ethHdrs{ header, header1 };
            return eth::HeaderWords + ethHdrs.dataWordCount();
        }

        // Not enough data to get the 2nd ETH header word.
        return 0u;
    };

    fixup_buffer(readBuffer, tempBuffer, skip_func);
}

inline void fixup_buffer_usb(DataBuffer &readBuffer, DataBuffer &tempBuffer)
{
    auto skip_func = [] (const BufferIterator &iter) -> u32
    {
        u32 header = iter.peekU32(0);
        return 1u + extract_frame_info(header).len;
    };

    fixup_buffer(readBuffer, tempBuffer, skip_func);
}

// Returns the result of QIODevice::read(): the number of bytes transferred or
// a  negative value on error.
qint64 MVLCListfileWorker::readAndProcessBuffer(DataBuffer *destBuffer)
{
    // Move leftover data from the previous buffer into the destination buffer.
    if (d->previousData.used)
    {
        assert(destBuffer->free() >= d->previousData.used);
        std::memcpy(destBuffer->endPtr(), d->previousData.data, d->previousData.used);
        destBuffer->used += d->previousData.used;
        d->previousData.used = 0u;
    }

    // Read data from the input file
    qint64 bytesRead = d->input->read(destBuffer->asCharStar(), destBuffer->free());

    if (bytesRead >= 0)
    {
        destBuffer->used += bytesRead;

        switch (d->format)
        {
            case ListfileBufferFormat::MVLC_ETH:
                fixup_buffer_eth(*destBuffer, d->previousData);
                break;

            case ListfileBufferFormat::MVLC_USB:
                fixup_buffer_usb(*destBuffer, d->previousData);
                break;

                InvalidDefaultCase;
        }
    }

    return bytesRead;
}

// Thread-safe calls, setting internal flags to do the state transition
void MVLCListfileWorker::stop()
{
    d->desiredState = DAQState::Stopping;
}

void MVLCListfileWorker::pause()
{
    d->desiredState = DAQState::Paused;
}

void MVLCListfileWorker::resume()
{
    d->desiredState = DAQState::Running;
}
