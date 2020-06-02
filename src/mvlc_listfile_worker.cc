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
#include "mvlc_listfile_worker.h"

#include <cassert>
#include <QDebug>
#include <QThread>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <stdexcept>

#include "mvlc_listfile.h"
#include "mvlc/mvlc_util.h"
#include "util_zip.h"

using namespace mesytec;

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
    ListfileBufferFormat format = ListfileBufferFormat::MVLC_ETH;
    u32 nextOutputBufferNumber = 1u;
    mvlc::ReadoutBufferQueues *snoopQueues = nullptr;
    ListfileReplayHandle *replayHandle = nullptr;

    std::unique_ptr<mesytec::mvlc::ReplayWorker> mvlcReplayWorker;
    std::unique_ptr<mesytec::mvlc::listfile::ZipReader> mvlcZipReader;

    Private()
        : state(DAQState::Idle)
        , desiredState(DAQState::Idle)
    {}
};

MVLCListfileWorker::MVLCListfileWorker(QObject *parent)
    : ListfileReplayWorker(parent)
    , d(std::make_unique<Private>())
{
}

MVLCListfileWorker::~MVLCListfileWorker()
{
}

void MVLCListfileWorker::setSnoopQueues(mesytec::mvlc::ReadoutBufferQueues *queues)
{
    d->snoopQueues = queues;
}

void MVLCListfileWorker::setListfile(ListfileReplayHandle *handle)
{
    QIODevice *input = handle->listfile.get();

    if (qobject_cast<QFile *>(input))
        throw std::runtime_error("MVLC replays from flat file are not supported yet.");

    d->replayHandle = handle;

    if (auto inZipFile = qobject_cast<QuaZipFile *>(input))
        d->stats.listfileFilename = inZipFile->getZipName();
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
    assert(d->state == DAQState::Idle);
    assert(d->snoopQueues);

    if (d->state != DAQState::Idle || !d->replayHandle)
        return;

    if (d->replayHandle->format != ListfileBufferFormat::MVLC_ETH
        && d->replayHandle->format != ListfileBufferFormat::MVLC_USB)
    {
        logMessage("Error: Unknown listfile format. Aborting replay.");
        return;
    }

    setState(DAQState::Starting);


    try
    {

        logMessage(QString("Starting replay from %1.%2")
                   .arg(d->replayHandle->inputFilename)
                   .arg(d->replayHandle->listfileFilename));

        d->mvlcZipReader = std::make_unique<mvlc::listfile::ZipReader>();
        d->mvlcZipReader->openArchive(d->replayHandle->inputFilename.toStdString());
        auto readHandle = d->mvlcZipReader->openEntry(d->replayHandle->listfileFilename.toStdString());

        d->mvlcReplayWorker = std::make_unique<mvlc::ReplayWorker>(
            *d->snoopQueues,
            readHandle);

        auto fStart = d->mvlcReplayWorker->start();

        if (auto ec = fStart.get())
            throw ec;
    }
    catch (const std::error_code &ec)
    {
        logError(ec.message().c_str());
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
        logError(QSL("VME Script parse error: ") + e.what());
    }

    d->stats.stop();
    setState(DAQState::Idle);

#if 0
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
#endif
}

using namespace mesytec::mvme_mvlc;

// Returns the result of QIODevice::read(): the number of bytes transferred or
// a  negative value on error.
qint64 MVLCListfileWorker::readAndProcessBuffer(DataBuffer *destBuffer)
{
#if 0
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
#endif
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

void MVLCListfileWorker::logError(const QString &msg)
{
    logMessage(QSL("MVLC Replay Error: %1").arg(msg));
}
