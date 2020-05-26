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
#include <QCoreApplication>
#include <QtConcurrent>
#include <QThread>
#include <chrono>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mvlc_impl_eth.h>
#include <mesytec-mvlc/mvlc_impl_usb.h>
#include <thread>
#include "mesytec-mvlc/mvlc_listfile.h"
#include "mesytec-mvlc/mvlc_listfile_zip.h"
#include "mesytec-mvlc/util/readout_buffer_queues.h"
#include "mvlc/mvlc_qt_object.h"

#include "mvlc/mvlc_util.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvlc_daq.h"
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

struct ListfileOutput
{
    QString outFilename;
    std::unique_ptr<QuaZip> archive;
    std::unique_ptr<QIODevice> outdev;

    inline bool isOpen()
    {
        return outdev && outdev->isOpen();
    }
};

struct listfile_write_error: public std::runtime_error
{
    listfile_write_error(const char *arg): std::runtime_error(arg) {}
    listfile_write_error(): std::runtime_error("listfile_write_error") {}
};

ListfileOutput listfile_create(ListFileOutputInfo &outinfo,
                             std::function<void (const QString &)> logger)
{
    ListfileOutput result;

    if (!outinfo.enabled)
        return result;

    if (outinfo.fullDirectory.isEmpty())
        throw QString("Error: listfile output directory is not set");

    result.outFilename = make_new_listfile_name(&outinfo);

    switch (outinfo.format)
    {
        case ListFileFormat::Plain:
            {
                result.outdev = std::make_unique<QFile>(result.outFilename);
                auto outFile = reinterpret_cast<QFile *>(result.outdev.get());

                logger(QString("Writing to listfile %1").arg(result.outFilename));

                if (!outFile->open(QIODevice::WriteOnly))
                {
                    throw QString("Error opening listFile %1 for writing: %2")
                        .arg(outFile->fileName())
                        .arg(outFile->errorString())
                        ;
                }

            } break;

        case ListFileFormat::ZIP:
            {
                /* The name of the listfile inside the zip archive. */
                QFileInfo fi(result.outFilename);
                QString listfileFilename(QFileInfo(result.outFilename).completeBaseName());
                listfileFilename += QSL(".mvlclst");

                result.archive = std::make_unique<QuaZip>();
                result.archive->setZipName(result.outFilename);
                result.archive->setZip64Enabled(true);

                logger(QString("Writing listfile into %1").arg(result.outFilename));

                if (!result.archive->open(QuaZip::mdCreate))
                {
                    throw make_zip_error(result.archive->getZipName(), *result.archive);
                }

                result.outdev = std::make_unique<QuaZipFile>(result.archive.get());
                auto outFile = reinterpret_cast<QuaZipFile *>(result.outdev.get());

                QuaZipNewInfo zipFileInfo(listfileFilename);
                zipFileInfo.setPermissions(static_cast<QFile::Permissions>(0x6644));

                bool res = outFile->open(QIODevice::WriteOnly, zipFileInfo,
                                         // password, crc
                                         nullptr, 0,
                                         // method (Z_DEFLATED or 0 for no compression)
                                         Z_DEFLATED,
                                         // level
                                         outinfo.compressionLevel
                                        );

                if (!res)
                {
                    result.outdev.reset();
                    throw make_zip_error(result.archive->getZipName(), *result.archive);
                }
            } break;

        case ListFileFormat::Invalid:
            assert(false);
    }

    return result;
}

void listfile_close(ListfileOutput &lf_out)
{
    if (lf_out.outdev)
    {
        lf_out.outdev->close();
        assert(!lf_out.outdev->isOpen());
    }

    if (lf_out.archive && lf_out.archive->isOpen())
    {
        lf_out.archive->close();

        if (lf_out.archive->getZipError() != UNZ_OK)
            throw make_zip_error(lf_out.archive->getZipName(), *lf_out.archive.get());
    }
}

inline void listfile_write_raw(ListfileOutput &lf_out, const char *buffer, size_t size)
{
    if (!lf_out.isOpen())
        return;

    if (lf_out.outdev->write(buffer, size) != static_cast<qint64>(size))
        throw listfile_write_error();
}

inline void listfile_write_raw(ListfileOutput &lf_out, const u8 *buffer, size_t size)
{
    listfile_write_raw(lf_out, reinterpret_cast<const char *>(buffer), size);
}

