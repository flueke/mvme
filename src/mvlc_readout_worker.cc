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
#include "mvlc_readout_worker.h"

#include <boost/range/adaptor/indexed.hpp>
#include <cassert>
#include <chrono>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <thread>
#include <sstream>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>

#include "mesytec-mvlc/mvlc_constants.h"
#include "mvlc_daq.h"
#include "mvme_mvlc_listfile.h"
#include "mvlc/mvlc_util.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "util/strings.h"
#include "util_zip.h"
#include "vme_analysis_common.h"

using boost::adaptors::indexed;

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

namespace mesytec::mvme
{

static const size_t ReadBufferSize = ::Megabytes(1);

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
    std::unique_ptr<mesytec::mvlc::listfile::SplitZipCreator> mvlcZipCreator;
    std::shared_ptr<mesytec::mvlc::listfile::WriteHandle> listfileWriteHandle;
    mvlc::ReadoutBufferQueues *snoopQueues = nullptr;

    // lots of mvlc api layers
    MVLC_VMEController *mvlcCtrl = nullptr;;
    MVLCObject *mvlcObj = nullptr;;

    u32 nextOutputBufferNumber = 1u;;
    std::vector<u32> stackTriggers; // XXX: Updated in daqStartSequence()

    std::atomic<DAQState> state;
    std::atomic<DAQState> desiredState;

    // stat counters
    mutable mesytec::mvlc::TicketMutex countersMutex;

    // Threaded listfile writing
    static const size_t ListfileWriterBufferCount = 16u;
    static const size_t ListfileWriterBufferSize  = ReadBufferSize;
    std::atomic<DebugInfoRequest> debugInfoRequest;

    explicit Private(MVLCReadoutWorker *q_)
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

    bool daqStartSequence();
};

