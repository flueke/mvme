#include "mvlc_listfile_worker.h"

struct MVLCListfileWorker::Private
{
    DAQStats stats;
    DAQState state;
    std::atomic<DAQState> desiredState;
};

MVLCListfileWorker::MVLCListfileWorker(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

MVLCListfileWorker::~MVLCListfileWorker()
{
}

void MVLCListfileWorker::setListfile(std::unique_ptr<QIODevice> listfile)
{
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

void MVLCListfileWorker::setState(DAQState state)
{
}

// Blocking call which will perform the work
void MVLCListfileWorker::start()
{
}

// Thread-safe calls, setting internal flags to do the state transition
void MVLCListfileWorker::stop()
{
}

void MVLCListfileWorker::pause()
{
}

void MVLCListfileWorker::resume()
{
}