void listfile_write_magic(ListfileOutput &lf_out, const MVLCObject &mvlc)
{
    const char *magic = nullptr;

    switch (mvlc.connectionType())
    {
        case mvlc::ConnectionType::ETH:
            magic = "MVLC_ETH";
            break;

        case mvlc::ConnectionType::USB:
            magic = "MVLC_USB";
            break;
    }

    listfile_write_raw(lf_out, magic, std::strlen(magic));
}

// Writes an empty system section
void listfile_write_system_event(ListfileOutput &lf_out, u8 subtype)
{
    if (subtype > mvlc::system_event::subtype::SubtypeMax)
        throw listfile_write_error("system event subtype out of range");

    u32 sectionHeader = (mvlc::frame_headers::SystemEvent << mvlc::frame_headers::TypeShift)
        | ((subtype & mvlc::system_event::SubtypeMask) << mvlc::system_event::SubtypeShift);

    listfile_write_raw(lf_out, reinterpret_cast<const u8 *>(&sectionHeader),
                       sizeof(sectionHeader));
}

// Writes the given buffer into system sections of the given subtype. The data
// is split into multiple sections if needed.
// Note: the input data is passed as a pointer to u32. This means when using
// this function data has to be padded to 32 bits. Padding is not done inside
// this function as different data might require different padding strategies.
void listfile_write_system_event(ListfileOutput &lf_out, u8 subtype,
                                 const u32 *buffp, size_t totalWords)
{
    if (!lf_out.isOpen())
        return;

    if (totalWords == 0)
    {
        listfile_write_system_event(lf_out, subtype);
        return;
    }

    if (subtype > mvlc::system_event::subtype::SubtypeMax)
        throw listfile_write_error("system event subtype out of range");

    const u32 *endp  = buffp + totalWords;

    while (buffp < endp)
    {
        unsigned wordsLeft = endp - buffp;
        unsigned wordsInSection = std::min(
            wordsLeft, static_cast<unsigned>(mvlc::system_event::LengthMask));

        bool isLastSection = (wordsInSection == wordsLeft);

        u32 sectionHeader = (mvlc::frame_headers::SystemEvent << mvlc::frame_headers::TypeShift)
            | ((subtype & mvlc::system_event::SubtypeMask) << mvlc::system_event::SubtypeShift);

        if (!isLastSection)
            sectionHeader |= 0b1 << mvlc::system_event::ContinueShift;

        sectionHeader |= ((wordsInSection & mvlc::system_event::LengthMask)
                          << mvlc::system_event::LengthShift);

        listfile_write_raw(lf_out, reinterpret_cast<const u8 *>(&sectionHeader),
                           sizeof(sectionHeader));

        listfile_write_raw(lf_out, reinterpret_cast<const u8 *>(buffp),
                           wordsInSection * sizeof(u32));

        buffp += wordsInSection;
    }

    assert(buffp == endp);
}

void listfile_write_system_event(ListfileOutput &lf_out, u8 subtype, const QByteArray &bytes)
{
    if (bytes.size() % sizeof(u32))
        throw listfile_write_error("unpadded system section data");

    unsigned totalWords = bytes.size() / sizeof(u32);
    const u32 *buffp = reinterpret_cast<const u32 *>(bytes.data());

    listfile_write_system_event(lf_out, subtype, buffp, totalWords);
}

void listfile_write_mvme_config(ListfileOutput &lf_out, const VMEConfig &vmeConfig)
{
    if (!lf_out.isOpen())
        return;

    QJsonObject json;
    vmeConfig.write(json);
    QJsonObject parentJson;
    parentJson["VMEConfig"] = json;
    QJsonDocument doc(parentJson);
    QByteArray bytes(doc.toJson());

    // Pad using spaces. The Qt JSON parser will handle this without error when
    // reading it back.
    while (bytes.size() % sizeof(u32))
        bytes.append(' ');

    listfile_write_system_event(lf_out, mvlc::system_event::subtype::MVMEConfig, bytes);
}

void listfile_write_endian_marker(ListfileOutput &lf_out)
{
    listfile_write_system_event(
        lf_out, mvlc::system_event::subtype::EndianMarker,
        &mvlc::system_event::EndianMarkerValue, 1);
}

