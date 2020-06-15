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
#include "mvlc_readout_worker.h"

#include <cassert>
#include <chrono>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <thread>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>

#include "mvlc_daq.h"
#include "mvlc_listfile.h"
#include "mvlc/mvlc_qt_object.h"
#include "mvlc/mvlc_util.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "util_zip.h"
#include "vme_analysis_common.h"

// =========================
//    MVLC readout outline
// =========================
//
// * Two different formats depending on connection type.
// * Pass only complete frames around. For readout the detection has to be done
//   anyways so that system frames can be properly inserted.
// * Do not try to hit exactly 1s between SoftwareTimeticks. This will
//   complicate the code a lot and is not really needed if some form of timestamp
//   and/or duration is stored in the timetick event.
//
//
// ETH
// -------------------------
// Small packets of 1500 or 8192 bytes. Two header words for packet loss detection
// and handling (resume processing after loss).
//
// - Strategy
//
//   1) start with a fresh buffer
//
//   2) while free space in buffer > 8k:
//     read packet and append to buffer
//     if (flush timeout elapsed)
//         flush buffer
//     if (time for timetick)
//         insert timetick frame
//
//   3) flush buffer
//
// => Inserting system frames is allowed at any point.
//
// - Replay from file:
//   Read any amount of data from file into memory. If a word is not a system
//   frame then it must be header0() of a previously received packet. Follow
//   the header framing via the header0::NumDataWords value. This way either
//   end up on the next header0() or at the start of a system frame.
//   If part of a packet is at the end of the buffer read from disk store the part
//   temporarily and truncate the buffer. Then when doing the next read add the
//   partial packet to the front of the new buffer.
//   -> Packet boundaries can be restored and it can be guaranteed that only full
//   packets worth of data are passed internally.
//
//
// USB
// -------------------------
// Stream of data. Reads do not coincide with buffer framing. The exception is the
// very first read which starts with an 0xF3 frame.
// To be able to insert system frames (e.g. timeticks) and to make the analysis
// easier to write, internal buffers must contain complete frames only. To make
// this work the readout code has to follow the 0xF3 data framing. Extract the
// length to be able to jump to the next frame start. Store partial data at the
// end and truncate the buffer before flushing it.
//
// - Replay:
//   Starts with a system or a readout frame. Follow frame structure doing
//   truncation and copy of partial frames.
//
// Note: max amount to copy is the max length of a frame. That's 2^13 words
// (32k bytes) for readout frames.

using namespace mesytec;
using namespace mesytec::mvlc;
using namespace mesytec::mvme_mvlc;

static const size_t ReadBufferSize = Megabytes(1);

namespace
{

DAQState readout_worker_state_to_daq_state(const mvlc::ReadoutWorker::State &state)
{
    switch (state)
    {
        case mvlc::ReadoutWorker::State::Idle:
            return DAQState::Idle;
        case mvlc::ReadoutWorker::State::Starting:
            return DAQState::Starting;
        case mvlc::ReadoutWorker::State::Running:
            return DAQState::Running;
        case mvlc::ReadoutWorker::State::Paused:
            return DAQState::Paused;
        case mvlc::ReadoutWorker::State::Stopping:
            return DAQState::Stopping;

        InvalidDefaultCase
    }

    return {};
}

} // end anon namespace

struct MVLCReadoutWorker::Private
{
    enum class DebugInfoRequest
    {
        None,
        OnNextBuffer,
        OnNextError,
    };

    MVLCReadoutWorker *q = nullptr;;

    std::unique_ptr<mesytec::mvlc::ReadoutWorker> mvlcReadoutWorker;
    std::unique_ptr<mesytec::mvlc::listfile::ZipCreator> mvlcZipCreator;
    mvlc::ReadoutBufferQueues *snoopQueues = nullptr;

    // lots of mvlc api layers
    MVLC_VMEController *mvlcCtrl = nullptr;;
    MVLCObject *mvlcObj = nullptr;;

    u32 nextOutputBufferNumber = 1u;;

    std::atomic<DAQState> state;
    std::atomic<DAQState> desiredState;

    // stat counters
    mutable mesytec::mvlc::TicketMutex countersMutex;