bool MVLCReadoutWorker::Private::daqStartSequence()
{
    auto logger = [this] (const QString &msg)
    {
        q->logMessage(msg);
    };

    auto error_logger = [this] (const QString &msg)
    {
        q->getContext().errorLogger(msg);
    };

    auto &vmeConfig = *q->getContext().vmeConfig;

    // Create MVLC stack trigger values from the VMEConfig and store in a
    // member variable.
    std::vector<u32> triggers;
    std::error_code ec;
    std::tie(triggers, ec) = get_trigger_values(vmeConfig, logger);

    if (ec)
    {
        logger(QString("MVLC Stack Trigger setup error: %1").arg(ec.message().c_str()));
        return false;
    }

    this->stackTriggers = triggers;

    // Run the init sequence built from the VMEConfig.
    return run_daq_start_sequence(
        mvlcCtrl,
        *q->getContext().vmeConfig,
        q->getContext().runInfo.ignoreStartupErrors,
        logger,
        error_logger);
}


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
    // Updates the local DAQ state and emits signals to propagate the state
    // change to the outside. By doing this from start() only and not from
    // stop/pause/resume the signals will be emitted from the readout worker
    // thread and queued up for other threads instead of being directly invoked
    // inside the thread calling stop/pause/resume. This matches the behaviour
    // of the readout workers for the other controllers and limits transitions
    // to times where the Qt eventloop is running.
    auto set_daq_state = [this] (const DAQState &state)
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
                break;
        }

        QCoreApplication::processEvents();
    };

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

    assert(cycles == 0 || !"mvlc_readout_worker does not support running a limited number of cycles"); (void) cycles;
    assert(d->snoopQueues);

    try
    {
        auto logger = [this](const QString &msg)
        {
            this->logMessage(msg);
        };

        auto errorLogger = [this] (const QString &msg)
        {
            this->logError(msg);
        };

        set_daq_state(DAQState::Starting);

        logMessage(QSL("Using VME Controller %1 (%2)")
                   .arg(d->mvlcCtrl->getIdentifyingString())
                   .arg(d->mvlcObj->getConnectionInfo()));

        if (!d->daqStartSequence())
        {
            set_daq_state(DAQState::Idle);
            return;
        }

        const auto vmeConfig = getContext().vmeConfig;

        // mesytec-mvlc CrateConfig
        auto crateConfig = mesytec::mvme::vmeconfig_to_crateconfig(vmeConfig);

        // listfile handling
        d->listfileWriteHandle = {};

        if (m_workerContext.listfileOutputInfo.enabled)
        {
            // Create the listfile preamble in a buffer and store it for later
            // use.
            std::vector<u8> preamble;

            {
                listfile::BufferedWriteHandle bwh;

                // Standard listfile preamble including a mesytec-mvlc CrateConfig
                // generated from our VMEConfig.
                listfile::listfile_write_preamble(bwh, crateConfig);

                // Write our VMEConfig to the listfile aswell (the CrateConfig from
                // the library does not have all the meta information stored in the
                // VMEConfig).
                mvme_mvlc_listfile::listfile_write_mvme_config(bwh, *vmeConfig);

                preamble = bwh.getBuffer();
            }

            auto &outInfo = m_workerContext.listfileOutputInfo;

            if (outInfo.format == ListFileFormat::ZIP
                || outInfo.format == ListFileFormat::LZ4)
            {
                //if (outInfo.fullDirectory.isEmpty())
                //    throw std::runtime_error("Error: listfile output directory is not set");

                listfile::SplitListfileSetup lfSetup;
                lfSetup.entryType = (outInfo.format == ListFileFormat::ZIP
                                     ? listfile::ZipEntryInfo::ZIP
                                     : listfile::ZipEntryInfo::LZ4);
                lfSetup.compressLevel = outInfo.compressionLevel;

                if (outInfo.flags & ListFileOutputInfo::SplitBySize)
                    lfSetup.splitMode = listfile::ZipSplitMode::SplitBySize;
                else if (outInfo.flags & ListFileOutputInfo::SplitByTime)
                    lfSetup.splitMode = listfile::ZipSplitMode::SplitByTime;

                lfSetup.splitSize = outInfo.splitSize;
                lfSetup.splitTime = outInfo.splitTime;

                QFileInfo lfInfo(make_new_listfile_name(&outInfo));
                auto lfDir = lfInfo.path();
                auto lfBase = lfInfo.completeBaseName();
                auto lfPrefix = lfDir + "/" + lfBase;

                lfSetup.filenamePrefix = lfPrefix.toStdString();
                lfSetup.preamble = preamble;


                // Set the openArchiveCallback
                lfSetup.openArchiveCallback = [this] (listfile::SplitZipCreator *zipCreator)
                {
                    // Update daqStats here so that the GUI displays the current filename.
                    // FIXME: thread safety! horrible design!
                    m_workerContext.daqStats.listfileFilename =
                        QString::fromStdString(zipCreator->archiveName());
                };

                // Set the closeArchiveCallback
                lfSetup.closeArchiveCallback = [this] (listfile::SplitZipCreator *zipCreator)
                {
                    assert(zipCreator->isOpen());
                    assert(!zipCreator->hasOpenEntry());

                    // Add the log buffer, analysis config and other files to the
                    // archive.

                    auto do_write = [zipCreator] (const std::string &filename, const QByteArray &data)
                    {
                        add_file_to_archive(zipCreator, filename, data);
                    };

                    do_write("messages.log", m_workerContext.getLogBuffer().join('\n').toUtf8());
                    // FIXME: thread safety for m_workerContext.getAnalysisJson()!
                    do_write("analysis.analysis", m_workerContext.getAnalysisJson().toJson());
                    do_write("mvme_run_notes.txt", m_workerContext.getRunNotes().toLocal8Bit());
                };

                d->mvlcZipCreator = std::make_unique<mvlc::listfile::SplitZipCreator>();
                d->mvlcZipCreator->createArchive(lfSetup);

                logMessage("");
                logger(QString("Writing listfile into %1").arg(m_workerContext.daqStats.listfileFilename));
                logMessage("");

                // This call writes out the preamble. The same happens if listfile splitting is
                // enabled and a new archive is started by the SplitZipCreator.
                d->listfileWriteHandle = std::shared_ptr<mvlc::listfile::WriteHandle>(
                    d->mvlcZipCreator->createListfileEntry());
            }
#ifdef MVLC_HAVE_ZMQ
            else if (outInfo.format == ListFileFormat::ZMQ_Ganil)
            {
                d->listfileWriteHandle = std::make_shared<mvlc::listfile::ZmqGanilWriteHandle>();
                // Note: intentionally not sending the listfile preamble as the GANIL receiver
                // code does not expect it.
            }
#endif
            else
            {
                throw std::runtime_error("Unsupported listfile format");
            }
        }

        // mesytec-mvlc readout worker
        d->mvlcReadoutWorker = std::make_unique<mvlc::ReadoutWorker>(
            d->mvlcCtrl->getMVLC(),
            d->stackTriggers,
            *d->snoopQueues,
            d->listfileWriteHandle);

        d->mvlcReadoutWorker->setMcstDaqStartCommands(crateConfig.mcstDaqStart);
        d->mvlcReadoutWorker->setMcstDaqStopCommands(crateConfig.mcstDaqStop);

        auto fStart = d->mvlcReadoutWorker->start();

        if (auto ec = fStart.get())
            throw ec;

        logMessage("");
        logMessage("Entering readout loop");

        set_daq_state(DAQState::Running);

        m_workerContext.daqStats.start();

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

            auto daqState = readout_worker_state_to_daq_state(d->mvlcReadoutWorker->state());

            if (d->state != daqState)
                set_daq_state(daqState);
        }

        if (d->listfileWriteHandle)
            listfile_write_system_event(*d->listfileWriteHandle, system_event::subtype::EndOfFile);

        logMessage("Leaving readout loop");
        logMessage("");

        mvlc_daq_shutdown(vmeConfig, d->mvlcCtrl, logger, errorLogger);
        m_workerContext.daqStats.stop();

        d->mvlcZipCreator.reset(); // destroy the ZipCreator to flush and close the listfile archive

        // In case we recorded a listfile and the run number was used increment
        // the run number here so that it represents the _next_ run number.
        if (m_workerContext.listfileOutputInfo.enabled
            && (m_workerContext.listfileOutputInfo.flags & ListFileOutputInfo::UseRunNumber))
        {
            ++m_workerContext.listfileOutputInfo.runNumber;
        }

        set_daq_state(readout_worker_state_to_daq_state(d->mvlcReadoutWorker->state()));

        auto stackErrors = d->mvlcCtrl->getMVLC().getStackErrorCounters();

        for (const auto &kv: stackErrors.stackErrors | indexed(0))
        {
            unsigned stackId = kv.index();
            const auto &counts = kv.value();

            if (counts.empty())
                continue;

            for (auto it = counts.begin(); it != counts.end(); ++it)
            {
                const auto &errorInfo = it->first;
                const auto &count = it->second;


                logMessage(QSL("MVLC stack errors: stackId=%1, stackLine=%2, flags=%3, count=%4")
                         .arg(stackId)
                         .arg(errorInfo.line)
                         .arg(format_frame_flags(errorInfo.flags).c_str())
                         .arg(count));
            }
        }

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
        logError(QSL("VME Script parse error: ") + e.toString());
    }
    catch (...)
    {
        logError("Unknown exception during readout");
    }

    // Reset object pointers to ensure we don't accidentially work with stale
    // pointers on next startup.
    d->mvlcCtrl = nullptr;
    d->mvlcObj = nullptr;

    set_daq_state(DAQState::Idle);
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
        logMessage(QString(QSL("MVLC readout stopped")));
    }

    // delete the listfileWriteHandle
    d->listfileWriteHandle = {};
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
        logMessage(QString(QSL("MVLC readout paused")));
    }
}