void listfile_write_timestamp(ListfileOutput &lf_out)
{
    if (!lf_out.isOpen())
        return;

    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    u64 timestamp = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();

    listfile_write_system_event(lf_out, mvlc::system_event::subtype::UnixTimetick,
                                reinterpret_cast<u32 *>(&timestamp),
                                sizeof(timestamp) / sizeof(u32));
}

#warning "Use the mesytec-mvlc version here"
void listfile_write_preamble(ListfileOutput &lf_out, const MVLCObject &mvlc,
                             const VMEConfig &vmeConfig)
{
    listfile_write_magic(lf_out, mvlc);
    listfile_write_endian_marker(lf_out);
    listfile_write_mvme_config(lf_out, vmeConfig);
    listfile_write_timestamp(lf_out);
}

void listfile_end_run_and_close(ListfileOutput &lf_out,
                                const QStringList &logBuffer = {},
                                const QByteArray &analysisConfig = {})
{
    if (!lf_out.isOpen())
        return;

    // Write end of file marker to indicate the file was properly written.
    listfile_write_system_event(lf_out, mvlc::system_event::subtype::EndOfFile);

    // Can close the listfile output device now. This is required in case it's
    // a file inside a zip archive as only one file per archive can be open at
    // a time.
    lf_out.outdev->close();

    if (lf_out.archive)
    {
        // Add additional files to the ZIP archive.

        // Logfile
        if (!logBuffer.isEmpty())
        {
            QuaZipNewInfo info("messages.log");
            info.setPermissions(static_cast<QFile::Permissions>(0x6644));
            QuaZipFile outFile(lf_out.archive.get());

            bool res = outFile.open(QIODevice::WriteOnly, info,
                                    // password, crc
                                    nullptr, 0,
                                    // method (Z_DEFLATED or 0 for no compression)
                                    0,
                                    // compression level
                                    1
                                   );

            if (res)
            {
                for (const auto &msg: logBuffer)
                {
                    outFile.write(msg.toLocal8Bit());
                    outFile.write("\n");
                }
            }
        }

        // Analysis
        if (!analysisConfig.isEmpty())
        {
            QuaZipNewInfo info("analysis.analysis");
            info.setPermissions(static_cast<QFile::Permissions>(0x6644));
            QuaZipFile outFile(lf_out.archive.get());

            bool res = outFile.open(QIODevice::WriteOnly, info,
                                    // password, crc
                                    nullptr, 0,
                                    // method (Z_DEFLATED or 0 for no compression)
                                    0,
                                    // compression level
                                    1
                                   );

            if (res)
            {
                outFile.write(analysisConfig);
            }
        }
    }

    // Closes the ZIP file and checks for errors.
    listfile_close(lf_out);
}

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
    mvlc::ReadoutBufferQueues *snoopQueues;

    // lots of mvlc api layers
    MVLC_VMEController *mvlcCtrl = nullptr;;
    MVLCObject *mvlcObj = nullptr;;

    //ListfileOutput listfileOut;
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
            auto archiveName = make_new_listfile_name(outInfo);
            listfileArchiveName = archiveName;
            auto memberName = QFileInfo(archiveName).completeBaseName();
            memberName += QSL(".mvlclst");

            if (outInfo->format != ListFileFormat::ZIP
                && outInfo->format != ListFileFormat::LZ4)
            {
                throw std::runtime_error("Unsupported listfile format");
            }

            d->mvlcZipCreator = std::make_unique<mvlc::listfile::ZipCreator>();
            d->mvlcZipCreator->createArchive(archiveName.toStdString());

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

        setState(DAQState::Running);

        m_workerContext.daqStats.start();
        m_workerContext.daqStats.listfileFilename = listfileArchiveName;

        // wait until readout done
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

        d->mvlcZipCreator.reset();
        m_workerContext.daqStats.stop();

#if 0
        // listfile handling
        d->listfileOut = listfile_create(*m_workerContext.listfileOutputInfo, logger);
        listfile_write_preamble(d->listfileOut, *d->mvlcObj, *m_workerContext.vmeConfig);

        d->preRunClear();

        logMessage("");
        logMessage(QSL("Entering readout loop"));
        // XXX: DAQStats::start clears everything so the listfileFilename has
        // to be assigned afterwards.
        m_workerContext.daqStats.start();
        m_workerContext.daqStats.listfileFilename = d->listfileOut.outFilename;

        readoutLoop();

        logMessage(QSL("Leaving readout loop"));
        logMessage(QSL(""));

        vme_daq_shutdown(getContext().vmeConfig, d->mvlcCtrl, logger);

        // Note: endRun() collects the log contents, which means it should be one of the
        // last actions happening in here. Log messages generated after this point won't
        // show up in the listfile.
        listfile_end_run_and_close(d->listfileOut, m_workerContext.getLogBuffer(),
                                   m_workerContext.getAnalysisJson().toJson());

        assert(!d->listfileOut.isOpen());

        m_workerContext.daqStats.stop();
