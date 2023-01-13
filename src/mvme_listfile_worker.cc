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
#include "mvme_listfile_worker.h"

#include <QCoreApplication>
#include <QThread>

#include "util/perf.h"

MVMEListfileWorker::MVMEListfileWorker(
    ThreadSafeDataBufferQueue *emptyBufferQueue,
    ThreadSafeDataBufferQueue *filledBufferQueue,
    QObject *parent)
: ListfileReplayWorker(emptyBufferQueue, filledBufferQueue, parent)
, m_state(DAQState::Idle)
, m_desiredState(DAQState::Idle)
{
    qDebug() << __PRETTY_FUNCTION__;
}

MVMEListfileWorker::~MVMEListfileWorker()
{
}

void MVMEListfileWorker::setListfile(ListfileReplayHandle *handle)
{
    m_listfile = ListFile(handle->listfile.get());
    m_stats.listFileTotalBytes = m_listfile.size();
    m_stats.listfileFilename = m_listfile.getFileName();
}

void MVMEListfileWorker::setEventsToRead(u32 eventsToRead)
{
    Q_ASSERT(m_state != DAQState::Running);
    m_eventsToRead = eventsToRead;
    m_logBuffers = (eventsToRead == 1);
}

void MVMEListfileWorker::start()
{
    Q_ASSERT(getEmptyQueue());
    Q_ASSERT(getFilledQueue());
    Q_ASSERT(m_state == DAQState::Idle);

    if (m_state != DAQState::Idle || !m_listfile.getInputDevice())
        return;

    m_listfile.open();
    m_listfile.seekToFirstSection();
    m_bytesRead = 0;
    m_totalBytes = m_listfile.size();
    m_stats.start();

    mainLoop();
}

void MVMEListfileWorker::stop()
{
    qDebug() << __PRETTY_FUNCTION__ << "current state =" << DAQStateStrings[m_state];
    m_desiredState = DAQState::Stopping;
}

void MVMEListfileWorker::pause()
{
    qDebug() << __PRETTY_FUNCTION__ << "pausing";
    m_desiredState = DAQState::Paused;
}

void MVMEListfileWorker::resume()
{
    qDebug() << __PRETTY_FUNCTION__ << "resuming";
    m_desiredState = DAQState::Running;
}

static const u32 FreeBufferWaitTimeout_ms = 250;
static const double PauseSleep_ms = 250;

void MVMEListfileWorker::mainLoop()
{
    using namespace ListfileSections;

    setState(DAQState::Running);

    logMessage(QString("Starting replay from %1").arg(m_listfile.getFileName()));

    const auto &lfc = listfile_constants(m_listfile.getFileVersion());

    while (true)
    {
        // pause
        if (m_state == DAQState::Running && m_desiredState == DAQState::Paused)
        {
            setState(DAQState::Paused);
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            setState(DAQState::Running);
        }
        // stop
        else if (m_desiredState == DAQState::Stopping)
        {
            break;
        }
        // stay in running state
        else if (m_state == DAQState::Running)
        {
            DataBuffer *buffer = nullptr;

            {
                QMutexLocker lock(&getEmptyQueue()->mutex);
                while (getEmptyQueue()->queue.isEmpty() && m_desiredState == DAQState::Running)
                {
                    getEmptyQueue()->wc.wait(&getEmptyQueue()->mutex, FreeBufferWaitTimeout_ms);
                }

                if (!getEmptyQueue()->queue.isEmpty())
                {
                    buffer = getEmptyQueue()->queue.dequeue();
                }
            }
            // The mutex is unlocked again at this point

            if (buffer)
            {
                buffer->used = 0;
                bool isBufferValid = false;

                if (unlikely(m_eventsToRead > 0))
                {
                    /* Read single events.
                     * Note: This is the unlikely case! This case only happens if
                     * the user pressed the "1 cycle / next event" button!
                     */

                    bool readMore = true;

                    // Skip non event sections
                    while (readMore)
                    {
                        isBufferValid = m_listfile.readNextSection(buffer);

                        if (isBufferValid)
                        {
                            m_stats.totalBuffersRead++;
                            m_stats.totalBytesRead += buffer->used;
                        }

                        if (isBufferValid && buffer->used >= sizeof(u32))
                        {
                            u32 sectionHeader = *reinterpret_cast<u32 *>(buffer->data);
                            u32 sectionType = (sectionHeader & lfc.SectionTypeMask) >> lfc.SectionTypeShift;

#if 0
                            u32 sectionWords  = (sectionHeader & SectionSizeMask) >> SectionSizeShift;

                            qDebug() << __PRETTY_FUNCTION__ << "got section of type" << sectionType
                                << ", words =" << sectionWords
                                << ", size =" << sectionWords * sizeof(u32)
                                << ", buffer->used =" << buffer->used;
                            qDebug("%s sectionHeader=0x%08x", __PRETTY_FUNCTION__, sectionHeader);
#endif

                            if (sectionType == SectionType_Event)
                            {
                                readMore = false;
                            }
                        }
                        else
                        {
                            readMore = false;
                        }
                    }

                    if (--m_eventsToRead == 0)
                    {
                        // When done reading the requested amount of events transition
                        // to Paused state.
                        m_desiredState = DAQState::Paused;
                    }
                }
                else
                {
                    // Read until buffer is full
                    s32 sectionsRead = m_listfile.readSectionsIntoBuffer(buffer);
                    isBufferValid = (sectionsRead > 0);

                    if (isBufferValid)
                    {
                        m_stats.totalBuffersRead++;
                        m_stats.totalBytesRead += buffer->used;
                    }
                }

                if (!isBufferValid)
                {
                    // Reading did not succeed. Put the previously acquired buffer
                    // back into the free queue. No need to notfiy the wait
                    // condition as there's no one else waiting on it.
                    QMutexLocker lock(&getEmptyQueue()->mutex);
                    getEmptyQueue()->queue.enqueue(buffer);

                    setState(DAQState::Stopping);
                }
                else
                {
                    if (m_logBuffers && getLogger())
                    {
                        logMessage(">>> Begin buffer");
                        BufferIterator bufferIter(buffer->data, buffer->used, BufferIterator::Align32);
                        logBuffer(bufferIter, [this](const QString &str) { this->logMessage(str); });
                        logMessage("<<< End buffer");
                    }
                    // Push the valid buffer onto the output queue.
                    getFilledQueue()->mutex.lock();
                    getFilledQueue()->queue.enqueue(buffer);
                    getFilledQueue()->mutex.unlock();
                    getFilledQueue()->wc.wakeOne();
                }
            }
        }
        // paused
        else if (m_state == DAQState::Paused)
        {
            QThread::msleep(PauseSleep_ms);
        }
        else
        {
            Q_ASSERT(!"Unhandled case in MVMEListfileWorker::mainLoop()!");
        }
    }

    m_stats.stop();
    setState(DAQState::Idle);

    qDebug() << __PRETTY_FUNCTION__ << "exit";
}

void MVMEListfileWorker::setState(DAQState newState)
{
    qDebug() << __PRETTY_FUNCTION__ << DAQStateStrings[m_state] << "->" << DAQStateStrings[newState];

    m_state = newState;
    m_desiredState = newState;
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

    //QCoreApplication::processEvents();
}