void MVLCReadoutWorker::resume(quint32 cycles)
{
    assert(cycles == 0 || !"mvlc_readout_worker does not support running a limited number of cycles"); (void) cycles;

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
        logMessage(QString(QSL("MVLC readout resumed")));
    }
}

bool MVLCReadoutWorker::isRunning() const
{
    return d->state != DAQState::Idle;
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
    getContext().errorLogger(QSL("MVLC Readout Error: %1").arg(msg));
}

bool run_daq_start_sequence(
    MVLC_VMEController *mvlcCtrl,
    VMEConfig &vmeConfig,
    bool ignoreStartupErrors,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> error_logger)
{
    auto run_init_func = [=, &vmeConfig] (auto initFunc, const QString &partTitle)
    {
        vme_script::run_script_options::Flag opts = 0u;
        if (!ignoreStartupErrors)
            opts = vme_script::run_script_options::AbortOnError;

        auto initResults = initFunc(&vmeConfig, mvlcCtrl, logger, error_logger, opts);

        if (!ignoreStartupErrors && has_errors(initResults))
        {
            logger("");
            logger(partTitle + " Errors:");
            auto logger_ = [=] (const QString &msg) { logger("  " + msg); };
            log_errors(initResults, logger_);
            return false;
        }

        return true;
    };


    auto &mvlc = *mvlcCtrl->getMVLCObject();

    logger("");
    logger("Initializing MVLC");

    // Clear triggers and stacks =========================================================

    logger("  Disabling triggers");

    if (auto ec = disable_all_triggers_and_daq_mode(mvlc))
    {
        logger(QString("Error disabling readout triggers: %1")
               .arg(ec.message().c_str()));
        return false;
    }

    logger("  Resetting stack offsets");

    if (auto ec = reset_stack_offsets(mvlc))
    {
        logger(QString("Error resetting stack offsets: %1")
               .arg(ec.message().c_str()));
        return false;
    }

    // MVLC Eth Jumbo Frames and Eth Receive Buffer Size =================================

    if (mvlc.connectionType() == mvlc::ConnectionType::ETH)
    {
        bool enableJumboFrames = vmeConfig.getControllerSettings().value("mvlc_eth_enable_jumbos").toBool();

        logger(QSL("  %1 jumbo frame support")
               .arg(enableJumboFrames ? QSL("Enabling") : QSL("Disabling")));

        if (auto ec = mvlc.writeRegister(mvlc::registers::jumbo_frame_enable, enableJumboFrames))
        {
            logger(QSL("Error %1 jumbo frames: %2")
                   .arg(enableJumboFrames ? QSL("enabling") : QSL("disabling"))
                   .arg(ec.message().c_str()));
            return false;
        }

        if (auto eth = dynamic_cast<mvlc::eth::MVLC_ETH_Interface *>(mvlc.getImpl()))
        {
            auto counters = eth->getThrottleCounters();
            logger(QSL("  Eth receive buffer size: %1")
                   .arg(format_number(counters.rcvBufferSize, QSL("B"), UnitScaling::Binary, 0, 'f', 0)));
        }
    }

    // Trigger IO ========================================================================

    logger("  Applying MVLC Trigger & I/O setup");

    if (auto ec = setup_trigger_io(mvlcCtrl, vmeConfig, logger))
    {
        logger(QSL("Error applying MVLC Trigger & I/O setup: %1").arg(ec.message().c_str()));
        return false;
    }

    // Global DAQ Start Scripts ==========================================================

    if (!run_init_func(vme_daq_run_global_daq_start_scripts, "Global DAQ Start Scripts"))
        return false;

    // Init Modules ======================================================================

    if (!run_init_func(vme_daq_run_init_modules, "Modules Init"))
        return false;

    // Setup readout stacks ==============================================================

    logger("");
    logger("Setting up MVLC readout stacks");
    if (auto ec = setup_readout_stacks(mvlc, vmeConfig, logger))
    {
        logger(QString("Error setting up readout stacks: %1").arg(ec.message().c_str()));
        return false;
    }


    {
        std::vector<u32> triggers;
        std::error_code ec;
        std::tie(triggers, ec) = get_trigger_values(vmeConfig, logger);

        if (ec)
        {
            logger(QString("MVLC Stack Trigger setup error: %1").arg(ec.message().c_str()));
            return false;
        }
    }

    return true;
}

}
