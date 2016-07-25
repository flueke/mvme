#include "vmusb_readout_worker.h"
#include "vmusb.h"
#include <QCoreApplication>
#include <QThread>
#include <memory>

using namespace VMUSBConstants;

static void processQtEvents(QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents)
{
    QCoreApplication::processEvents(flags);
}

VMUSBReadoutWorker::VMUSBReadoutWorker(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_context(context)
    , m_readBuffer(new DataBuffer(VMUSBConstants::BufferMaxSize))
{
}

VMUSBReadoutWorker::~VMUSBReadoutWorker()
{
    delete m_readBuffer;
}

void VMUSBReadoutWorker::start(quint32 cycles)
{
    if (m_state != DAQState::Idle)
        return;

    qDebug() << __PRETTY_FUNCTION__ << "cycles =" << cycles;

    m_cyclesToRun = cycles;
    setState(DAQState::Starting);

    try
    {
        auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());

        if (!vmusb) throw "VMUSB controller required";

        m_vmusbStack.resetLoadOffset(); // reset the static load offset

        auto initCommands = VMECommandList();
        auto startCommands = VMECommandList();
        m_stopCommands = VMECommandList();

        int stackID = 2; // start at ID=2 as 0=NIM, 1=scaler

        for (auto event: m_context->getEventConfigs())
        {
            qDebug() << "daq event" << event->name;

            m_vmusbStack = VMUSBStack();
            m_vmusbStack.triggerCondition = event->triggerCondition;
            m_vmusbStack.irqLevel = event->irqLevel;
            m_vmusbStack.irqVector = event->irqVector;

            if (event->triggerCondition == TriggerCondition::Interrupt)
            {
                event->stackID = stackID; // record the stack id in the event structure
                m_vmusbStack.setStackID(stackID);
                ++stackID;
            }

            for (auto module: event->modules)
            {
                qDebug() << "reset module" << module->getName();

                module->resetModule(vmusb);
                module->addInitCommands(&initCommands);
                module->addStartDaqCommands(&startCommands);
                module->addStopDaqCommands(&m_stopCommands);
                m_vmusbStack.addModule(module);
            }

            m_vmusbStack.loadStack(vmusb);
            m_vmusbStack.enableStack(vmusb);
        }

        char buffer[100];

        {
            QString tmp;
            QTextStream strm(&tmp);
            initCommands.dump(strm);
            qDebug() << "init" << endl << tmp << endl;
        }

        // TODO: run init commands one by one to make sure the device has enough time to do its work
        qDebug() << "running init commands";
        vmusb->executeCommands(&initCommands, buffer, sizeof(buffer));

        {
            QString tmp;
            QTextStream strm(&tmp);
            startCommands.dump(strm);
            qDebug() << "start" << endl << tmp << endl;
        }

        qDebug() << "running start commands";
        vmusb->executeCommands(&startCommands, buffer, sizeof(buffer));

        readoutLoop();
    }
    catch (const char *message)
    {
        setError(message);
    }
    catch (const std::runtime_error &e)
    {
        setError(e.what());
    }
}

void VMUSBReadoutWorker::stop()
{
    if (m_state != DAQState::Running)
        return;

    setState(DAQState::Stopping);
    processQtEvents();
}

void VMUSBReadoutWorker::readoutLoop()
{
    setState(DAQState::Running);

    auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());
    vmusb->enterDaqMode();

    int timeout_ms = 2000; // TODO: make this dynamic and dependent on the Bulk Transfer Setup Register timeout

    while (m_state == DAQState::Running)
    {
        processQtEvents();

        m_readBuffer->used = 0;

        int bytesRead = vmusb->bulkRead(m_readBuffer->data, m_readBuffer->size, timeout_ms);

        if (bytesRead > 0)
        {
            m_readBuffer->used = bytesRead;
            processBuffer(m_readBuffer);
        }
        else
        {
            // TODO: error out after a number of successive read errors
        }

        //qDebug() << "cyclesToRun =" << m_cyclesToRun << ", bytesRead =" << bytesRead;

        if (m_cyclesToRun > 0)
        {
            if (m_cyclesToRun == 1)
            {
                break;
            }
            --m_cyclesToRun;
        }
    }

    processQtEvents();

    qDebug() << __PRETTY_FUNCTION__ << "left readoutLoop";
    vmusb->leaveDaqMode();

    int bytesRead = 0;

    do
    {
        m_readBuffer->used = 0;

        bytesRead = vmusb->bulkRead(m_readBuffer->data, m_readBuffer->size, timeout_ms);

        if (bytesRead > 0)
        {
            m_readBuffer->used = bytesRead;
            processBuffer(m_readBuffer);
        }
        else
        {
            qDebug() << "bulkRead returned" << bytesRead;
        }
        processQtEvents();
    } while (bytesRead > 0);

    qDebug() << "running stop commands";
    char buffer[100];
    vmusb->executeCommands(&m_stopCommands, buffer, sizeof(buffer));

    qDebug() << "DAQ state = idle";
    setState(DAQState::Idle);
}

