#include "mvme_listfile_worker.h"

#include <QCoreApplication>
#include <QThread>

#include "util/perf.h"

MVMEListfileWorker::MVMEListfileWorker(DAQStats &stats, QObject *parent)
    : QObject(parent)
    , m_stats(stats)
    , m_state(DAQState::Idle)
    , m_desiredState(DAQState::Idle)
{
}

MVMEListfileWorker::~MVMEListfileWorker()
{
}

void MVMEListfileWorker::setListFile(ListFile *listFile)
{
    m_listFile = listFile;
}

void MVMEListfileWorker::setEventsToRead(u32 eventsToRead)
{
    Q_ASSERT(m_state != DAQState::Running);
    m_eventsToRead = eventsToRead;
    m_logBuffers = (eventsToRead == 1);
}

void MVMEListfileWorker::start()
{
    Q_ASSERT(m_freeBuffers);
    Q_ASSERT(m_fullBuffers);
    Q_ASSERT(m_state == DAQState::Idle);

    if (m_state != DAQState::Idle || !m_listFile)
        return;

    m_listFile->seekToFirstSection();
    m_bytesRead = 0;
    m_totalBytes = m_listFile->size();
    m_stats.listFileTotalBytes = m_listFile->size();
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

    logMessage(QString("Starting replay from %1").arg(m_listFile->getFileName()));

    const auto &lfc = listfile_constants(m_listFile->getFileVersion());

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
                QMutexLocker lock(&m_freeBuffers->mutex);
                while (m_freeBuffers->queue.isEmpty() && m_desiredState == DAQState::Running)
                {
                    m_freeBuffers->wc.wait(&m_freeBuffers->mutex, FreeBufferWaitTimeout_ms);
                }

                if (!m_freeBuffers->queue.isEmpty())
                {
                    buffer = m_freeBuffers->queue.dequeue();
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
                        isBufferValid = m_listFile->readNextSection(buffer);

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
                    s32 sectionsRead = m_listFile->readSectionsIntoBuffer(buffer);
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
                    QMutexLocker lock(&m_freeBuffers->mutex);
                    m_freeBuffers->queue.enqueue(buffer);

                    setState(DAQState::Stopping);
                }
                else
                {
                    if (m_logBuffers && m_logger)
                    {
                        logMessage(">>> Begin buffer");
                        BufferIterator bufferIter(buffer->data, buffer->used, BufferIterator::Align32);
                        logBuffer(bufferIter, [this](const QString &str) { this->logMessage(str); });
                        logMessage("<<< End buffer");
                    }
                    // Push the valid buffer onto the output queue.
                    m_fullBuffers->mutex.lock();
                    m_fullBuffers->queue.enqueue(buffer);
                    m_fullBuffers->mutex.unlock();
                    m_fullBuffers->wc.wakeOne();
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

    QCoreApplication::processEvents();
}

void MVMEListfileWorker::logMessage(const QString &str)
{
    if (m_logger)
        m_logger(str);
}


