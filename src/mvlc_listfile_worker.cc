#include "mvlc_listfile_worker.h"

#include "util_zip.h"
#include <QDebug>

struct MVLCListfileWorker::Private
{
    DAQStats stats;
    DAQState state;
    std::atomic<DAQState> desiredState;
    u32 eventsToRead = 0;
    bool logBuffers = false;
    QIODevice *input = nullptr;
    ListfileBufferFormat format;
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

void MVLCListfileWorker::setListfile(QIODevice *input, ListfileBufferFormat format)
{
    assert(format == ListfileBufferFormat::MVLC_ETH
           || format == ListfileBufferFormat::MVLC_USB);

    d->input = input;
    d->format = format;
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

    logMessage(QString("Starting replay from %1").arg(
            get_filename(d->input)));

    seek_in_file(d->input, 0);
    d->stats.start();
    d->stats.listFileTotalBytes = d->input->size();

    setState(DAQState::Running);

    switch (d->format)
    {
        case ListfileBufferFormat::MVLC_ETH:
            mainloop_eth();
            break;

        case ListfileBufferFormat::MVLC_USB:
            mainloop_usb();
            break;

        InvalidDefaultCase;
    }

    d->stats.stop();
    setState(DAQState::Idle);
}

void MVLCListfileWorker::mainloop_eth()
{
}

void MVLCListfileWorker::mainloop_usb()
{
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