void VMUSBReadoutWorker::setState(DAQState state)
{
    m_state = state;
    emit stateChanged(state);
}

void VMUSBReadoutWorker::setError(const QString &message)
{
    emit error(message);
    setState(DAQState::Idle);
}

void VMUSBReadoutWorker::addFreeBuffer(DataBuffer *buffer)
{
    m_context->getFreeBuffers()->enqueue(buffer);
    //qDebug() << __PRETTY_FUNCTION__ << m_context->getFreeBuffers()->size();
}

DataBuffer* VMUSBReadoutWorker::getFreeBuffer()
{
    auto queue = m_context->getFreeBuffers();

    bool warned = true;
    while (!queue->size() &&
            (m_state == DAQState::Running || m_state == DAQState::Stopping))
    {
        if (!warned)
        {
            qDebug() << "Warning: readout worker buffer queue empty";
            warned = true;
        }
        // Avoid fast spinning by blocking if no events are pending
        processQtEvents(QEventLoop::WaitForMoreEvents);
    }

    return queue->dequeue();
}

void VMUSBReadoutWorker::processBuffer(DataBuffer *readBuffer)
{
    //qDebug() << __PRETTY_FUNCTION__ << ": readBuffer->used =" << readBuffer->used;


    auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());

    BufferIterator::Alignment align = (vmusb->getMode() & GlobalMode::Align32Mask) ?
        BufferIterator::Align32 : BufferIterator::Align16;

    BufferIterator iter(readBuffer->data, readBuffer->used, align);

    std::unique_ptr<DataBuffer> eventBuffer;

    try
    {
        u32 header1 = iter.extractWord();

        bool lastBuffer     = header1 & Buffer::LastBufferMask;
        bool scalerBuffer   = header1 & Buffer::IsScalerBufferMask;
        bool continuousMode = header1 & Buffer::ContinuationMask;
        bool multiBuffer    = header1 & Buffer::MultiBufferMask;
        u16 numberOfEvents  = header1 & Buffer::NumberOfEventsMask;

        if (lastBuffer || scalerBuffer || continuousMode || multiBuffer)
        {
            qDebug("header1: 0x%08x, lastBuffer=%d, scalerBuffer=%d, continuousMode=%d, multiBuffer=%d, numberOfEvents=%u",
                    header1, lastBuffer, scalerBuffer, continuousMode, multiBuffer, numberOfEvents);
        }

        if (vmusb->getMode() & GlobalMode::HeaderOptMask)
        {
            u32 header2 = iter.extractWord();
            u16 numberOfWords = header2 & Buffer::NumberOfWordsMask;
            qDebug("header2: numberOfWords=%u", numberOfWords);
        }

        bool debugOutputDone = false;
        for (u32 eventIndex=0; eventIndex < numberOfEvents; ++eventIndex)
        {
            u32 eventHeader = iter.extractWord();

            u8 stackID          = (eventHeader >> Buffer::StackIDShift) & Buffer::StackIDMask;
            bool partialEvent   = eventHeader & Buffer::ContinuationMask; // TODO: handle this!
            u32 eventLength     = eventHeader & Buffer::EventLengthMask; // in 16-bit words

            if (partialEvent)
            {
                qDebug("event %u, eventHeader=0x%08x, stackID=%u, partialEvent=%d, eventLength=%u",
                        eventIndex, eventHeader, stackID, partialEvent, eventLength);
                qDebug() << "===== Warning: partial event support not implemented! ======";
            }

            eventBuffer.reset(getFreeBuffer());
            eventBuffer->used = 0;

            u16 *bufp = eventBuffer->asU16();

            for (u32 i=0; i<eventLength; ++i)
            {
                u16 data = iter.extractU16();
                *bufp++ = data;
            }

            eventBuffer->used = eventLength * sizeof(u16);
            //qDebug("eventBuffer ready, usedSize=%u, bytes left in readBuffer=%u", eventBuffer->used, iter.bytesLeft());
            emit eventReady(eventBuffer.release());
            //addFreeBuffer(eventBuffer.release());
        }

        if (iter.longwordsLeft())
        {
            u32 bufferTerminator = iter.extractU32();
            if (bufferTerminator != 0xffffffff)
            {
                qDebug("processBuffer() warning, buffer terminator != 0xffffffff");
            }
        }

        if (iter.bytesLeft() != 0)
        {
            qDebug("processBuffer() warning: %u bytes left in buffer!", iter.bytesLeft());
            for (size_t i=0; i<iter.longwordsLeft(); ++i)
                qDebug("  0x%08x", iter.extractU32());

            for (size_t i=0; i<iter.wordsLeft(); ++i)
                qDebug("  0x%04x", iter.extractU16());
        }
    }
    // TODO: handle these errors somehow (terminating readout?)
    catch (const end_of_buffer &)
    {
        qDebug("Error: end of readBuffer reached unexpectedly!");
    }

    if (eventBuffer)
    {
        addFreeBuffer(eventBuffer.release());
    }
}
