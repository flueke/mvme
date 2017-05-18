#include "vmusb_buffer_processor.h"
#include "mvme_context.h"
#include "vmusb.h"
#include "mvme_listfile.h"
#include <memory>

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

#include <quazip/quazip.h>
#include <quazip/quazipfile.h>

using namespace vmusb_constants;

//#define BPDEBUG
//#define WRITE_BUFFER_LOG

// Note: Assumption: VMUSBs HeaderOptMask option is not used!
void format_vmusb_buffer(DataBuffer *buffer, QTextStream &out, u64 bufferNumber)
{
    try
    {
        out << "buffer #" << bufferNumber
            << ": bytes=" << buffer->used
            << ", shortwords=" << buffer->used/sizeof(u16)
            << ", longwords=" << buffer->used/sizeof(u32)
            << endl;

        QString tmp;
        BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align16);


        u32 header1 = iter.extractWord();
        bool lastBuffer     = header1 & Buffer::LastBufferMask;
        bool scalerBuffer   = header1 & Buffer::IsScalerBufferMask;
        bool continuousMode = header1 & Buffer::ContinuationMask;
        bool multiBuffer    = header1 & Buffer::MultiBufferMask;
        u16 numberOfEvents  = header1 & Buffer::NumberOfEventsMask;


        tmp = QString("header1=0x%1, numberOfEvents=%2, lastBuffer=%3, cont=%4, mult=%5, buffer#=%6")
            .arg(header1, 8, 16, QLatin1Char('0'))
            .arg(numberOfEvents)
            .arg(lastBuffer)
            .arg(continuousMode)
            .arg(multiBuffer)
            .arg(bufferNumber)
            ;

        out << tmp << endl;

        for (u16 eventIndex = 0; eventIndex < numberOfEvents; ++eventIndex)
        {
            u32 eventHeader = iter.extractShortword();
            u8 stackID          = (eventHeader >> Buffer::StackIDShift) & Buffer::StackIDMask;
            bool partialEvent   = eventHeader & Buffer::ContinuationMask;
            u32 eventLength     = eventHeader & Buffer::EventLengthMask;

            tmp = QString("event #%5, header=0x%1, stackID=%2, length=%3 shorts, partial=%4, buffer#=%6")
                .arg(eventHeader, 8, 16, QLatin1Char('0'))
                .arg(stackID)
                .arg(eventLength)
                .arg(partialEvent)
                .arg(eventIndex)
                .arg(bufferNumber)
                ;

            out << tmp << endl << "longwords:" << endl;

            int col = 0;
            u32 longwordsLeft = eventLength / 2;

            while (longwordsLeft--)
            {
                tmp = QString("0x%1").arg(iter.extractU32(), 8, 16, QLatin1Char('0'));
                out << tmp;
                ++col;
                if (col < 8)
                {
                    out << " ";
                }
                else
                {
                    out << endl;
                    col = 0;
                }
            }

            u32 shortwordsLeft = eventLength - ((eventLength / 2) * 2);

            out << endl << "shortwords:" << endl;
            col = 0;
            while (shortwordsLeft--)
            {
                tmp = QString("0x%1").arg(iter.extractU16(), 4, 16, QLatin1Char('0'));
                out << tmp;
                ++col;
                if (col < 8)
                {
                    out << " ";
                }
                else
                {
                    out << endl;
                    col = 0;
                }
            }

            out << endl << "end of event #" << eventIndex << endl;
        }


        if (iter.bytesLeft())
        {
            out << "bytes:" << endl;
            out << iter.bytesLeft() << " bytes left in buffer:" << endl;
            int col = 0;
            while (iter.bytesLeft())
            {
                tmp = QString("0x%1").arg(iter.extractU8(), 2, 16, QLatin1Char('0'));
                out << tmp;
                ++col;
                if (col < 8)
                {
                    out << " ";
                }
                else
                {
                    out << endl;
                    col = 0;
                }
            }
            out << endl;
        }
    }
    catch (const end_of_buffer &)
    {
        out << "!!! end of buffer reached unexpectedly !!!" << endl;
    }
}

static std::runtime_error make_zip_error(const QString &msg, const QuaZip &zip)
{
  auto m = QString("Error: archive=%1, error=%2")
    .arg(msg)
    .arg(zip.getZipError());

  return std::runtime_error(m.toStdString());
}

static void throw_io_device_error(QIODevice *device)
{
    if (auto zipFile = qobject_cast<QuaZipFile *>(device))
    {
        throw make_zip_error(zipFile->getZip()->getZipName(),
                             *(zipFile->getZip()));
    }
    else if (auto file = qobject_cast<QFile *>(device))
    {
        throw QString("Error: file=%1, error=%2")
            .arg(file->fileName())
            .arg(file->errorString())
            ;
    }
    else
    {
        throw QString("IO Error: %1")
            .arg(device->errorString());
    }
}

