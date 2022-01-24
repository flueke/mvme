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
#include <QCoreApplication>
#include <QDebug>
#include <QThread>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <stdexcept>

#include "mvme_mvlc_listfile.h"
#include "mvlc/mvlc_util.h"
#include "util_zip.h"

using namespace mesytec;

namespace
{
DAQState replay_worker_state_to_daq_state(const mvlc::ReplayWorker::State &state)
{
    switch (state)
    {
        case mvlc::ReplayWorker::State::Idle:
            return DAQState::Idle;
        case mvlc::ReplayWorker::State::Starting:
            return DAQState::Starting;
        case mvlc::ReplayWorker::State::Running:
            return DAQState::Running;
        case mvlc::ReplayWorker::State::Paused:
            return DAQState::Paused;
        case mvlc::ReplayWorker::State::Stopping:
            return DAQState::Stopping;

        InvalidDefaultCase
    }

    return {};
}

} // end anon namespace

struct MVLCListfileWorker::Private
{
    MVLCListfileWorker *q = nullptr;
    mvlc::Protected<DAQStats> stats;
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

    explicit Private(MVLCListfileWorker *q_)
        : q(q_)
        , stats()
        , state(DAQState::Idle)
        , desiredState(DAQState::Idle)
    {}

    void updateDAQStats()
    {
        auto counters = mvlcReplayWorker->counters();

        auto daqStats = stats.access();
        daqStats->totalBytesRead = counters.bytesRead;
        daqStats->totalBuffersRead = counters.buffersRead;
        daqStats->buffersFlushed = counters.buffersFlushed;
    }

    template<typename Cond>
    void waitForStateChange(Cond cond)
    {
        while (cond(mvlcReplayWorker->state()))
        {
            mvlcReplayWorker->waitableState().wait_for(
                std::chrono::milliseconds(20),
                [cond] (const mvlc::ReplayWorker::State &state)
                {
                    return !cond(state);
                });
            QCoreApplication::processEvents();
        }
        q->setState(replay_worker_state_to_daq_state(mvlcReplayWorker->state()));
    }
};

MVLCListfileWorker::MVLCListfileWorker(QObject *parent)
    : ListfileReplayWorker(parent)
    , d(std::make_unique<Private>(this))
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
        d->stats.access()->listfileFilename = inZipFile->getZipName();
}

DAQStats MVLCListfileWorker::getStats() const
{
    return d->stats.copy();
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
    d->stats.access()->start();

    try
    {

        logMessage(QString("Starting replay from %1:%2")
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

        setState(DAQState::Running);

        // wait until readout done while periodically updating the DAQ stats
        while (d->mvlcReplayWorker->state() != mvlc::ReplayWorker::State::Idle)
        {
            d->mvlcReplayWorker->waitableState().wait_for(
                std::chrono::milliseconds(100),
                [] (const mvlc::ReplayWorker::State &state)
                {
                    return state == mvlc::ReplayWorker::State::Idle;
                });

            d->updateDAQStats();
        }
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
        logError(QSL("VME Script parse error: ") + e.toString());
    }

    d->stats.access()->stop();
    setState(DAQState::Idle);
}

using namespace mesytec::mvme_mvlc;

void MVLCListfileWorker::stop()
{
    if (auto ec = d->mvlcReplayWorker->stop())
        logError(ec.message().c_str());
    else
    {
        auto cond = [](const mvlc::ReplayWorker::State &state)
        {
            return state != mvlc::ReplayWorker::State::Idle;
        };

        d->waitForStateChange(cond);
        logMessage(QString(QSL("MVLC readout stopped")));
    }
}

void MVLCListfileWorker::pause()
{
    if (auto ec = d->mvlcReplayWorker->pause())
        logError(ec.message().c_str());
    else
    {
        auto cond = [](const mvlc::ReplayWorker::State &state)
        {
            return state == mvlc::ReplayWorker::State::Running;
        };

        d->waitForStateChange(cond);
        logMessage(QString(QSL("MVLC readout paused")));
    }
}

void MVLCListfileWorker::resume()
{
    if (auto ec = d->mvlcReplayWorker->resume())
        logError(ec.message().c_str());
    else
    {
        auto cond = [](const mvlc::ReplayWorker::State &state)
        {
            return state == mvlc::ReplayWorker::State::Paused;
        };

        d->waitForStateChange(cond);
        logMessage(QString(QSL("MVLC readout resumed")));
    }
}

void MVLCListfileWorker::logError(const QString &msg)
{
    logMessage(QSL("MVLC Replay Error: %1").arg(msg));
}
