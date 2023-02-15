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
#include "vmusb_buffer_processor.h"

#include <memory>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include <quazipfile.h>
#include <quazip.h>

#include "mvme_stream_util.h"
#include "vme_daq.h"
#include "vmusb.h"
#include "vmusb_util.h"

using namespace vmusb_constants;

//#define BPDEBUG
//#define WRITE_BUFFER_LOG

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
 * our MVME format (see mvme_listfile.h for details). Partial events are
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

/* Structure to keep track of the current event in case of a partial event
 * spanning multiple buffers.
 * Offsets are used instead of pointers as the buffer might have to be resized
 * which can invalidate pointers into it.
 */
struct ProcessorState
{
    MVMEStreamWriterHelper streamWriter;

    s32 stackID = -1;                   // stack id of the current event or -1 if no event is "in progress".
    s32 moduleIndex = -1;               // index into the list of eventconfigs or -1 if no module is "in progress"

    /* VMUSB uses 16-bit words internally which means it can split our 32-bit
     * aligned module data words in case of partial events. If this is the case
     * the partial 16-bits are stored here and reused when the next buffer
     * starts.
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
    VMUSBReadoutWorker *m_readoutWorker = nullptr;
    std::unique_ptr<DAQReadoutListfileHelper> m_listfileHelper;

#ifdef WRITE_BUFFER_LOG
    QFile *m_bufferLogFile = nullptr;
#endif

    ProcessorState m_state;
    DataBuffer *m_outputBuffer = nullptr;

    DataBuffer *getOutputBuffer();
};

static const size_t LocalEventBufferSize    = 27 * 1024 * 2;

VMUSBBufferProcessor::VMUSBBufferProcessor(VMUSBReadoutWorker *parent)
    : QObject(parent)
    , m_d(new VMUSBBufferProcessorPrivate)
    , m_localEventBuffer(27 * 1024 * 2)
{
    m_d->m_q = this;
    m_d->m_readoutWorker = parent;
}

VMUSBBufferProcessor::~VMUSBBufferProcessor()
{
    delete m_d;
}

void VMUSBBufferProcessor::beginRun()
{
    Q_ASSERT(m_freeBufferQueue);
    Q_ASSERT(m_filledBufferQueue);
    Q_ASSERT(m_d->m_outputBuffer == nullptr);

    m_vmusb = qobject_cast<VMUSB *>(m_d->m_readoutWorker->getContext().controller);

    if (!m_vmusb)
    {
        /* This should not happen but ensures that m_vmusb is set when
         * processBuffer() will be called later on. */
        throw QString(QSL("Error from VMUSBBufferProcessor: no VMUSB present!"));
    }

    resetRunState();

    m_d->m_listfileHelper = std::make_unique<DAQReadoutListfileHelper>(
        m_d->m_readoutWorker->getContext());
    m_d->m_listfileHelper->beginRun();

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
        enqueue(m_freeBufferQueue, m_d->m_outputBuffer);
        m_d->m_outputBuffer = nullptr;
    }

#ifdef WRITE_BUFFER_LOG
    delete m_d->m_bufferLogFile;
    m_d->m_bufferLogFile = nullptr;
#endif

    m_d->m_listfileHelper->endRun();
}

void VMUSBBufferProcessor::handlePause()
{
    m_d->m_listfileHelper->writePauseSection();
}

void VMUSBBufferProcessor::handleResume()
{
    m_d->m_listfileHelper->writeResumeSection();
}

void VMUSBBufferProcessor::resetRunState()
{
    auto eventConfigs = m_d->m_readoutWorker->getContext().vmeConfig->getEventConfigs();
    m_eventConfigByStackID.clear();

    for (auto config: eventConfigs)
    {
        m_eventConfigByStackID[config->stackID] = config;
    }

    m_d->m_state = ProcessorState();
}

