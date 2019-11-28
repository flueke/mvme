#include "mvlc/mvlc_impl_eth.h"
#include "mvlc_readout_worker.h"

#include <QCoreApplication>
#include <QtConcurrent>
#include <QThread>

#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvlc/mvlc_util.h"
#include "mvlc/mvlc_impl_usb.h"
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
//   partial packet to the front fo the new buffer.
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

using namespace mesytec::mvlc;

static const size_t LocalEventBufferSize = Megabytes(1);
static const size_t ReadBufferSize = Megabytes(1);
static const auto ShutdownReadoutMaxWait = std::chrono::seconds(10);

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
        case ConnectionType::ETH:
            magic = "MVLC_ETH";
            break;

        case ConnectionType::USB:
            magic = "MVLC_USB";
            break;
    }

    listfile_write_raw(lf_out, magic, std::strlen(magic));
}

// Writes an empty system section
void listfile_write_system_event(ListfileOutput &lf_out, u8 subtype)
{
    if (subtype > system_event::subtype::SubtypeMax)
        throw listfile_write_error("system event subtype out of range");

    u32 sectionHeader = (frame_headers::SystemEvent << frame_headers::TypeShift)
        | ((subtype & system_event::SubtypeMask) << system_event::SubtypeShift);

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

    if (subtype > system_event::subtype::SubtypeMax)
        throw listfile_write_error("system event subtype out of range");

    const u32 *endp  = buffp + totalWords;

    while (buffp < endp)
    {
        unsigned wordsLeft = endp - buffp;
        unsigned wordsInSection = std::min(
            wordsLeft, static_cast<unsigned>(system_event::LengthMask));

        bool isLastSection = (wordsInSection == wordsLeft);

        u32 sectionHeader = (frame_headers::SystemEvent << frame_headers::TypeShift)
            | ((subtype & system_event::SubtypeMask) << system_event::SubtypeShift);

        if (!isLastSection)
            sectionHeader |= 0b1 << system_event::ContinueShift;

        sectionHeader |= (wordsInSection & system_event::LengthMask) << system_event::LengthShift;

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

void listfile_write_vme_config(ListfileOutput &lf_out, const VMEConfig &vmeConfig)
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

    listfile_write_system_event(lf_out, system_event::subtype::VMEConfig, bytes);
}

void listfile_write_endian_marker(ListfileOutput &lf_out)
{
    listfile_write_system_event(
        lf_out, system_event::subtype::EndianMarker,
        &system_event::EndianMarkerValue, 1);
}

void listfile_write_timestamp(ListfileOutput &lf_out)
{
    if (!lf_out.isOpen())
        return;

    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    u64 timestamp = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();

    listfile_write_system_event(lf_out, system_event::subtype::UnixTimestamp,
                                reinterpret_cast<u32 *>(&timestamp),
                                sizeof(timestamp) / sizeof(u32));
}

void listfile_write_preamble(ListfileOutput &lf_out, const MVLCObject &mvlc,
                             const VMEConfig &vmeConfig)
{
    listfile_write_magic(lf_out, mvlc);
    listfile_write_endian_marker(lf_out);
    listfile_write_vme_config(lf_out, vmeConfig);
    listfile_write_timestamp(lf_out);
}

void listfile_end_run_and_close(ListfileOutput &lf_out,
                                const QStringList &logBuffer = {},
                                const QByteArray &analysisConfig = {})
{
    if (!lf_out.isOpen())
        return;

    // Write end of file marker to indicate the file was properly written.
    listfile_write_system_event(lf_out, system_event::subtype::EndOfFile);

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

//
// Listfile writing (and compression) in a separate thread
//
struct ListfileWriterThreadContext
{
    ListfileWriterThreadContext(size_t bufferCount, size_t bufferSize)
        : quit(false)
        , writeErrorCount(0)
    {
        for (size_t i=0; i<bufferCount;i++)
        {
            bufferStore.emplace_back(std::make_unique<DataBuffer>(bufferSize));
            enqueue(&emptyBuffers, bufferStore.back().get());
        }
    }

    std::atomic<bool> quit;
    std::vector<std::unique_ptr<DataBuffer>> bufferStore;
    ThreadSafeDataBufferQueue emptyBuffers;
    ThreadSafeDataBufferQueue filledBuffers;
    std::atomic<size_t> writeErrorCount;
    // Protects access to the listfiles io device. Threads have to take this
    // lock while they are writing to the file.
    mesytec::mvme::TicketMutex listfileMutex;
};

void threaded_listfile_writer(
    ListfileWriterThreadContext &ctx, QIODevice *outdev, DAQStats &daqStats)
{
    qDebug() << __PRETTY_FUNCTION__ << ">>> >>> >>> startup";

    while (!(ctx.quit && is_empty(&ctx.filledBuffers)))
    {
        bool quit_ = ctx.quit;
        bool is_empty_ = is_empty(&ctx.filledBuffers);

        //qDebug() << "quit =" << quit_ << ", is_empty =" << is_empty(&ctx.filledBuffers)
        //    << ", cond =" << !(quit_ && is_empty_);


        if (auto buffer = dequeue(&ctx.filledBuffers, 100))
        {
            std::unique_lock<mesytec::mvme::TicketMutex> guard(ctx.listfileMutex);

            if (outdev && outdev->isOpen())
            {
                qint64 bytesWritten = outdev->write(
                    reinterpret_cast<const char *>(buffer->data), buffer->used);

                if (bytesWritten != static_cast<qint64>(buffer->used))
                    ++ctx.writeErrorCount;

                daqStats.listFileBytesWritten += bytesWritten;
            }

            enqueue_and_wakeOne(&ctx.emptyBuffers, buffer);
        }
    }
    qDebug() << __PRETTY_FUNCTION__ << "<<< <<< <<< shutdown";
}

} // end anon namespace

struct MVLCReadoutWorker::Private
{
    MVLCReadoutWorker *q = nullptr;;

    // lots of mvlc api layers
    MVLC_VMEController *mvlcCtrl = nullptr;;
    MVLCObject *mvlcObj = nullptr;;
    eth::Impl *mvlc_eth = nullptr;;
    usb::Impl *mvlc_usb = nullptr;;

    ListfileOutput listfileOut;
    u32 nextOutputBufferNumber = 1u;;

    std::atomic<DAQState> state;
    std::atomic<DAQState> desiredState;
    quint32 cyclesToRun = 0;
    bool logBuffers = false;
    DataBuffer previousData;
    DataBuffer localEventBuffer;
    DataBuffer *outputBuffer = nullptr;

    // stat counters
    MVLCReadoutCounters counters = {};
    mutable mesytec::mvme::TicketMutex countersMutex;

    // Threaded listfile writing
    static const size_t ListfileWriterBufferCount = 16u;
    static const size_t ListfileWriterBufferSize  = ReadBufferSize;
    ListfileWriterThreadContext listfileWriterContext;
    std::thread listfileWriterThread;

    Private(MVLCReadoutWorker *q_)
        : q(q_)
        , state(DAQState::Idle)
        , desiredState(DAQState::Idle)
        , previousData(ReadBufferSize)
        , localEventBuffer(LocalEventBufferSize)
        , listfileWriterContext(ListfileWriterBufferCount, ListfileWriterBufferSize)
    {}

    struct EventWithModules
    {
        EventConfig *event;
        QVector<ModuleConfig *> modules;
        QVector<u8> moduleTypes;
    };

    QVector<EventWithModules> events;

    void preRunClear()
    {
        nextOutputBufferNumber = 1u;

        UniqueLock guard(countersMutex);
        counters = {};
    }

    // Cleanly end a running readout session. The code disables all triggers by
    // writing to the trigger registers via the command pipe while in parallel
    // reading and processing data from the data pipe until no more data
    // arrives. These things have to be done in parallel as otherwise in the
    // case of USB the data from the data pipe could clog the bus and no
    // replies could be received on the command pipe.
    QFuture<std::error_code> shutdownReadout()
    {
        assert(q);
        assert(mvlcObj);

        auto f = QtConcurrent::run([this] ()
        {
            static const int DisableTriggerRetryCount = 5;

            for (int try_ = 0; try_ < DisableTriggerRetryCount; try_++)
            {
                if (auto ec = disable_all_triggers(*mvlcObj))
                {
                    if (ec == ErrorType::ConnectionError)
                        return ec;
                }
                else break;
            }

            return std::error_code{};
        });

        using Clock = std::chrono::high_resolution_clock;
        auto tStart = Clock::now();
        size_t bytesTransferred = 0u;
        // The loop could hang forever if disabling the readout triggers does
        // not work for some reason. To circumvent this the total time spent in
        // the loop is limited to ShutdownReadoutMaxWait.
        do
        {
            q->readAndProcessBuffer(bytesTransferred);

            auto elapsed = Clock::now() - tStart;

            if (elapsed > ShutdownReadoutMaxWait)
                break;

        } while (bytesTransferred > 0);

        if (auto ec = f.result())
        {
            q->logMessage(QSL("MVLC Readout: Error disabling triggers: %1")
                          .arg(ec.message().c_str()));
        }

        return f;
    }
};

MVLCReadoutWorker::MVLCReadoutWorker(QObject *parent)
    : VMEReadoutWorker(parent)
    , d(std::make_unique<Private>(this))
{
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

    if (d->mvlcObj)
    {
        switch (d->mvlcObj->connectionType())
        {
            case ConnectionType::ETH:
                d->mvlc_eth = reinterpret_cast<eth::Impl *>(d->mvlcObj->getImpl());
                d->mvlc_usb = nullptr;
                break;

            case ConnectionType::USB:
                d->mvlc_eth = nullptr;
                d->mvlc_usb = reinterpret_cast<usb::Impl *>(d->mvlcObj->getImpl());
                break;
        }
    }
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

    d->cyclesToRun = cycles;
    d->logBuffers = (cycles > 0); // log buffers to GUI if number of cycles has been passed in

    try
    {
        auto logger = [this](const QString &msg)
        {
            this->logMessage(msg);
        };

        setState(DAQState::Starting);

        // Clear the error counts when starting a new run.
        {
            auto &counters = d->mvlcObj->getGuardedStackErrorCounters();
            auto guard = counters.lock();
            counters.counters = {};
        }

        // vme init sequence
        if (!do_VME_DAQ_Init(d->mvlcCtrl))
        {
#if 0
#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
        QMetaObject::invokeMethod(d->mvlcCtrl, &MVLC_VMEController::enableNotificationPolling);
#else
        QMetaObject::invokeMethod(d->mvlcCtrl, "enableNotificationPolling");
#endif
#endif
            setState(DAQState::Idle);
            return;
        }

        if (d->mvlc_eth)
        {
            logMessage(QSL("MVLC connection type is UDP. Sending initial empty request"
                           " via the data socket."));

            static const std::array<u32, 2> EmptyRequest = { 0xF1000000, 0xF2000000 };
            size_t bytesTransferred = 0;

            if (auto ec = d->mvlcObj->write(
                    Pipe::Data,
                    reinterpret_cast<const u8 *>(EmptyRequest.data()),
                    EmptyRequest.size() * sizeof(u32),
                    bytesTransferred))
            {
                throw ec;
            }

            // Reset the per pipe and per packet-channel stats when starting a
            // new run. By reusing these stats there's no need for an extra
            // layer of stats for the ETH readout.
            auto guard = d->mvlcObj->getLocks().lockBoth();
            d->mvlc_eth->resetPipeAndChannelStats();
        }

        logMessage("");

        // Stack, trigger and IO setup.
        // Note: Currently triggers are enabled immediately, this means data
        // will start coming in right away.
        if (auto ec = setup_mvlc(*d->mvlcObj, *getContext().vmeConfig, logger))
            throw ec;

        // listfile handling
        d->listfileOut = listfile_create(*m_workerContext.listfileOutputInfo, logger);
        listfile_write_preamble(d->listfileOut, *d->mvlcObj, *m_workerContext.vmeConfig);

        d->preRunClear();
#if 0
        d->startNotificationPolling();
#endif

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

#if 0
#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
        QMetaObject::invokeMethod(d->mvlcCtrl, &MVLC_VMEController::enableNotificationPolling);
#else
        QMetaObject::invokeMethod(d->mvlcCtrl, "enableNotificationPolling");
#endif
#endif

    // Reset object pointers to ensure we don't accidentially work with stale
    // pointers on next startup.
    d->mvlcCtrl = nullptr;
    d->mvlcObj = nullptr;
    d->mvlc_eth = nullptr;
    d->mvlc_usb = nullptr;

    setState(DAQState::Idle);
}

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
            std::unique_lock<mesytec::mvme::TicketMutex> guard(d->listfileWriterContext.listfileMutex);
            listfile_write_timestamp(d->listfileOut);
        }

        // stay in running state
        if (likely(d->state == DAQState::Running && d->desiredState == DAQState::Running))
        {
            size_t bytesTransferred = 0u;
            auto ec = readAndProcessBuffer(bytesTransferred);

            if (ec == ErrorType::ConnectionError)
            {
                logMessage(QSL("Lost connection to MVLC. Leaving readout loop. Error=%1")
                           .arg(ec.message().c_str()));
                // Call close on the MVLC_VMEController so that the
                // "disconnected" state is reflected in the whole application.
                d->mvlcCtrl->close();
                break;
            }

            if (unlikely(d->cyclesToRun > 0))
            {
                if (d->cyclesToRun == 1)
                {
                    break;
                }
                d->cyclesToRun--;
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

    if (getOutputBuffer()->used > 0)
        flushCurrentOutputBuffer();

    return ec;
}

/* Steps for the ETH readout:
 * get  buffer
 * fill  buffer until full or flush timeout elapsed
 * flush buffer
 */
// Tunable. Effects time to stop/pause and analysis buffer fill-level/count.
// 1000/FlushBufferTimeout is the minimum number of buffers the analysis will
// get per second assuming that we receive any data at all and that the
// analysis can keep up.
// If set too low buffers won't be completely filled even at high data rates
// and queue load will increase. Set too high and the user will have to wait
// longer to see data at low rates.
static const std::chrono::milliseconds FlushBufferTimeout(500);

std::error_code MVLCReadoutWorker::readout_eth(size_t &totalBytesTransferred)
{
    assert(d->mvlc_eth);

    auto destBuffer = getOutputBuffer();
    auto &mvlcLocks = d->mvlcObj->getLocks();
    auto &daqStats = m_workerContext.daqStats;

    // Lock the data lock once and then read until either the buffer is full or
    // FlushBufferTimeout elapsed.
    //auto dataGuard = mvlcLocks.lockData();
    auto tStart = std::chrono::steady_clock::now();

    while (destBuffer->free() >= eth::JumboFrameMaxSize)
    {
        size_t bytesTransferred = 0u;

        // TODO: this would be more efficient if the lock is held for multiple
        // packets. Using the FlushBufferTimeout is too long though and the GUI
        // will become sluggish.
        auto dataGuard = mvlcLocks.lockData();
        auto result = d->mvlc_eth->read_packet(
            Pipe::Data, destBuffer->asU8(), destBuffer->free());
        dataGuard.unlock();

        daqStats.totalBytesRead += result.bytesTransferred;

        // ShortRead means that the received packet length was non-zero but
        // shorter than the two ETH header words. Overwrite this short data on
        // the next iteration so that the framing structure stays intact.
        // Also do not count these short reads in totalBytesTransferred as that
        // would suggest we actually did receive valid data.
        if (result.ec == MVLCErrorCode::ShortRead)
        {
            daqStats.buffersWithErrors++;
            continue;
        }

        destBuffer->used += result.bytesTransferred;
        totalBytesTransferred += result.bytesTransferred;

        // A crude way of handling packets with residual bytes at the end. Just
        // subtract the residue from buffer->used which means the residual
        // bytes will be overwritten by the next packets data. This will at
        // least keep the structure somewhat intact assuming that the
        // dataWordCount in header0 is correct. Note that this case does not
        // happen, the MVLC never generates packets with residual bytes.
        if (unlikely(result.leftoverBytes()))
            destBuffer->used -= result.leftoverBytes();

        auto elapsed = std::chrono::steady_clock::now() - tStart;

        if (elapsed >= FlushBufferTimeout)
            break;
    }

    return {};
}

inline bool is_valid_readout_frame(const FrameInfo &frameInfo)
{
    return (frameInfo.type == frame_headers::StackFrame
            || frameInfo.type == frame_headers::StackContinuation);
}

// Ensure that the readBuffer contains only complete frames. In other words: if
// a frame starts then it should fully fit into the readBuffer. Trailing data
// is moved to the tempBuffer.
//
// Walk through the readBuffer following the frame structure. If a partial
// frame is found at the end of the buffer move the trailing bytes to the
// tempBuffer and shrink the readBuffer accordingly.
//
// Note that invalid data words (ones that do not pass
// is_valid_readout_frame()) are just skipped and left in the buffer without
// modification. This has to be taken into account on the analysis side.
inline void fixup_usb_buffer(
    DataBuffer &readBuffer, DataBuffer &tempBuffer,
    MVLCReadoutCounters &counters)
{
    BufferIterator iter(readBuffer.data, readBuffer.used);

    while (!iter.atEnd())
    {
        // Need at least 32 bits of data to extract a frame header.
        if (iter.longwordsLeft())
        {
            FrameInfo frameInfo = {};

            while (iter.longwordsLeft())
            {
                // Can peek and check the next frame header
                frameInfo = extract_frame_info(iter.peekU32());

                if (is_valid_readout_frame(frameInfo))
                    break;

                ++counters.frameTypeErrors;

                // Unexpected or invalid frame type. This should not happen
                // if the incoming MVLC data and the readout code are
                // correct.
                // Consume the invalid frame header word and try again with the
                // next word.
                iter.extractU32();
            }

            if (!is_valid_readout_frame(frameInfo))
            {
                // The above loop was not able to find a valid readout frame.
                // Go to the top of the outer loop and let that handle any
                // possible leftover bytes on the next iteration.
                continue;
            }

            // Check if the full frame including the header is in the
            // readBuffer. If not move the trailing data to the tempBuffer.
            if (frameInfo.len + 1u > iter.longwordsLeft())
            {
                auto trailingBytes = iter.bytesLeft();
                move_bytes(readBuffer, tempBuffer, iter.asU8(), trailingBytes);
                counters.partialFrameTotalBytes += trailingBytes;
                return;
            }

            // Skip over the frameHeader and the frame contents.
            iter.skipExact(frameInfo.len + 1, sizeof(u32));
        }
        else
        {
            // Not enough data left to get the next frame header. Move trailing
            // bytes to the tempBuffer.
            auto trailingBytes = iter.bytesLeft();
            move_bytes(readBuffer, tempBuffer, iter.asU8(), trailingBytes);
            counters.partialFrameTotalBytes += trailingBytes;
            return;
        }
    }
}

//static const size_t USBReadMinBytes = Kilobytes(256);
static const size_t USBReadMinBytes = mesytec::mvlc::usb::USBSingleTransferMaxBytes;

std::error_code MVLCReadoutWorker::readout_usb(size_t &totalBytesTransferred)
{
    assert(d->mvlc_usb);

    auto destBuffer = getOutputBuffer();
    auto tStart = std::chrono::steady_clock::now();
    auto &mvlcLocks = d->mvlcObj->getLocks();
    auto &daqStats = m_workerContext.daqStats;
    std::error_code ec;

    if (d->previousData.used)
    {
        destBuffer->ensureFreeSpace(d->previousData.used);

        move_bytes(d->previousData, *destBuffer,
                   d->previousData.data, d->previousData.used);

        assert(d->previousData.used == 0);
    }

    destBuffer->ensureFreeSpace(USBReadMinBytes);

    while (destBuffer->free() >= USBReadMinBytes)
    {
        size_t bytesTransferred = 0u;

        auto dataGuard = mvlcLocks.lockData();
        //ec = d->mvlc_usb->read_unbuffered(
        //    Pipe::Data, destBuffer->asU8(), destBuffer->free(), bytesTransferred);
        ec = d->mvlc_usb->read_unbuffered(
            Pipe::Data, destBuffer->asU8(), USBReadMinBytes, bytesTransferred);
        dataGuard.unlock();

        if (ec == ErrorType::ConnectionError)
            break;

        daqStats.totalBytesRead += bytesTransferred;
        destBuffer->used += bytesTransferred;
        totalBytesTransferred += bytesTransferred;

        auto elapsed = std::chrono::steady_clock::now() - tStart;

        if (elapsed >= FlushBufferTimeout)
            break;
    }

    UniqueLock countersGuard(d->countersMutex);
    auto prevFrameTypeErrors = d->counters.frameTypeErrors;
    fixup_usb_buffer(*destBuffer, d->previousData, d->counters);
    if (prevFrameTypeErrors != d->counters.frameTypeErrors)
        ++daqStats.buffersWithErrors;

    return ec;
}

DataBuffer *MVLCReadoutWorker::getOutputBuffer()
{
    DataBuffer *outputBuffer = d->outputBuffer;

    if (!outputBuffer)
    {
        outputBuffer = dequeue(m_workerContext.freeBuffers);

        if (!outputBuffer)
        {
            outputBuffer = &d->localEventBuffer;
        }

        // Setup the newly acquired buffer
        outputBuffer->used = 0;
        outputBuffer->id   = d->nextOutputBufferNumber++;
        outputBuffer->tag  = static_cast<int>((d->mvlc_eth
                                               ? ListfileBufferFormat::MVLC_ETH
                                               : ListfileBufferFormat::MVLC_USB));
        d->outputBuffer = outputBuffer;
    }

    return outputBuffer;
}

void MVLCReadoutWorker::maybePutBackBuffer()
{
    if (d->outputBuffer && d->outputBuffer != &d->localEventBuffer)
    {
        // put the buffer back onto the free queue
        enqueue(m_workerContext.freeBuffers, d->outputBuffer);
    }

    d->outputBuffer = nullptr;
}

void MVLCReadoutWorker::flushCurrentOutputBuffer()
{
    auto outputBuffer = d->outputBuffer;

    if (outputBuffer)
    {
        m_workerContext.daqStats.totalBuffersRead++;

        if (d->listfileOut.outdev)
        {
            if (auto listfileWriterBuffer = dequeue(&d->listfileWriterContext.emptyBuffers))
            {
                // copy the data and queue it up for the writer thread
                *listfileWriterBuffer = *outputBuffer;
                enqueue_and_wakeOne(&d->listfileWriterContext.filledBuffers, listfileWriterBuffer);
            }
        }

        if (outputBuffer != &d->localEventBuffer)
        {
            enqueue_and_wakeOne(m_workerContext.fullBuffers, outputBuffer);
        }
        else
        {
            m_workerContext.daqStats.droppedBuffers++;
        }
        d->outputBuffer = nullptr;
    }
}

void MVLCReadoutWorker::pauseDAQ()
{
    d->shutdownReadout();

    std::unique_lock<mesytec::mvme::TicketMutex> guard(d->listfileWriterContext.listfileMutex);
    listfile_write_system_event(d->listfileOut, system_event::subtype::Pause);

    setState(DAQState::Paused);
    logMessage(QString(QSL("MVLC readout paused")));
}

void MVLCReadoutWorker::resumeDAQ()
{
    assert(d->mvlcObj);

    auto logger = [this](const QString &msg)
    {
        this->logMessage(msg);
    };

    enable_triggers(*d->mvlcObj, *getContext().vmeConfig, logger);

#if 0
    d->startNotificationPolling();
#endif

    std::unique_lock<mesytec::mvme::TicketMutex> guard(d->listfileWriterContext.listfileMutex);
    listfile_write_system_event(d->listfileOut, system_event::subtype::Resume);

    setState(DAQState::Running);
    logMessage(QSL("MVLC readout resumed"));
}

void MVLCReadoutWorker::stop()
{
    if (d->state == DAQState::Running || d->state == DAQState::Paused)
        d->desiredState = DAQState::Stopping;
}

void MVLCReadoutWorker::pause()
{
    if (d->state == DAQState::Running)
        d->desiredState = DAQState::Paused;
}

void MVLCReadoutWorker::resume(quint32 cycles)
{
    if (d->state == DAQState::Paused)
    {
        d->cyclesToRun = cycles;
        d->logBuffers = (cycles > 0); // log buffers to GUI if number of cycles has been passed in
        d->desiredState = DAQState::Running;
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
            emit daqStopped();
            break;

        case DAQState::Paused:
            emit daqPaused();
            break;

        case DAQState::Starting:
        case DAQState::Stopping:
            break;

        case DAQState::Running:
            emit daqStarted();
    }

    QCoreApplication::processEvents();
}

DAQState MVLCReadoutWorker::getState() const
{
    return d->state;
}

MVLCReadoutCounters MVLCReadoutWorker::getReadoutCounters() const
{
    UniqueLock guard(d->countersMutex);
    return d->counters;
}

MVLC_VMEController *MVLCReadoutWorker::getMVLC()
{
    return d->mvlcCtrl;
}

void MVLCReadoutWorker::logError(const QString &msg)
{
    logMessage(QSL("MVLC Readout Error: %1").arg(msg));
}