#endif
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

    // Reset object pointers to ensure we don't accidentially work with stale
    // pointers on next startup.
    d->mvlcCtrl = nullptr;
    d->mvlcObj = nullptr;

    setState(DAQState::Idle);
}

#if 0
void MVLCReadoutWorker::readoutLoop()
{
    setState(DAQState::Running);

    assert(!d->listfileWriterThread.joinable());

    d->listfileWriterContext.quit = false;

    d->listfileWriterThread = std::thread(
        threaded_listfile_writer,
        std::ref(d->listfileWriterContext),
        d->listfileOut.outdev.get(),
        std::ref(m_workerContext.daqStats));

#ifdef Q_OS_LINUX
        pthread_setname_np(d->listfileWriterThread.native_handle(),
                           "rdo_file_writer");
#endif

    vme_analysis_common::TimetickGenerator timetickGen;

    while (true)
    {
        // Write a timestamp system event to the listfile once per second.
        if (timetickGen.generateElapsedSeconds() >= 1)
        {
            std::unique_lock<mesytec::mvlc::TicketMutex> guard(d->listfileWriterContext.listfileMutex);
            listfile_write_timestamp(d->listfileOut);
        }

        // stay in running state
        if (likely(d->state == DAQState::Running && d->desiredState == DAQState::Running))
        {
            size_t bytesTransferred = 0u;
            auto ec = readAndProcessBuffer(bytesTransferred);

            if (ec == mvlc::ErrorType::ConnectionError)
            {
                logMessage(QSL("Lost connection to MVLC. Leaving readout loop. Error=%1")
                           .arg(ec.message().c_str()));
                // Call close on the MVLC_VMEController so that the
                // "disconnected" state is reflected in the whole application.
                d->mvlcCtrl->close();
                break;
            }
        }
        // pause
        else if (d->state == DAQState::Running && d->desiredState == DAQState::Paused)
        {
            pauseDAQ();
        }
        // resume
        else if (d->state == DAQState::Paused && d->desiredState == DAQState::Running)
        {
            resumeDAQ();
        }
        // stop
        else if (d->desiredState == DAQState::Stopping)
        {
            logMessage(QSL("MVLC readout stopping"));
            break;
        }
        // paused
        else if (d->state == DAQState::Paused)
        {
            static const unsigned PauseSleepDuration_ms = 100;
            QThread::msleep(PauseSleepDuration_ms);
        }
        else
        {
            InvalidCodePath;
        }
    }

    setState(DAQState::Stopping);
    d->shutdownReadout();
    maybePutBackBuffer();

    d->listfileWriterContext.quit = true;
    if (d->listfileWriterThread.joinable())
        d->listfileWriterThread.join();

    qDebug() << __PRETTY_FUNCTION__ << "at end";
}
#endif

#if 0
std::error_code MVLCReadoutWorker::readAndProcessBuffer(size_t &bytesTransferred)
{
    // Return ConnectionError from this function if the whole readout should be
    // aborted.
    // Other error codes are ignored right now.
    //
    // TODO: If no data was read within some interval that fact should be logged.

    bytesTransferred = 0u;
    std::error_code ec;

    if (d->mvlc_eth)
        ec = readout_eth(bytesTransferred);
    else
        ec = readout_usb(bytesTransferred);

    auto outputBuffer = getOutputBuffer();

    // TODO: handle the DebugInfoRequest::OnNextError case
    if (outputBuffer->used > 0)
    {
        if (d->debugInfoRequest == Private::DebugInfoRequest::OnNextBuffer)
        {
            d->debugInfoRequest = Private::DebugInfoRequest::None;
            emit debugInfoReady(*outputBuffer);
        }

        flushCurrentOutputBuffer();
    }

    return ec;
}
#endif

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
        logMessage(QString(QSL("MVLC readout resumed")));
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
    return d->mvlcReadoutWorker->counters();
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