/* +=========================================================================+
 * |             Buffer Processing - What this code tries to do              |
 * +=========================================================================+
 *
 * In DAQ mode VMUSB yields buffers in the following format:
 *   Buffer Header (numberOfEvents)
 *     Event Header0 (stackID, eventLength, partialEvent)
 *       Event Data0
 *     Event Header1 (stackID, eventLength, partialEvent)
 *       Event Data1
 *     ...
 *     Event HeaderN
 *       Event DataN
 *     BufferTerminator
 *
 * Event data is further structured by our own readout commands to contain
 * module data separated by EndMarker (0x87654321).
 *
 * VMUSB has an internal buffer of 2k 16-bit words for event assembly. If the
 * length of the data of a readout stack exceeds this size the Event Header
 * will have the partialEvent bit set. The last part of the event will be
 * marked by having the partialEvent bit unset again.
 *
 * Our readout assumes 32-bit aligned data from the readout commands but VMUSB
 * interally uses 16-bit aligned data. This means a partial event can be split
 * at a 16-bit boundary and thus a 32-bit word can be part of two event
 * sections.
 *
 * The code below tries to transform incoming data from the VMUSB format into
 * our MVME format (see mvme_listfile.h) for details. Partial events are
 * reassembled, event data is parsed and split into individual module sections
 * and VMUSB stackIDs are mapped to EventConfigs. Error and consistency checks
 * are done and invalid data is discarded.
 *
 * The strategy to deal with buffers and partial events is as follows: If there
 * are no partial events the VMUSB buffer is processed completely and then the
 * output buffer is put into the outgoing queue. If there is a partial event,
 * buffers are processed until the event is reassembled and then the output
 * buffer is flushed immediately. This is done to avoid the case where partial
 * events appear in succession in which case the output buffer would never get
 * flushed and grow indefinitely.
 */

/* To keep track of the current event in case of a partial event spanning
 * multiple buffers.
 * Offsets are used instead of pointers as the buffer might have to be resized
 * which can invalidate pointers into it.
 */
struct ProcessorState
{
    s32 stackID = -1;                   // stack id of the current event or -1 if no event is "in progress".

    s32 eventSize = 0;                  // size of the event section in 32-bit words.
    size_t eventHeaderOffset = 0;       // offset into the output buffer

    s32 moduleSize = 0;                 // size of the module section in 32-bit words.
    ssize_t moduleHeaderOffset = 0;      // offset into the output buffer or -1 if no moduleHeader has been written yet
    s32 moduleIndex = -1;               // index into the list of eventconfigs or -1 if no module is "in progress"

    /* VMUSB uses 16-bit words internally which means it can split our 32-bit
     * aligned module data words in case of partial events. If this is the case
     * the partial 16-bits are stored and reused when the next buffer starts.
     */
    u16 partialData = 0;
    bool hasPartialData = false;

    // True if the last event was partial
    bool wasPartial = false;
};

struct ProcessorAction
{
    static const u32 NoneSet     = 0;
    static const u32 KeepState   = 1u << 0; // Keep the ProcessorState. If unset resets the state.
    static const u32 FlushBuffer = 1u << 1; // Flush the current output buffer and acquire a new one
    static const u32 SkipInput   = 1u << 2; // Skip the current input buffer.
                                            // Implies state reset and reuses the buffer without flusing it.
};

struct VMUSBBufferProcessorPrivate
{
    VMUSBBufferProcessor *m_q;
    QuaZip m_listFileArchive;
    QIODevice *m_listFileOut = nullptr;
#ifdef WRITE_BUFFER_LOG
    QFile *m_bufferLogFile = nullptr;
#endif

    ProcessorState m_state;
    DataBuffer *m_outputBuffer = nullptr;

    DataBuffer *getOutputBuffer();
};

static void processQtEvents(QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents)
{
    QCoreApplication::processEvents(flags);
}

