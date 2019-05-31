#include "mvlc_readout_worker.h"

#include <QCoreApplication>
#include <QtConcurrent>
#include <QThread>

#include <quazipfile.h>
#include <quazip.h>

#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvlc/mvlc_util.h"
#include "mvlc/mvlc_impl_usb.h"
#include "mvlc/mvlc_impl_eth.h"
#include "mvlc_daq.h"
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

    // TODO: where to handle error notifications? The GUI should display their
    // rates by stack somewhere.
#if 0
    connect(mvlc, &MVLC_VMEController::stackErrorNotification,
            this, [this] (const QVector<u32> &notification)
    {
        bool wasLogged = logMessage(QSL("Stack Error Notification from MVLC (size=%1)")
                                    .arg(notification.size()), true);

        if (!wasLogged)
            return;

        for (const auto &word: notification)
        {
            logMessage(QSL("  0x%1").arg(word, 8, 16, QLatin1Char('0')),
                       false);
        }

        logMessage(QSL("End of Stack Error Notification"), false);
    });
#endif


using namespace mesytec::mvlc;

static const size_t LocalEventBufferSize = Megabytes(1);
static const size_t ReadBufferSize = Megabytes(1);

namespace
{

struct ListfileOutput
{
    QString outFilename;
    std::unique_ptr<QuaZip> archive;
    std::unique_ptr<QIODevice> outdev;
};

ListfileOutput open_listfile(ListFileOutputInfo &outinfo,
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
                listfileFilename += QSL(".mvmelst");

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
    u64 nextOutputBufferNumber = 0u;;

    void preRunClear()
    {
        nextOutputBufferNumber = 0u;
    }
};

MVLCReadoutWorker::MVLCReadoutWorker(QObject *parent)
    : VMEReadoutWorker(parent)
    , d(std::make_unique<Private>())
    , m_state(DAQState::Idle)
    , m_desiredState(DAQState::Idle)
    , m_readBuffer(ReadBufferSize)
    , m_previousData(ReadBufferSize)
    , m_localEventBuffer(LocalEventBufferSize)
{
    d->q = this;
}

MVLCReadoutWorker::~MVLCReadoutWorker()
{
}

void MVLCReadoutWorker::start(quint32 cycles)
{
    if (m_state != DAQState::Idle)
    {
        logMessage("Readout state != Idle, aborting startup");
        return;
    }

    // Setup the Private struct members
    d->mvlcCtrl = qobject_cast<MVLC_VMEController *>(getContext().controller);

    if (!d->mvlcCtrl)
    {
        logMessage("MVLC controller required");
        InvalidCodePath;
        return;
    }

    d->mvlcObj = d->mvlcCtrl->getMVLCObject();

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

    m_cyclesToRun = cycles;
    m_logBuffers = (cycles > 0); // log buffers to GUI if number of cycles has been passed in

    try
    {
        auto logger = [this](const QString &msg)
        {
            this->logMessage(msg);
        };

        setState(DAQState::Starting);

        // vme init sequence
        auto results = vme_daq_init(getContext().vmeConfig, d->mvlcCtrl, logger);
        log_errors(results, logger);

        if (d->mvlcObj->connectionType() == ConnectionType::ETH)
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
        }

        logMessage("Initializing MVLC");

        // Stack and trigger setup. Triggers are enabled immediately, this
        // means data will start coming in right away.
        if (auto ec = setup_mvlc(*d->mvlcObj, *getContext().vmeConfig, logger))
            throw ec;

        // listfile handling
        d->listfileOut = open_listfile(*m_workerContext.listfileOutputInfo, logger);
        m_workerContext.daqStats->listfileFilename = d->listfileOut.outFilename;
        // TODO: write magic and vme config to listfile.

        d->preRunClear();

        logMessage("");
        logMessage(QSL("Entering readout loop"));
        m_workerContext.daqStats->start();

        readoutLoop();

        logMessage(QSL("Leaving readout loop"));
        logMessage(QSL(""));

        vme_daq_shutdown(getContext().vmeConfig, d->mvlcCtrl, logger);

        // Note: endRun() collects the log contents, which means it should be one of the
        // last actions happening in here. Log messages generated after this point won't
        // show up in the listfile.
        //m_listfileHelper->endRun();
        m_workerContext.daqStats->stop();
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

    setState(DAQState::Idle);
}

void MVLCReadoutWorker::readoutLoop()
{
    using vme_analysis_common::TimetickGenerator;

    setState(DAQState::Running);

    TimetickGenerator timetickGen;
    //m_listfileHelper->writeTimetickSection();

    while (true)
    {
        int elapsedSeconds = timetickGen.generateElapsedSeconds();

        while (elapsedSeconds >= 1)
        {
            //m_listfileHelper->writeTimetickSection();
            elapsedSeconds--;
        }

        // stay in running state
        if (likely(m_state == DAQState::Running && m_desiredState == DAQState::Running))
        {
            size_t bytesTransferred = 0u;
            auto ec = readAndProcessBuffer(bytesTransferred);

            if (ec == ErrorType::ConnectionError)
            {
                logMessage(QSL("Lost connection to MVLC. Leaving readout loop. Error=%1")
                           .arg(ec.message().c_str()));
                break;
            }

            if (unlikely(m_cyclesToRun > 0))
            {
                if (m_cyclesToRun == 1)
                {
                    break;
                }
                m_cyclesToRun--;
            }
        }
        // pause
        else if (m_state == DAQState::Running && m_desiredState == DAQState::Paused)
        {
            pauseDAQ();
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            resumeDAQ();
        }
        // stop
        else if (m_desiredState == DAQState::Stopping)
        {
            logMessage(QSL("MVLC readout stopping"));
            break;
        }
        // paused
        else if (m_state == DAQState::Paused)
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

    auto f = QtConcurrent::run([this]()
    {
        return disable_all_triggers(*d->mvlcObj);
    });

    size_t bytesTransferred = 0u;
    // FIXME: the code can hang here forever if disabling the readout triggers
    // does not work.  Measure total time spent in the loop and break out
    // after a threshold has been reached.
    do
    {
        readAndProcessBuffer(bytesTransferred);
    } while (bytesTransferred > 0);

    maybePutBackBuffer();

    if (auto ec = f.result())
    {
        logMessage(QSL("MVLC Readout: Error disabling triggers: %1")
                   .arg(ec.message().c_str()));
    }

    qDebug() << __PRETTY_FUNCTION__ << "at end";
}

std::error_code MVLCReadoutWorker::readAndProcessBuffer(size_t &bytesTransferred)
{
    // TODO: rename this once I can come up with a better name. depending on
    // the connection type and the amount of incoming data, etc. this does
    // different things.

    // Return ConnectionError if the whole readout should be aborted.
    // Other error codes do canceling of the run.
    // If no data was read within some interval that fact should be logged.
    //
    // What this should do:
    // grab a fresh output buffer
    // read into that buffer until either the buffer is full and can be flushed
    // or a certain time has passed and we want to flush a buffer to stay
    // responsive (the low data rate case).
    // If the format needs it do perform consistency checks on the incoming data.
    // For usb: follow the F3 framing.

    bytesTransferred = 0u;
    std::error_code ec;

    if (d->mvlc_eth)
        ec = readout_eth(bytesTransferred);
    else
        ec = readout_usb(bytesTransferred);

    if (getOutputBuffer()->used > 0)
        flushCurrentOutputBuffer();

#if 0
    if (bytesTransferred == 0)
    {
        // Reuse the output buffer in the next call.
        // The side effect is that no buffer will be flushed to the fullQueue.
        getOutputBuffer()->used = 0;
    }
    else
    {
        flushCurrentOutputBuffer();
    }
#endif

    return ec;

#if 0
    auto mvlc = qobject_cast<MVLC_VMEController *>(getContext().controller)->getMVLCObject();

    m_readBuffer.used = 0u;

    if (m_previousData.used)
    {
        std::memcpy(m_readBuffer.data, m_previousData.data, m_previousData.used);
        m_readBuffer.used = m_previousData.used;
        m_previousData.used = 0u;
    }

    // TODO: use lower level (unbuffered) reads

    auto ec = mvlc->read(Pipe::Data,
                         m_readBuffer.data + m_readBuffer.used,
                         m_readBuffer.size - m_readBuffer.used,
                         bytesTransferred);

    m_readBuffer.used += bytesTransferred;

    // TODO: write raw file contents here

    if (bytesTransferred == 0 || ec == ErrorType::ConnectionError)
        return ec;

    if (m_logBuffers)
    {
        const auto bufferNumber = m_workerContext.daqStats->totalBuffersRead;

        size_t bytesToLog = std::min(m_readBuffer.size, (size_t)10240u);

        logMessage(QString(">>> Begin MVLC buffer #%1 (first %2 bytes)")
                   .arg(bufferNumber)
                   .arg(bytesToLog),
                   false);

        logBuffer(BufferIterator(m_readBuffer.data, bytesToLog), [this](const QString &str)
        {
            logMessage(str, false);
        });

        logMessage(QString("<<< End MVLC buffer #%1").arg(bufferNumber), false);
    }

    // update stats
    getContext().daqStats->totalBytesRead += bytesTransferred;
    getContext().daqStats->totalBuffersRead++;

    if (bytesTransferred < 1 * sizeof(u32))
    {
        logMessage(QSL("MVLC Readout Warning: readout data too short (%1 bytes)")
                   .arg(bytesTransferred));
        return make_error_code(MVLCErrorCode::ShortRead);
    }

    BufferIterator iter(m_readBuffer.data, m_readBuffer.used);

    auto outputBuffer = getOutputBuffer();
    outputBuffer->ensureCapacity(outputBuffer->used + m_readBuffer.used * 2);
    //m_rdoState.streamWriter.setOutputBuffer(outputBuffer);

    try
    {
        while (!iter.atEnd())
        {
            // Interpret the frame header (0xF3)
            u32 frameHeader = iter.peekU32();
            auto frameInfo = extract_header_info(frameHeader);

            if (!(frameInfo.type == buffer_headers::StackBuffer
                  || frameInfo.type == buffer_headers::StackContinuation))
            {
                logMessage(QSL("MVLC Readout Warning:"
                               "received unexpected frame header: 0x%1, prevWord=0x%2")
                           .arg(frameHeader, 8, 16, QLatin1Char('0'))
                           .arg(*(iter.asU32() - 1), 8, 16, QLatin1Char('0'))
                           , true);
                return make_error_code(MVLCErrorCode::UnexpectedBufferHeader);
            }

            //qDebug("frameInfo.len=%u, longwordsLeft=%u, frameHeader=0x%08x",
            //       frameInfo.len, iter.longwordsLeft() - 1, frameHeader);

            if (frameInfo.len > iter.longwordsLeft() - 1)
            {
                std::memcpy(m_previousData.data, iter.asU8(), iter.bytesLeft());
                m_previousData.used = iter.bytesLeft();
                flushCurrentOutputBuffer();
                return make_error_code(MVLCErrorCode::NeedMoreData);
            }

            // eat the frame header
            iter.extractU32();

            // The contents of the F3 buffer can be anything produced by the
            // readout stack.  The most common case will be one block read
            // section (F5) per module that is read out. By building custom
            // readout stacks arbitrary structures can be created. For now only
            // block reads are supported by this code.
            // To support non-block reads mixed with block reads the readout
            // stack needs to be interpreted.

            if (frameInfo.stack == 0 || frameInfo.stack - 1 >= m_events.size())
                return make_error_code(MVLCErrorCode::StackIndexOutOfRange);

            const auto &ewm = m_events[frameInfo.stack - 1];

            if (m_rdoState.stack >= 0)
            {
                if (m_rdoState.stack != frameInfo.stack)
                {
                    //qDebug("rdoState.stack != frameInfo.stack (%d != %d)",
                    //       m_rdoState.stack, frameInfo.stack);
                    return make_error_code(MVLCErrorCode::StackIndexOutOfRange);
                }

                assert(m_rdoState.streamWriter.hasOpenEventSection());
                //qDebug("data is continuation for stack %d", m_rdoState.stack);
            }
            else
            {
                assert(!m_rdoState.streamWriter.hasOpenEventSection());
                m_rdoState.stack = frameInfo.stack;
                m_rdoState.streamWriter.openEventSection(m_rdoState.stack - 1);
                //qDebug("data starts for stack %d", m_rdoState.stack);
            }

            s16 mi = std::max(m_rdoState.module, (s16)0);

            //qDebug("data begins with block for module %d", mi);

            for (; mi < ewm.modules.size(); mi++)
            {
                u32 blkHeader = iter.extractU32();
                auto blkInfo = extract_header_info(blkHeader);

                if (blkInfo.type != buffer_headers::BlockRead)
                {
                    logMessage(QSL("MVLC Readout Warning: unexpeceted block header: 0x%1")
                               .arg(blkHeader, 8, 16, QLatin1Char('0')), true);
                    return make_error_code(MVLCErrorCode::UnexpectedBufferHeader);
                }

                if (blkInfo.len == 0)
                {
                    logMessage(QSL("MVLC Readout Warning: received block read frame of size 0"));
                }

                if (!m_rdoState.streamWriter.hasOpenModuleSection())
                {
                    //qDebug("opening module section for module %d", mi);
                    m_rdoState.streamWriter.openModuleSection(ewm.moduleTypes[mi]);
                }
                else
                {
                    //qDebug("data is continuation for module %d", mi);
                }

                for (u16 wi = 0; wi < blkInfo.len; wi++)
                {
                    u32 data = iter.extractU32();
                    m_rdoState.streamWriter.writeModuleData(data);
                }

                if (blkInfo.flags & buffer_flags::Continue)
                {
                    m_rdoState.module = mi;
                    //qDebug("data for module %d continues in next buffer", mi);
                    break;
                }

                //qDebug("data for module %d done, closing module section", mi);
                m_rdoState.streamWriter.closeModuleSection();
                m_rdoState.module++;

                if (m_rdoState.module >= ewm.modules.size())
                    m_rdoState.module = 0;

                u32 endMarker = iter.extractU32();

                if (endMarker != EndMarker)
                {
                    logMessage(QSL("MVLC Readout Warning: "
                                   "unexpected end marker at end of module data: 0x%1")
                               .arg(endMarker, 8, 16, QLatin1Char('0')));
                }
            }

            if (!(frameInfo.flags & buffer_flags::Continue))
            {
                //qDebug("closing event section for stack %d", m_rdoState.stack);
                m_rdoState.streamWriter.writeEventData(EndMarker);
                m_rdoState.streamWriter.closeEventSection();
                m_rdoState.module = 0;
                m_rdoState.stack = -1;
            }
            else
            {
                //qDebug("event from stack %d continues in next frame", frameInfo.stack);
                m_rdoState.stack = frameInfo.stack;
            }
        } // while (!iter.atEnd())
    }
    catch (const end_of_buffer &)
    {
        if (m_rdoState.streamWriter.hasOpenModuleSection())
            m_rdoState.streamWriter.closeModuleSection();

        if (m_rdoState.streamWriter.hasOpenEventSection())
            m_rdoState.streamWriter.closeEventSection();

        logMessage(QSL("MVLC Readout Warning: unexpectedly reached end of readout buffer"));
        return make_error_code(MVLCErrorCode::UnexpectedResponseSize);
    }

    if (m_rdoState.stack >= 0)
    {
        assert(m_rdoState.streamWriter.hasOpenEventSection());
        //qDebug("data for stack %d continues in next frame, leaving event section open",
        //       m_rdoState.stack);
    }
    else
    {
        assert(!m_rdoState.streamWriter.hasOpenEventSection());
        //qDebug("done with input data, stack data does not continue in"
        //       " next frame -> flusing output buffer");
        flushCurrentOutputBuffer();
    }

    return {};
#endif
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
// and queue load will increase.
static const std::chrono::milliseconds FlushBufferTimeout(500);

std::error_code MVLCReadoutWorker::readout_eth(size_t &totalBytesTransferred)
{
    assert(d->mvlc_eth);

    auto destBuffer = getOutputBuffer();
    auto tStart = std::chrono::steady_clock::now();
    auto &mvlcLocks = d->mvlcObj->getLocks();
    auto &daqStats = m_workerContext.daqStats;

    while (destBuffer->free() >= eth::JumboFrameMaxSize)
    {
        size_t bytesTransferred = 0u;

        auto dataGuard = mvlcLocks.lockData();
        auto result = d->mvlc_eth->read_packet(
            Pipe::Data, destBuffer->asU8(), destBuffer->free());
        dataGuard.unlock();

        daqStats->totalBytesRead += result.bytesTransferred;

        // ShortRead means that the received packet length was non-zero but
        // shorter than the two ETH header words. Overwrite this short data on
        // the next iteration so that the framing structure stays intact.
        // Also do not count these short reads in totalBytesTransferred as that
        // would suggest we actually did receive valid data.
        if (result.ec == MVLCErrorCode::ShortRead)
        {
            daqStats->buffersWithErrors++;
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

std::error_code MVLCReadoutWorker::readout_usb(size_t &bytesTransferred)
{
    assert(d->mvlc_usb);
    assert(!m_outputBuffer);

    assert(!"implement me!");
}

DataBuffer *MVLCReadoutWorker::getOutputBuffer()
{
    DataBuffer *outputBuffer = m_outputBuffer;

    if (!outputBuffer)
    {
        outputBuffer = dequeue(m_workerContext.freeBuffers);

        if (!outputBuffer)
        {
            outputBuffer = &m_localEventBuffer;
        }

        // Reset a fresh buffer
        outputBuffer->used = 0;
        outputBuffer->id   = d->nextOutputBufferNumber++;
        outputBuffer->tag  = static_cast<int>((d->mvlc_eth
                                               ? DataBufferFormatTags::MVLC_ETH
                                               : DataBufferFormatTags::MVLC_USB));
        m_outputBuffer = outputBuffer;
    }

    return outputBuffer;
}

void MVLCReadoutWorker::maybePutBackBuffer()
{
    if (m_outputBuffer && m_outputBuffer != &m_localEventBuffer)
    {
        // put the buffer back onto the free queue
        enqueue(m_workerContext.freeBuffers, m_outputBuffer);
    }

    m_outputBuffer = nullptr;
}

void MVLCReadoutWorker::flushCurrentOutputBuffer()
{
    auto outputBuffer = m_outputBuffer;

    if (outputBuffer)
    {
        m_workerContext.daqStats->totalBuffersRead++;

        if (d->listfileOut.outdev)
        {
            // write to listfile
            qint64 bytesWritten = d->listfileOut.outdev->write(
                reinterpret_cast<const char *>(outputBuffer->data),
                outputBuffer->used);

            if (bytesWritten != static_cast<qint64>(outputBuffer->used))
                throw_io_device_error(d->listfileOut.outdev);

            m_workerContext.daqStats->listFileBytesWritten += bytesWritten;
        }

        if (outputBuffer != &m_localEventBuffer)
        {
            enqueue_and_wakeOne(m_workerContext.fullBuffers, outputBuffer);
        }
        else
        {
            m_workerContext.daqStats->droppedBuffers++;
        }
        m_outputBuffer = nullptr;
    }
}

void MVLCReadoutWorker::pauseDAQ()
{
    auto f = QtConcurrent::run([this]()
    {
        return disable_all_triggers(*d->mvlcObj);
    });

    size_t bytesTransferred = 0u;

    do
    {
        readAndProcessBuffer(bytesTransferred);
    } while (bytesTransferred > 0);


    //m_listfileHelper->writePauseSection();

    setState(DAQState::Paused);
    logMessage(QString(QSL("MVLC readout paused")));
}

void MVLCReadoutWorker::resumeDAQ()
{
    auto mvlc = qobject_cast<MVLC_VMEController *>(getContext().controller);
    assert(mvlc);

    enable_triggers(*mvlc->getMVLCObject(), *getContext().vmeConfig);

    //m_listfileHelper->writeResumeSection();

    setState(DAQState::Running);
    logMessage(QSL("MVLC readout resumed"));
}

void MVLCReadoutWorker::stop()
{
    if (m_state == DAQState::Running || m_state == DAQState::Paused)
        m_desiredState = DAQState::Stopping;
}

void MVLCReadoutWorker::pause()
{
    if (m_state == DAQState::Running)
        m_desiredState = DAQState::Paused;
}

void MVLCReadoutWorker::resume(quint32 cycles)
{
    if (m_state == DAQState::Paused)
    {
        m_cyclesToRun = cycles;
        m_logBuffers = (cycles > 0); // log buffers to GUI if number of cycles has been passed in
        m_desiredState = DAQState::Running;
    }
}

bool MVLCReadoutWorker::isRunning() const
{
    return m_state != DAQState::Idle;
}

void MVLCReadoutWorker::setState(const DAQState &state)
{
    qDebug() << __PRETTY_FUNCTION__ << DAQStateStrings[m_state] << "->" << DAQStateStrings[state];
    m_state = state;
    m_desiredState = state;
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
    return m_state;
}

void MVLCReadoutWorker::logError(const QString &msg)
{
    logMessage(QSL("MVLC Readout Error: %1").arg(msg));
}
