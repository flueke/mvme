#include "mvlc_readout.h"

#include <atomic>


namespace mesytec
{
namespace mvlc
{

struct ReadoutWorker::Private
{
    std::atomic<ReadoutWorker::State> state;
    std::atomic<ReadoutWorker::State> desiredState;
    MVLC mvlc;
    std::vector<StackTrigger> stackTriggers;
};

ReadoutWorker::ReadoutWorker(MVLC &mvlc, const std::vector<StackTrigger> &stackTriggers)
    : d(std::make_unique<Private>())
{
    d->state = State::Idle;
    d->desiredState = State::Idle;
    d->mvlc = mvlc;
    d->stackTriggers = stackTriggers;
}

ReadoutWorker::State ReadoutWorker::state() const
{
    return d->state;
}

std::error_code ReadoutWorker::start(const std::chrono::seconds &duration)
{
}

std::error_code ReadoutWorker::stop()
{
}

std::error_code ReadoutWorker::pause()
{
}

std::error_code ReadoutWorker::resume()
{
}

} // end namespace mvlc
} // end namespace mesytec