    // Threaded listfile writing
    static const size_t ListfileWriterBufferCount = 16u;
    static const size_t ListfileWriterBufferSize  = ReadBufferSize;
    std::atomic<DebugInfoRequest> debugInfoRequest;

    Private(MVLCReadoutWorker *q_)
        : q(q_)
        , state(DAQState::Idle)
        , desiredState(DAQState::Idle)
        , debugInfoRequest(DebugInfoRequest::None)
    {}

    struct EventWithModules
    {
        EventConfig *event;
        QVector<ModuleConfig *> modules;
        QVector<u8> moduleTypes;
    };

    QVector<EventWithModules> events;

    void updateDAQStats()
    {
        auto rdoCounters = mvlcReadoutWorker->counters();

        auto &daqStats = q->getContext().daqStats;
        daqStats.totalBytesRead = rdoCounters.bytesRead;
        daqStats.totalBuffersRead = rdoCounters.buffersRead;
        daqStats.buffersWithErrors = rdoCounters.usbFramingErrors + rdoCounters.ethShortReads;
        daqStats.droppedBuffers = rdoCounters.snoopMissedBuffers;
        daqStats.listFileBytesWritten = rdoCounters.listfileWriterCounters.bytesWritten;
    }
};

MVLCReadoutWorker::MVLCReadoutWorker(QObject *parent)
    : VMEReadoutWorker(parent)
    , d(std::make_unique<Private>(this))
{
    qRegisterMetaType<mesytec::mvlc::ReadoutBuffer>(
        "mesytec::mvlc::ReadoutBuffer");
}

MVLCReadoutWorker::~MVLCReadoutWorker()
{
}

// Setup the Private struct members. All layers of the MVLC impl are used
// in here: MVLC_VMEController to execute vme scripts, MVLCObject to setup
// stacks and triggers and the low level implementations for fast ethernet
// and usb reads.
void MVLCReadoutWorker::setMVLCObjects()
{
    d->mvlcCtrl = qobject_cast<MVLC_VMEController *>(getContext().controller);

    if (d->mvlcCtrl)
        d->mvlcObj = d->mvlcCtrl->getMVLCObject();
}

