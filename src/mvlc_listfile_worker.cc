#include "mvlc_listfile_worker.h"

#include "util_zip.h"

struct MVLCListfileWorker::Private
{
    DAQStats stats;
    DAQState state;
    std::atomic<DAQState> desiredState;
    u32 eventsToRead = 0;
    bool logBuffers = false;
    QIODevice *input = nullptr;
};

MVLCListfileWorker::MVLCListfileWorker(
    ThreadSafeDataBufferQueue *emptyBufferQueue,
    ThreadSafeDataBufferQueue *filledBufferQueue,
    QObject *parent)
    : ListfileReplayWorker(emptyBufferQueue, filledBufferQueue, parent)
    , d(std::make_unique<Private>())
{
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

// Blocking call which will perform the work
void MVLCListfileWorker::start()
{
    Q_ASSERT(getEmptyQueue());
    Q_ASSERT(getFilledQueue());
    Q_ASSERT(d->state == DAQState::Idle);

    if (d->state != DAQState::Idle || !d->input)
        return;

    seek_in_file(d->input, 0);
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