VMUSBBufferProcessor::VMUSBBufferProcessor(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_d(new VMUSBBufferProcessorPrivate)
    , m_context(context)
    , m_localEventBuffer(27 * 1024 * 2)
    , m_listFileWriter(new ListFileWriter(this))
{
    m_d->m_q = this;
}

void VMUSBBufferProcessor::beginRun()
{
    Q_ASSERT(m_freeBufferQueue);
    Q_ASSERT(m_filledBufferQueue);
    Q_ASSERT(m_d->m_outputBuffer == nullptr);

    m_vmusb = dynamic_cast<VMUSB *>(m_context->getController());

    if (!m_vmusb)
    {
        /* This should not happen but ensures that m_vmusb is set when
         * processBuffer() will be called later on. */
        throw QString(QSL("Error from VMUSBBufferProcessor: no VMUSB present!"));
    }

    resetRunState();

    auto outputInfo = m_context->getListFileOutputInfo();
    QString outPath = m_context->getListFileOutputDirectoryFullPath();
    bool listFileOutputEnabled = outputInfo.enabled;
    auto runInfo = m_context->getRunInfo();

    // TODO: this needs to move into some generic listfile handler!
    if (listFileOutputEnabled && !outPath.isEmpty())
    {
        delete m_d->m_listFileOut;
        m_d->m_listFileOut = nullptr;

        switch (outputInfo.format)
        {
            case ListFileFormat::Plain:
                {
                    QFile *outFile = new QFile;
                    m_d->m_listFileOut = outFile;
                    QString outFilename = outPath + '/' + runInfo.runId + ".mvmelst";
                    outFile->setFileName(outFilename);

                    logMessage(QString("Writing to listfile %1").arg(outFilename));

                    if (outFile->exists())
                    {
                        throw QString("Error: listFile %1 exists");
                    }

                    if (!outFile->open(QIODevice::WriteOnly))
                    {
                        throw QString("Error opening listFile %1 for writing: %2")
                            .arg(outFile->fileName())
                            .arg(outFile->errorString())
                            ;
                    }

                    m_listFileWriter->setOutputDevice(outFile);
                    getStats()->listfileFilename = outFilename;
                } break;

            case ListFileFormat::ZIP:
                {
                    QString outFilename = outPath + '/' + runInfo.runId + ".zip";
                    m_d->m_listFileArchive.setZipName(outFilename);
                    m_d->m_listFileArchive.setZip64Enabled(true);

                    logMessage(QString("Writing listfile into %1").arg(outFilename));

                    if (!m_d->m_listFileArchive.open(QuaZip::mdCreate))
                    {
                        throw make_zip_error(m_d->m_listFileArchive.getZipName(), m_d->m_listFileArchive);
                    }

                    auto outFile = new QuaZipFile(&m_d->m_listFileArchive);
                    m_d->m_listFileOut = outFile;

                    QuaZipNewInfo zipFileInfo("listfile.mvmelst");
                    zipFileInfo.setPermissions(static_cast<QFile::Permissions>(0x6644));

                    bool res = outFile->open(QIODevice::WriteOnly, zipFileInfo,
                                             // password, crc
                                             nullptr, 0,
                                             // method (Z_DEFLATED or 0 for no compression)
                                             Z_DEFLATED,
                                             // level
                                             outputInfo.compressionLevel
                                            );

                    if (!res)
                    {
                        delete m_d->m_listFileOut;
                        m_d->m_listFileOut = nullptr;
                        throw make_zip_error(m_d->m_listFileArchive.getZipName(), m_d->m_listFileArchive);
                    }

                    m_listFileWriter->setOutputDevice(m_d->m_listFileOut);
                    getStats()->listfileFilename = outFilename;

                } break;

            InvalidDefaultCase;
        }


        QJsonObject daqConfigJson;
        m_context->getVMEConfig()->write(daqConfigJson);
        QJsonObject configJson;
        configJson["DAQConfig"] = daqConfigJson;
        QJsonDocument doc(configJson);

        if (!m_listFileWriter->writePreamble() || !m_listFileWriter->writeConfig(doc.toJson()))
        {
            throw_io_device_error(m_d->m_listFileOut);
        }

        getStats()->listFileBytesWritten = m_listFileWriter->bytesWritten();
    }

#ifdef WRITE_BUFFER_LOG
    m_d->m_bufferLogFile = new QFile("buffer.log", this);
    m_d->m_bufferLogFile->open(QIODevice::WriteOnly);
#endif
}

void VMUSBBufferProcessor::endRun()
{
    if (m_d->m_outputBuffer && m_d->m_outputBuffer != &m_localEventBuffer)
    {
        // Return buffer to the free queue
        QMutexLocker lock(&m_freeBufferQueue->mutex);
        m_freeBufferQueue->queue.enqueue(m_d->m_outputBuffer);
        m_d->m_outputBuffer = nullptr;
    }

#ifdef WRITE_BUFFER_LOG
    delete m_d->m_bufferLogFile;
    m_d->m_bufferLogFile = nullptr;
#endif

    if (m_d->m_listFileOut && m_d->m_listFileOut->isOpen())
    {
        if (!m_listFileWriter->writeEndSection())
        {
            throw_io_device_error(m_d->m_listFileOut);
        }

        getStats()->listFileBytesWritten = m_listFileWriter->bytesWritten();

        m_d->m_listFileOut->close();

        auto outputInfo = m_context->getListFileOutputInfo();

        // TODO: more error reporting here (file I/O)
        switch (outputInfo.format)
        {
            case ListFileFormat::Plain:
                {
                    // Write a Logfile
                    QFile *listFileOut = qobject_cast<QFile *>(m_d->m_listFileOut);
                    Q_ASSERT(listFileOut);
                    QString logFileName = listFileOut->fileName();
                    logFileName.replace(".mvmelst", ".log");
                    QFile logFile(logFileName);
                    if (logFile.open(QIODevice::WriteOnly))
                    {
                        auto messages = m_context->getLogBuffer();
                        for (const auto &msg: messages)
                        {
                            logFile.write(msg.toLocal8Bit());
                            logFile.write("\n");
                        }
                    }
                } break;

            case ListFileFormat::ZIP:
                {

                    // Logfile
                    {
                        QuaZipNewInfo info("messages.log");
                        info.setPermissions(static_cast<QFile::Permissions>(0x6644));
                        QuaZipFile outFile(&m_d->m_listFileArchive);

                        bool res = outFile.open(QIODevice::WriteOnly, info,
                                                // password, crc
                                                nullptr, 0,
                                                // method (Z_DEFLATED or 0 for no compression)
                                                0,
                                                // level
                                                outputInfo.compressionLevel
                                               );

                        if (res)
                        {
                            auto messages = m_context->getLogBuffer();
                            for (const auto &msg: messages)
                            {
                                outFile.write(msg.toLocal8Bit());
                                outFile.write("\n");
                            }
                        }
                    }

                    // Analysis
                    {
                        QuaZipNewInfo info("analysis.analysis");
                        info.setPermissions(static_cast<QFile::Permissions>(0x6644));
                        QuaZipFile outFile(&m_d->m_listFileArchive);

                        bool res = outFile.open(QIODevice::WriteOnly, info,
                                                // password, crc
                                                nullptr, 0,
                                                // method (Z_DEFLATED or 0 for no compression)
                                                0,
                                                // level
                                                outputInfo.compressionLevel
                                               );

                        if (res)
                        {
                            outFile.write(m_context->getAnalysisJsonDocument().toJson());
                        }
                    }

                    m_d->m_listFileArchive.close();

                    if (m_d->m_listFileArchive.getZipError() != UNZ_OK)
                    {
                        throw make_zip_error(m_d->m_listFileArchive.getZipName(), m_d->m_listFileArchive);
                    }
                } break;

                InvalidDefaultCase;
        }
    }
}

void VMUSBBufferProcessor::resetRunState()
{
    auto eventConfigs = m_context->getEventConfigs();
    m_eventConfigByStackID.clear();

    for (auto config: eventConfigs)
    {
        m_eventConfigByStackID[config->stackID] = config;
    }

    m_d->m_state = ProcessorState();
}

// Returns the current output buffer if one is set. Otherwise sets and returns
// a new output buffer.
//
// The buffer will be taken from the free queue if possible, otherwise the
// local buffer will be used.
DataBuffer *VMUSBBufferProcessorPrivate::getOutputBuffer()
{
    DataBuffer *outputBuffer = m_outputBuffer;

    if (!outputBuffer)
    {
        outputBuffer = m_q->getFreeBuffer();

        if (!outputBuffer)
        {
            outputBuffer = &m_q->m_localEventBuffer;
        }

        // Reset a fresh buffer
        outputBuffer->used = 0;
        m_outputBuffer = outputBuffer;
    }

    return outputBuffer;
}

void VMUSBBufferProcessor::processBuffer(DataBuffer *readBuffer)
{
#ifdef BPDEBUG
    qDebug() << __PRETTY_FUNCTION__;
#endif

    using LF = listfile_v1;

    auto stats = getStats();
    u64 bufferNumber = stats->totalBuffersRead;
    BufferIterator iter(readBuffer->data, readBuffer->used, BufferIterator::Align16);
    DataBuffer *outputBuffer = nullptr;
    ProcessorState *state = &m_d->m_state;
    u32 action = 0;

#ifdef WRITE_BUFFER_LOG
    {
        QTextStream out(m_d->m_bufferLogFile);
        out << ">>>>> begin buffer #" << bufferNumber << endl;
        format_vmusb_buffer(readBuffer, out, bufferNumber);
        out << "<<<<< end buffer #" << bufferNumber << endl;
    }
#endif

    try
    {
        // extract buffer header information

        u32 header1 = iter.extractU16();

        bool lastBuffer     = header1 & Buffer::LastBufferMask;
        bool scalerBuffer   = header1 & Buffer::IsScalerBufferMask;
        bool continuousMode = header1 & Buffer::ContinuationMask;
        bool multiBuffer    = header1 & Buffer::MultiBufferMask;
        u16 numberOfEvents  = header1 & Buffer::NumberOfEventsMask;

#ifdef BPDEBUG
        qDebug("%s buffer #%llu, buffer_size=%u, header1: 0x%08x, lastBuffer=%d"
               ", scalerBuffer=%d, continuousMode=%d, multiBuffer=%d, numberOfEvents=%u",
               __PRETTY_FUNCTION__,
               bufferNumber, readBuffer->used, header1, lastBuffer, scalerBuffer,
               continuousMode, multiBuffer, numberOfEvents);
#endif

        // iterate over the event sections
        for (u16 eventIndex=0; eventIndex < numberOfEvents; ++eventIndex)
        {
            outputBuffer = m_d->getOutputBuffer();

#ifdef BPDEBUG
            qDebug() << __PRETTY_FUNCTION__ << "using output buffer" << outputBuffer;
#endif

            try
            {
                action = processEvent(iter, outputBuffer, state, bufferNumber, eventIndex, numberOfEvents);
            }
            catch (const end_of_buffer &)
            {
                qDebug() << __PRETTY_FUNCTION__
                    << "buffer #" << bufferNumber
                    << "eventIndex" << eventIndex
                    << "numberOfEvents" << numberOfEvents
                    << "processEvent() raised end_of_buffer"
                    ;
                throw;
            }

            if (!(action & ProcessorAction::KeepState) || (action & ProcessorAction::SkipInput))
            {
#ifdef BPDEBUG
                qDebug() << __PRETTY_FUNCTION__ << "resetting ProcessorState";
#endif
                *state = ProcessorState();
            }

            if (action & ProcessorAction::SkipInput)
            {
#ifdef BPDEBUG
                qDebug() << __PRETTY_FUNCTION__ << "skipping input buffer";
#endif
                break;
            }

            if (action & ProcessorAction::FlushBuffer)
            {
#ifdef BPDEBUG
                qDebug() << __PRETTY_FUNCTION__ << "flusing output buffer" << outputBuffer;
#endif
                //
                // FlushBuffer
                //

                if (m_d->m_listFileOut && m_d->m_listFileOut->isOpen())
                {
                    if (!m_listFileWriter->writeBuffer(reinterpret_cast<const char *>(outputBuffer->data),
                                                       outputBuffer->used))
                    {
                        throw_io_device_error(m_d->m_listFileOut);
                    }
                    getStats()->listFileBytesWritten = m_listFileWriter->bytesWritten();
                }

                if (outputBuffer != &m_localEventBuffer)
                {
                    // It's not the local buffer -> put it into the queue of filled buffers
                    m_filledBufferQueue->mutex.lock();
                    m_filledBufferQueue->queue.enqueue(outputBuffer);
                    m_filledBufferQueue->mutex.unlock();
                    m_filledBufferQueue->wc.wakeOne();
                }
                else
                {
                    getStats()->droppedBuffers++;
                }

                m_d->m_outputBuffer = outputBuffer = nullptr;
            }
        }

#ifdef BPDEBUG
        qDebug() << __PRETTY_FUNCTION__
            << "buffer #" << bufferNumber
            << "fell out of event iteration";
#endif

        if (!(action & ProcessorAction::SkipInput))
        {
#ifdef BPDEBUG
            qDebug() << __PRETTY_FUNCTION__
                << "buffer #" << bufferNumber
                << "after event iteration and continuing with the buffer;"
                << "bytesLeft =" << iter.bytesLeft();
#endif

            while (iter.shortwordsLeft() >= 2)
            {
                for (int i=0; i<2; ++i)
                {
                    u16 bufferTerminator = iter.extractU16();
                    if (bufferTerminator != Buffer::BufferTerminator)
                    {
                        auto msg = QString(QSL("VMUSB Warning: (buffer #%2) unexpected buffer terminator 0x%1"))
                            .arg(bufferTerminator, 4, 16, QLatin1Char('0'))
                            .arg(bufferNumber)
                            ;
                        logMessage(msg);
                        qDebug() << __PRETTY_FUNCTION__ << msg;
                    }
                }
            }

            if (iter.bytesLeft() != 0)
            {
                auto msg = QString(QSL("VMUSB Warning: (buffer #%3) %1 bytes left in buffer, numberOfEvents=%2"))
                    .arg(iter.bytesLeft())
                    .arg(numberOfEvents)
                    .arg(bufferNumber)
                    ;
                logMessage(msg);
                qDebug() << __PRETTY_FUNCTION__ << msg;

                while (iter.longwordsLeft())
                {
                    logMessage(QString(QSL("  0x%1"))
                                    .arg(iter.extractU32(), 8, 16, QLatin1Char('0')));
                }

                while (iter.wordsLeft())
                {
                    logMessage(QString(QSL("  0x%1"))
                                    .arg(iter.extractU16(), 4, 16, QLatin1Char('0')));
                }

                while (iter.bytesLeft())
                {
                    logMessage(QString(QSL("  0x%1"))
                                    .arg(iter.extractU8(), 2, 16, QLatin1Char('0')));
                }
            }

            return;
        }
        else
        {
#ifdef BPDEBUG
            qDebug() << __PRETTY_FUNCTION__
                << "buffer #" << bufferNumber
                << "was told to skip the buffer;"
                << "bytesLeft =" << iter.bytesLeft()
                ;
#endif
            getStats()->buffersWithErrors++;
        }
    }
    catch (const end_of_buffer &)
    {
        auto msg = QString(QSL("VMUSB Warning: (buffer #%1) end of readBuffer reached unexpectedly!")
                           .arg(bufferNumber));
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg << "skipping input buffer";
        getStats()->buffersWithErrors++;
    }

    if (outputBuffer && outputBuffer != &m_localEventBuffer)
    {
        // Put the buffer back onto the free queue
        QMutexLocker lock(&m_freeBufferQueue->mutex);
        m_freeBufferQueue->queue.enqueue(outputBuffer);
        m_d->m_outputBuffer = nullptr;
    }
}

/* Process one VMUSB event, transforming it into a MVME event.
 * MVME Event structure:
 * Event Header
 *   SubeventHeader (== Module header)
 *     Raw module contents
 *     EndMarker
 *   SubeventHeader (== Module header)
 *     Raw module contents
 *     EndMarker
 * EndMarker
 * Event Header
 * ...
 */
u32 VMUSBBufferProcessor::processEvent(BufferIterator &iter, DataBuffer *outputBuffer, ProcessorState *state, u64 bufferNumber,
                                       u16 eventIndex, u16 numberOfEvents)
{
    using LF = listfile_v1;

    if (iter.shortwordsLeft() < 1)
    {
        auto msg = QString(QSL("VMUSB Error: (buffer #%1) processEvent(): end of buffer when extracting event header"))
            .arg(bufferNumber);
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;
        return ProcessorAction::SkipInput;
    }

    u32 eventHeader     = iter.extractU16();
    u8 stackID          = (eventHeader >> Buffer::StackIDShift) & Buffer::StackIDMask;
    bool partialEvent   = eventHeader & Buffer::ContinuationMask;
    u32 eventLength     = eventHeader & Buffer::EventLengthMask; // in 16-bit words

#ifdef BPDEBUG
    qDebug("%s eventHeader=0x%08x, stackID=%u, partialEvent=%d, eventLength=%u shorts",
           __PRETTY_FUNCTION__, eventHeader, stackID, partialEvent, eventLength);
#endif

    if (partialEvent)
    {
#ifdef BPDEBUG
        auto msg = QString(QSL("VMUSB (buffer #%1) got a partial event!"))
            .arg(bufferNumber);
        qDebug() << __PRETTY_FUNCTION__ << msg;
#endif
    }

    if (iter.shortwordsLeft() < eventLength)
    {
        auto msg = QSL("VMUSB Error: (buffer #%1) event length (%2 shorts/%3 bytes) exceeds buffer length (%4 shorts, %5 bytes), skipping buffer")
            .arg(bufferNumber)
            .arg(eventLength)
            .arg(eventLength * sizeof(u16))
            .arg(iter.shortwordsLeft())
            .arg(iter.bytesLeft())
            ;
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;
        return ProcessorAction::SkipInput;
    }

    if (stackID > StackIDMax)
    {
        auto msg = QString(QSL("VMUSB: (buffer #%2) Parsed stackID=%1 is out of range, skipping buffer"))
            .arg(stackID)
            .arg(bufferNumber);
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;
        return ProcessorAction::SkipInput;
    }

    if (!m_eventConfigByStackID.contains(stackID))
    {
        auto msg = QString(QSL("VMUSB: (buffer #%3) No event config for stackID=%1, eventLength=%2, skipping event"))
            .arg(stackID)
            .arg(eventLength)
            .arg(bufferNumber);
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;
        iter.skip(sizeof(u16), eventLength);
        return ProcessorAction::KeepState;
    }

    /* Create a local iterator limited by the event length. A check above made
     * sure that the event length does not exceed the inputs size. */
    BufferIterator eventIter(iter.buffp, eventLength * sizeof(u16), iter.alignment);

    // ensure double the event length is available
    outputBuffer->ensureCapacity(eventLength * sizeof(u16) * 2);

    auto eventConfig = m_eventConfigByStackID[stackID];

    if (m_logBuffers)
    {
        logMessage(QString(">>> Begin event %1 in buffer #%2")
                   .arg(eventIndex)
                   .arg(bufferNumber));

        logBuffer(eventIter, [this](const QString &str) { logMessage(str); });

        logMessage(QString("<<< End event %1 in buffer #%2")
                   .arg(eventIndex)
                   .arg(bufferNumber));
    }

    s32 moduleCount = eventConfig->modules.size();

    if (moduleCount == 0)
    {
        auto msg = QString(QSL("VMUSB: (buffer #%1) No module configs for stackID=%2, skipping event"))
            .arg(bufferNumber)
            .arg(stackID);

        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg << "-> skipping event and returning NoneSet";

        iter.skip(sizeof(u16), eventLength);
        return ProcessorAction::NoneSet;
    }

    if (state->stackID < 0)
    {
        *state = ProcessorState();
        state->stackID = stackID;;
        state->wasPartial = partialEvent;

        state->eventHeaderOffset = outputBuffer->used;
        u32 *mvmeEventHeader = outputBuffer->asU32();
        outputBuffer->used += sizeof(u32);

        /* Store the event index in the header. */
        int configEventIndex = m_context->getEventConfigs().indexOf(eventConfig);
        *mvmeEventHeader = (ListfileSections::SectionType_Event << LF::SectionTypeShift) & LF::SectionTypeMask;
        *mvmeEventHeader |= (configEventIndex << LF::EventTypeShift) & LF::EventTypeMask;

#ifdef BPDEBUG
        qDebug() << __PRETTY_FUNCTION__
            << "buffer #" << bufferNumber
            << "writing mvmeEventHeader @" << mvmeEventHeader
            << "mvmeEventHeader =" << QString::number(*mvmeEventHeader, 16)
            ;
#endif
    }
    else
    {
        if (state->stackID != stackID)
        {
            Q_ASSERT(state->wasPartial);
            auto msg = QString(QSL("VMUSB: (buffer #%1) Got differing stackIDs during partial event processing"))
                       .arg(bufferNumber);
            logMessage(msg);
            qDebug() << __PRETTY_FUNCTION__ << msg << "returning SkipInput";
            return ProcessorAction::SkipInput;
        }
    }

    while (true)
    {
        if (state->moduleIndex < 0 || state->moduleHeaderOffset < 0)
        {
            // Have to write a new module header

            if (state->moduleIndex < 0)
            {
                state->moduleIndex = 0;
            }
            else
            {
                ++state->moduleIndex;
            }

            state->moduleSize  = 0;
            state->moduleHeaderOffset = outputBuffer->used;

            if (state->moduleIndex >= eventConfig->modules.size())
            {
                logMessage(QString(QSL("VMUSB: (buffer #%1) Module index %2 is out of range while parsing input. Skipping buffer"))
                    .arg(bufferNumber)
                    .arg(state->moduleIndex)
                    );

                qDebug() << __PRETTY_FUNCTION__
                    << "buffer #" << bufferNumber
                    << "eventIndex =" << eventIndex
                    << "moduleIndex =" << state->moduleIndex
                    << "moduleIndex is out of range -> returning SkipInput"
                    ;

                return ProcessorAction::SkipInput;
            }

            auto moduleConfig = eventConfig->modules[state->moduleIndex];
            u32 *moduleHeader = outputBuffer->asU32();
            *moduleHeader = (((u32)moduleConfig->getModuleMeta().typeId) << LF::ModuleTypeShift) & LF::ModuleTypeMask;
            outputBuffer->used += sizeof(u32);
            ++state->eventSize; // increment for the moduleHeader

#ifdef BPDEBUG
            qDebug() << __PRETTY_FUNCTION__
                << "buffer #" << bufferNumber
                << "eventIndex =" << eventIndex
                << "moduleIndex =" << state->moduleIndex
                << "writing moduleHeader @" << moduleHeader
                << "moduleHeader =" << QString::number(*moduleHeader, 16)
                ;
#endif
        }

        s32 moduleIndex = state->moduleIndex;

        while (true)
        {
            u32 data = 0;
            if (state->hasPartialData && eventIter.shortwordsLeft() > 0)
            {
                state->hasPartialData = false;
                data = (eventIter.extractU16() << 16) | state->partialData;
#ifdef BPDEBUG
                qDebug() << __PRETTY_FUNCTION__
                    << "buffer #" << bufferNumber
                    << "eventIndex =" << eventIndex
                    << "moduleIndex =" << state->moduleIndex
                    << "assembled partial data =" << QString::number(data, 16)
                    ;
#endif
            }
            else if (eventIter.longwordsLeft() > 0)
            {
                data = eventIter.extractU32();
            }
            else
            {
#ifdef BPDEBUG
                qDebug() << __PRETTY_FUNCTION__
                    << "buffer #" << bufferNumber
                    << "eventIndex =" << eventIndex
                    << "moduleIndex =" << state->moduleIndex
                    << "breaking out of inner loop because neither partial data usable nor longwords left in input";
#endif
                break;
            }

            // copy data and increment pointers and sizes
            *(outputBuffer->asU32()) = data;
            outputBuffer->used += sizeof(u32);
            ++state->moduleSize;
            ++state->eventSize;

            if (data == EndMarker)
            {
                // The EndMarker for the module data was found. Update the moduleHeader to contain the correct size.
                u32 *moduleHeader = outputBuffer->asU32(state->moduleHeaderOffset);
                *moduleHeader |= (state->moduleSize << LF::SubEventSizeShift) & LF::SubEventSizeMask;

#ifdef BPDEBUG
                qDebug() << __PRETTY_FUNCTION__
                    << "buffer #" << bufferNumber
                    << "eventIndex =" << eventIndex
                    << "moduleIndex =" << state->moduleIndex
                    << "updated moduleHeader @" << moduleHeader
                    << "with size =" << state->moduleSize
                    << "moduleHeader =" << QString::number(*moduleHeader, 16)
                    ;
#endif

                // Update state so that a new module section will be started but keep the moduleIndex.
                state->moduleHeaderOffset = -1;
                break;
            }
        }

        if (!((state->hasPartialData && eventIter.shortwordsLeft() > 0)
              || (eventIter.longwordsLeft() > 0)))
        {
#ifdef BPDEBUG
            qDebug() << __PRETTY_FUNCTION__
                << "buffer #" << bufferNumber
                << "eventIndex =" << eventIndex
                << "moduleIndex =" << state->moduleIndex
                << "breaking out of outer loop because neither partial data useable nor longwords left in input";
#endif
            break;
        }
    }

    // For partial events store a trailing 16-bit data word in the ProcessorState.
    if (partialEvent && eventIter.shortwordsLeft() == 1)
    {
#ifdef BPDEBUG
        qDebug() << __PRETTY_FUNCTION__
            << "buffer #" << bufferNumber
            << "eventIndex =" << eventIndex
            << "moduleIndex =" << state->moduleIndex
            << "moduleHeaderOffset =" << state->moduleHeaderOffset
            << "partialEvent and shortword left in buffer: storing data in state"
            ;
#endif

        state->partialData = eventIter.extractU16();
        state->hasPartialData = true;
    }

#ifdef BPDEBUG
    qDebug() << __PRETTY_FUNCTION__
        << "buffer #" << bufferNumber
        << "eventIndex =" << eventIndex
        << "moduleIndex =" << state->moduleIndex
        << "moduleHeaderOffset =" << state->moduleHeaderOffset
        << "after reading data:"
        << eventIter.bytesLeft() << "bytes left in event iterator"
        ;
#endif

    // read possible end of event markers added by vmusb
    while (eventIter.shortwordsLeft() > 0)
    {
        u16 bufferTerminator = eventIter.peekU16();

        if (bufferTerminator == Buffer::BufferTerminator)
        {
            eventIter.extractU16();
        }
        else
        {
            auto msg = QString(QSL("VMUSB Warning: (buffer #%2) unexpected buffer terminator 0x%1, skipping buffer"))
                .arg(bufferTerminator, 4, 16, QLatin1Char('0'))
                .arg(bufferNumber)
                ;
            logMessage(msg);
#ifdef BPDEBUG
            qDebug() << __PRETTY_FUNCTION__
                << "buffer #" << bufferNumber
                << "eventIndex =" << eventIndex
                << "moduleIndex =" << state->moduleIndex
                << "invalid buffer terminator at end of event:" << QString::number(bufferTerminator, 16)
                << "-> returning SkipInput";
#endif
            return ProcessorAction::SkipInput;
        }
    }

    iter.buffp = eventIter.buffp; // advance the buffer iterator

    if (!partialEvent)
    {
        // Finish the event section in the output buffer: write an EndMarker
        // and update the mvmeEventHeader with the correct size.

        *(outputBuffer->asU32()) = EndMarker;
        outputBuffer->used += sizeof(u32);
        ++state->eventSize;

        u32 *mvmeEventHeader = outputBuffer->asU32(state->eventHeaderOffset);
        u32 backupEventHeader = *mvmeEventHeader;
        *mvmeEventHeader |= (state->eventSize << LF::SectionSizeShift) & LF::SectionSizeMask;

#ifdef BPDEBUG
        qDebug() << __PRETTY_FUNCTION__
            << "buffer #" << bufferNumber
            << "updated mvmeEventHeader @" << mvmeEventHeader
            << "with size =" << state->eventSize
            << "mvmeEventHeader =" << QString::number(*mvmeEventHeader, 16)
            << "backupEventHeader =" << QString::number(backupEventHeader, 16)
            ;
#endif

        if (state->wasPartial)
        {
            // We had a partial event and now processed the last part. This
            // means we do want to flush the output buffer so as to not start
            // processing another possibly partial event.
#ifdef BPDEBUG
            qDebug() << __PRETTY_FUNCTION__
                << "buffer #" << bufferNumber
                << "eventIndex" << eventIndex
                << "wasPartial was set -> returning FlushBuffer"
                ;
#endif
            return ProcessorAction::FlushBuffer;
        }
        else
        {
            // A regular complete event section has been processed. It's ok to
            // process more events and output them to the same buffer.
            if (eventIndex == numberOfEvents - 1)
            {
#ifdef BPDEBUG
                qDebug() << __PRETTY_FUNCTION__
                    << "buffer #" << bufferNumber
                    << "eventIndex" << eventIndex
                    << "numberOfEvents reached -> returning FlushBuffer"
                    ;
#endif
                return ProcessorAction::FlushBuffer;
            }

#ifdef BPDEBUG
            qDebug() << __PRETTY_FUNCTION__
                << "buffer #" << bufferNumber
                << "eventIndex" << eventIndex
                << "numberOfEvents" << numberOfEvents
                << "returning NoneSet"
                ;
#endif
            return ProcessorAction::NoneSet;
        }
    }
    else
    {
#ifdef BPDEBUG
        qDebug() << __PRETTY_FUNCTION__
            << "buffer #" << bufferNumber
            << "eventIndex" << eventIndex
            << "numberOfEvents" << numberOfEvents
            << "setting wasPartial -> returning KeepState"
            ;
#endif

        state->wasPartial = true;
        return ProcessorAction::KeepState;
    }
}

DataBuffer* VMUSBBufferProcessor::getFreeBuffer()
{
    DataBuffer *result = nullptr;

    QMutexLocker lock(&m_freeBufferQueue->mutex);
    if (!m_freeBufferQueue->queue.isEmpty())
        result = m_freeBufferQueue->queue.dequeue();

    return result;
}

DAQStats *VMUSBBufferProcessor::getStats()
{
    return &m_context->getDAQStats();
}

void VMUSBBufferProcessor::logMessage(const QString &message)
{
    m_context->logMessage(message);
}