void MVLCReadoutWorker::start(quint32 cycles)
{
    if (d->state != DAQState::Idle)
    {
        logMessage("Readout state != Idle, aborting startup");
        return;
    }

    setMVLCObjects();

    if (!d->mvlcCtrl)
    {
        logMessage("MVLC controller required");
        InvalidCodePath;
        return;
    }

    assert(cycles == 0 || !"mvlc_readout_worker does not support running a limited number of cycles");
    assert(d->snoopQueues);

    try
    {
        auto logger = [this](const QString &msg)
        {
            this->logMessage(msg);
        };

        setState(DAQState::Starting);

        // Run the standard VME DAQ init sequence
        if (!this->do_VME_DAQ_Init(d->mvlcCtrl))
        {
            setState(DAQState::Idle);
            return;
        }

        logMessage("");

        // Stack and trigger io setup. The only thing left to do for the MVLC
        // to produce data is to enable the stack triggers.
        if (auto ec = setup_mvlc(*d->mvlcObj, *getContext().vmeConfig, logger))
            throw ec;

        logMessage("  Enabling triggers");

        std::vector<u32> triggers;
        std::error_code ec;

        std::tie(triggers, ec) = get_trigger_values(
            *getContext().vmeConfig, logger);

        if (ec)
            throw ec;

        mvlc::listfile::WriteHandle *listfileWriteHandle = nullptr;
        QString listfileArchiveName;

        if (m_workerContext.listfileOutputInfo->enabled)
        {
            auto outInfo = m_workerContext.listfileOutputInfo;

            if (outInfo->fullDirectory.isEmpty())
                throw std::runtime_error("Error: listfile output directory is not set");

            listfileArchiveName = make_new_listfile_name(outInfo);
            auto memberName = QFileInfo(listfileArchiveName).completeBaseName();
            memberName += QSL(".mvlclst");

            if (outInfo->format != ListFileFormat::ZIP
                && outInfo->format != ListFileFormat::LZ4)
            {
                throw std::runtime_error("Unsupported listfile format");
            }

            logMessage("");
            logger(QString("Writing listfile into %1").arg(listfileArchiveName));
            logMessage("");

            d->mvlcZipCreator = std::make_unique<mvlc::listfile::ZipCreator>();
            d->mvlcZipCreator->createArchive(listfileArchiveName.toStdString());

            if (outInfo->format == ListFileFormat::ZIP)
            {
                listfileWriteHandle = d->mvlcZipCreator->createZIPEntry(
                    memberName.toStdString(), outInfo->compressionLevel);
            }
            else if (outInfo->format == ListFileFormat::LZ4)
            {
                listfileWriteHandle = d->mvlcZipCreator->createLZ4Entry(
                    memberName.toStdString(), outInfo->compressionLevel);
            }

            assert(listfileWriteHandle);

            if (listfileWriteHandle)
            {
                mvlc::listfile::listfile_write_preamble(
                    *listfileWriteHandle,
                    mesytec::mvme::vmeconfig_to_crateconfig(getContext().vmeConfig));

                mvme_mvlc_listfile::listfile_write_mvme_config(
                    *listfileWriteHandle,
                    *getContext().vmeConfig);
            }
        }

        d->mvlcReadoutWorker = std::make_unique<mvlc::ReadoutWorker>(
            d->mvlcCtrl->getMVLC(),
            triggers,
            *d->snoopQueues,
            listfileWriteHandle);

        auto fStart = d->mvlcReadoutWorker->start();

        if (auto ec = fStart.get())
            throw ec;

        logMessage("Entering readout loop");

        setState(DAQState::Running);

        m_workerContext.daqStats.start();
        m_workerContext.daqStats.listfileFilename = listfileArchiveName;

        // wait until readout done while periodically updating the DAQ stats
        while (d->mvlcReadoutWorker->state() != ReadoutWorker::State::Idle)
        {
            d->mvlcReadoutWorker->waitableState().wait_for(
                std::chrono::milliseconds(100),
                [] (const ReadoutWorker::State &state)
                {
                    return state == ReadoutWorker::State::Idle;
                });

            d->updateDAQStats();
        }

        logMessage("Leaving readout loop");
        logMessage("");

        vme_daq_shutdown(getContext().vmeConfig, d->mvlcCtrl, logger);
        m_workerContext.daqStats.stop();

        // add the log buffer and the analysis configs to the listfile archive
        if (d->mvlcZipCreator && d->mvlcZipCreator->isOpen())
        {
            if (d->mvlcZipCreator->hasOpenEntry())
                d->mvlcZipCreator->closeCurrentEntry();

            if (auto writeHandle = d->mvlcZipCreator->createZIPEntry("messages.log", 0))
            {
                auto messages = m_workerContext.getLogBuffer().join('\n');
                auto bytes = messages.toUtf8();
                writeHandle->write(reinterpret_cast<const u8 *>(bytes.data()), bytes.size());
                d->mvlcZipCreator->closeCurrentEntry();
            }

            if (auto writeHandle = d->mvlcZipCreator->createZIPEntry("analysis.analysis", 0))
            {
                auto bytes = m_workerContext.getAnalysisJson().toJson();
                writeHandle->write(reinterpret_cast<const u8 *>(bytes.data()), bytes.size());
                d->mvlcZipCreator->closeCurrentEntry();
            }

            if (auto writeHandle = d->mvlcZipCreator->createZIPEntry("mvme_run_notes.txt", 0))
            {
                auto bytes = m_workerContext.getRunNotes().toLocal8Bit();
                writeHandle->write(reinterpret_cast<const u8 *>(bytes.data()), bytes.size());
                d->mvlcZipCreator->closeCurrentEntry();
            }
        }

        d->mvlcZipCreator.reset(); // destroy the ZipCreator to flush and close the listfile archive

        // Rethrow any exception recorded by the mvlc::ReadoutWorker.
        if (auto eptr = d->mvlcReadoutWorker->counters().eptr)
            std::rethrow_exception(eptr);
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
    catch (...)
    {
        logError("Unknown exception during readout");
    }

    // Reset object pointers to ensure we don't accidentially work with stale
    // pointers on next startup.
    d->mvlcCtrl = nullptr;
    d->mvlcObj = nullptr;

    setState(DAQState::Idle);
}

void MVLCReadoutWorker::stop()
{
    if (auto ec = d->mvlcReadoutWorker->stop())
        logError(ec.message().c_str());
    else
    {
        while (d->mvlcReadoutWorker->state() != ReadoutWorker::State::Idle)
        {
            d->mvlcReadoutWorker->waitableState().wait_for(
                std::chrono::milliseconds(20),
                [] (const ReadoutWorker::State &state)
                {
                    return state == ReadoutWorker::State::Idle;
                });
            QCoreApplication::processEvents();
        }
        setState(readout_worker_state_to_daq_state(d->mvlcReadoutWorker->state()));
        logMessage(QString(QSL("MVLC readout stopped")));
    }
}

void MVLCReadoutWorker::pause()
{
    if (auto ec = d->mvlcReadoutWorker->pause())
        logError(ec.message().c_str());
    else
    {
        while (d->mvlcReadoutWorker->state() == ReadoutWorker::State::Running)
        {
            d->mvlcReadoutWorker->waitableState().wait_for(
                std::chrono::milliseconds(20),
                [] (const ReadoutWorker::State &state)
                {
                    return state != ReadoutWorker::State::Running;
                });
            QCoreApplication::processEvents();
        }
        setState(readout_worker_state_to_daq_state(d->mvlcReadoutWorker->state()));
        logMessage(QString(QSL("MVLC readout paused")));
    }
}

void MVLCReadoutWorker::resume(quint32 cycles)
{
    assert(cycles == 0 || !"mvlc_readout_worker does not support running a limited number of cycles");

    if (auto ec = d->mvlcReadoutWorker->resume())
        logError(ec.message().c_str());
    else
    {
        while (d->mvlcReadoutWorker->state() == ReadoutWorker::State::Paused)
        {
            d->mvlcReadoutWorker->waitableState().wait_for(
                std::chrono::milliseconds(20),
                [] (const ReadoutWorker::State &state)
                {
                    return state != ReadoutWorker::State::Paused;
                });
            QCoreApplication::processEvents();
        }
        setState(readout_worker_state_to_daq_state(d->mvlcReadoutWorker->state()));
        logMessage(QString(QSL("MVLC readout resumed")));
    }
}

bool MVLCReadoutWorker::isRunning() const
{
    return d->state != DAQState::Idle;
}

void MVLCReadoutWorker::setState(const DAQState &state)
{
    qDebug() << __PRETTY_FUNCTION__ << DAQStateStrings[d->state] << "->" << DAQStateStrings[state];
    d->state = state;
    d->desiredState = state;
    emit stateChanged(state);

    switch (state)
    {
        case DAQState::Idle:
            qDebug() << __PRETTY_FUNCTION__ << "emit daqStopped()";
            emit daqStopped();
            break;

        case DAQState::Paused:
            qDebug() << __PRETTY_FUNCTION__ << "emit daqPaused()";
            emit daqPaused();
            break;

        case DAQState::Starting:
        case DAQState::Stopping:
            break;

        case DAQState::Running:
            qDebug() << __PRETTY_FUNCTION__ << "emit daqStarted()";
            emit daqStarted();
    }

    QCoreApplication::processEvents();
}

DAQState MVLCReadoutWorker::getState() const
{
    return d->state;
}

mesytec::mvlc::ReadoutWorker::Counters MVLCReadoutWorker::getReadoutCounters() const
{
    return (d->mvlcReadoutWorker
            ? d->mvlcReadoutWorker->counters()
            : mesytec::mvlc::ReadoutWorker::Counters{});
}

MVLC_VMEController *MVLCReadoutWorker::getMVLC()
{
    return d->mvlcCtrl;
}

void MVLCReadoutWorker::setSnoopQueues(mesytec::mvlc::ReadoutBufferQueues *queues)
{
    d->snoopQueues = queues;
}

void MVLCReadoutWorker::requestDebugInfoOnNextBuffer()
{
    d->debugInfoRequest = Private::DebugInfoRequest::OnNextBuffer;
}

void MVLCReadoutWorker::requestDebugInfoOnNextError()
{
    d->debugInfoRequest = Private::DebugInfoRequest::OnNextError;
}

void MVLCReadoutWorker::logError(const QString &msg)
{
    logMessage(QSL("MVLC Readout Error: %1").arg(msg));
}