void VMUSBBufferProcessor::warnIfStreamWriterError(u64 bufferNumber, int writerFlags, u16 eventIndex)
{
    if (writerFlags & MVMEStreamWriterHelper::ModuleSizeExceeded)
    {
        auto msg = (QString(QSL("VMUSB Warning: (buffer #%1) maximum module data size exceeded. "
                                "Data will be truncated! (eventIndex=%2)"))
                    .arg(bufferNumber)
                    .arg(eventIndex));
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;
    }

    if (writerFlags & MVMEStreamWriterHelper::EventSizeExceeded)
    {
        auto msg = (QString(QSL("VMUSB Warning: (buffer #%1) maximum event section size exceeded. "
                                "Data will be truncated! (eventIndex=%2)"))
                    .arg(bufferNumber)
                    .arg(eventIndex));
        logMessage(msg);
        qDebug() << __PRETTY_FUNCTION__ << msg;
    }
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

        u16 numberOfEvents  = header1 & Buffer::NumberOfEventsMask;

#ifdef BPDEBUG
        bool lastBuffer     = header1 & Buffer::LastBufferMask;
        bool scalerBuffer   = header1 & Buffer::IsScalerBufferMask;
        bool continuousMode = header1 & Buffer::ContinuationMask;
        bool multiBuffer    = header1 & Buffer::MultiBufferMask;
        qDebug("%s buffer #%lu, buffer_size=%lu, header1: 0x%08x, lastBuffer=%d"
               ", scalerBuffer=%d, continuousMode=%d, multiBuffer=%d, numberOfEvents=%u",
               __PRETTY_FUNCTION__,
               (long unsigned) bufferNumber, readBuffer->used, header1, lastBuffer, scalerBuffer,
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
                Q_ASSERT(m_d->m_listfileHelper);
                m_d->m_listfileHelper->writeBuffer(outputBuffer);

                if (outputBuffer != &m_localEventBuffer)
                {
                    // It's not the local buffer -> put it into the queue of filled buffers
                    enqueue_and_wakeOne(m_filledBufferQueue, outputBuffer);
                }
                else
                {
                    getStats()->droppedBuffers++;
                }

                m_d->m_outputBuffer = outputBuffer = nullptr;
                state->streamWriter.setOutputBuffer(nullptr);
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
                /* Extra test to suppress a warning about 0xffff being left in
                 * the buffer. This happens only on some systems and/or with
                 * some VMUSBs, and it does not lead to any other errors, so
                 * the warning can be suppressed here. */
                if (iter.bytesLeft() == 2 && iter.peekU16() == 0xffff)
                {
                    return;
                }

                auto msg = QString(QSL("VMUSB Warning: (buffer #%3) %1 bytes left in buffer, numberOfEvents=%2"))
                    .arg(iter.bytesLeft())
                    .arg(numberOfEvents)
                    .arg(bufferNumber)
                    ;
                logMessage(msg);

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
        enqueue(m_freeBufferQueue, outputBuffer);
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
    if (iter.shortwordsLeft() < 1)
    {
        auto msg = QString(QSL("VMUSB Error: (buffer #%1) processEvent(): end of buffer when extracting event header"))
            .arg(bufferNumber);
        logMessage(msg, true);
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
        logMessage(msg, true);
        qDebug() << __PRETTY_FUNCTION__ << msg;
        return ProcessorAction::SkipInput;
    }

    if (stackID > StackIDMax)
    {
        auto msg = QString(QSL("VMUSB: (buffer #%2) Parsed stackID=%1 is out of range, skipping buffer"))
            .arg(stackID)
            .arg(bufferNumber);
        logMessage(msg, true);
        qDebug() << __PRETTY_FUNCTION__ << msg;
        return ProcessorAction::SkipInput;
    }

    if (!m_eventConfigByStackID.contains(stackID))
    {
        auto msg = QString(QSL("VMUSB: (buffer #%3) No event config for stackID=%1, eventLength=%2, skipping event"))
            .arg(stackID)
            .arg(eventLength)
            .arg(bufferNumber);
        logMessage(msg, true);
        qDebug() << __PRETTY_FUNCTION__ << msg;
        iter.skip(sizeof(u16), eventLength);
        return ProcessorAction::KeepState;
    }

    /* Create a local iterator limited by the event length. A check above made
     * sure that the event length does not exceed the inputs size. */
    BufferIterator eventIter(iter.buffp, eventLength * sizeof(u16), iter.alignment);

    // ensure double the event length is available
    outputBuffer->ensureFreeSpace(eventLength * sizeof(u16) * 2);

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

    auto moduleConfigs = eventConfig->getModuleConfigs();
    const s32 moduleCount = moduleConfigs.size();
    int writerFlags = 0;

    if (moduleCount == 0)
    {
        auto msg = QString(QSL("VMUSB: (buffer #%1) No module configs for stackID=%2, skipping event"))
            .arg(bufferNumber)
            .arg(stackID);

        logMessage(msg, true);
        qDebug() << __PRETTY_FUNCTION__ << msg << "-> skipping event and returning NoneSet";

        iter.skip(sizeof(u16), eventLength);
        return ProcessorAction::NoneSet;
    }

    if (state->stackID < 0)
    {
        int configEventIndex = m_d->m_readoutWorker->getContext().vmeConfig->getEventConfigs().indexOf(eventConfig);

        *state = ProcessorState();
        state->stackID = stackID;;
        state->wasPartial = partialEvent;
        state->streamWriter.setOutputBuffer(outputBuffer);
        state->streamWriter.openEventSection(configEventIndex);
    }
    else
    {
        if (state->stackID != stackID)
        {
            Q_ASSERT(state->wasPartial);
            auto msg = QString(QSL("VMUSB: (buffer #%1) Got differing stackIDs during partial event processing"))
                       .arg(bufferNumber);
            logMessage(msg, true);
            qDebug() << __PRETTY_FUNCTION__ << msg << "returning SkipInput";
            return ProcessorAction::SkipInput;
        }
    }

    while (true)
    {
        if (!state->streamWriter.hasOpenModuleSection())
        {
            // Have to write a new module header

            if (state->moduleIndex < 0)
            {
                state->moduleIndex = 0;
            }
            else
            {
                state->moduleIndex++;
            }

            if (state->moduleIndex >= moduleCount)
            {
                logMessage(QString(QSL("VMUSB: (buffer #%1) Module index %2 is out of range while parsing input. Skipping buffer"))
                    .arg(bufferNumber)
                    .arg(state->moduleIndex),
                    true);

                qDebug() << __PRETTY_FUNCTION__
                    << "buffer #" << bufferNumber
                    << "eventIndex =" << eventIndex
                    << "moduleIndex =" << state->moduleIndex
                    << "moduleIndex is out of range -> returning SkipInput"
                    ;

                return ProcessorAction::SkipInput;
            }

            auto moduleConfig = moduleConfigs[state->moduleIndex];
            writerFlags |= state->streamWriter.openModuleSection((u32)moduleConfig->getModuleMeta().typeId);
        }

        Q_ASSERT(state->moduleIndex >= 0);
        Q_ASSERT(state->streamWriter.moduleHeaderOffset() >= 0);

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

            // copy module data to output
            if (state->streamWriter.hasOpenModuleSection())
            {
                writerFlags |= state->streamWriter.writeModuleData(data);
            }

            if (data == EndMarker)
            {
                if (state->streamWriter.hasOpenModuleSection())
                {
                    state->streamWriter.closeModuleSection();
                }

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
            logMessage(msg, true);
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
        writerFlags |= state->streamWriter.writeEventData(EndMarker);
        state->streamWriter.closeEventSection();
        warnIfStreamWriterError(bufferNumber, writerFlags, eventIndex);

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
    return dequeue(m_freeBufferQueue);
}

DAQStats *VMUSBBufferProcessor::getStats()
{
    return &m_d->m_readoutWorker->getContext().daqStats;
}

void VMUSBBufferProcessor::logMessage(const QString &message, bool useThrottle)
{
    m_d->m_readoutWorker->getContext().logMessage(message, useThrottle);
}

void VMUSBBufferProcessor::logMessage(const QString &message)
{
    m_d->m_readoutWorker->getContext().logMessage(message);
}

/* Called every second by VMUSBReadoutWorker. Generates a SectionType_Timetick
 * section and writes it to the listfile. Tries to use a buffer from the free
 * queue, if none is available the m_localTimetickBuffer will be used instead.
 *
 * Note: This must not reuse m_localEventBuffer and it must not use
 * getOutputBuffer() to obtain a free buffer as partial event handling might be
 * in the process of building up a complete event using one of those buffers.
 */
void VMUSBBufferProcessor::timetick()
{
    #ifdef BPDEBUG
    qDebug() << __PRETTY_FUNCTION__ << QTime::currentTime() << "timetick";
    #endif

    Q_ASSERT(m_d->m_listfileHelper);
    m_d->m_listfileHelper->writeTimetickSection();
}
