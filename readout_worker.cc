#include "readout_worker.h"
#include "vmusb.h"
#include <QCoreApplication>
#include <QThread>

static void processEvents(QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents)
{
    QCoreApplication::processEvents(flags);
}

ReadoutWorker::ReadoutWorker(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_context(context)
{
}

void ReadoutWorker::start(quint32 cycles)
{
    if (m_state != DAQState::Idle)
        return;

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
        int stackID = 2;

        for (auto event: m_context->getEventConfigs())
        {
            m_vmusbStack = VMUSBStack();
            m_vmusbStack.triggerCondition = event->triggerCondition;
            m_vmusbStack.irqLevel = event->irqLevel;
            m_vmusbStack.irqVector = event->irqVector;
            m_vmusbStack.setStackID(stackID++);

            for (auto module: event->modules)
            {
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

        vmusb->executeCommands(&initCommands, buffer, sizeof(buffer));

        {
            QString tmp;
            QTextStream strm(&tmp);
            startCommands.dump(strm);
            qDebug() << "start" << endl << tmp << endl;
        }

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

void ReadoutWorker::stop()
{
    if (m_state != DAQState::Running)
        return;

    setState(DAQState::Stopping);
    processEvents();
}

void ReadoutWorker::readoutLoop()
{
    setState(DAQState::Running);

    auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());
    vmusb->enterDaqMode();

    char buffer[32*1024];
    int timeout_ms = 2000;
    DataBufferQueue *bufferQueue = m_context->getFreeBuffers();

    while (m_state == DAQState::Running)
    {
        processEvents();

        auto buffer = getFreeBuffer();
        buffer->used = 0;

        int bytesRead = vmusb->bulkRead(buffer->data, buffer->size, timeout_ms);

        if (bytesRead > 0)
        {
            buffer->used = bytesRead;
            emit bufferRead(buffer);
        }
        else
        {
            qDebug() << "bulkRead returned" << bytesRead;
            addFreeBuffer(buffer);
        }

        qDebug() << "cyclesToRun =" << m_cyclesToRun << ", bytesRead =" << bytesRead;

        if (m_cyclesToRun > 0)
        {
            if (m_cyclesToRun == 1)
            {
                break;
            }
            --m_cyclesToRun;
        }
    }

    processEvents();

    qDebug() << __PRETTY_FUNCTION__ << "left readoutLoop";
    vmusb->leaveDaqMode();

    int bytesRead = 0;

    do
    {
        auto buffer = getFreeBuffer();
        buffer->used = 0;

        bytesRead = vmusb->bulkRead(buffer->data, buffer->size, timeout_ms);

        if (bytesRead > 0)
        {
            buffer->used = bytesRead;
            emit bufferRead(buffer);
        } 
        else
        {
            qDebug() << "bulkRead returned" << bytesRead;
            addFreeBuffer(buffer);
        }
        processEvents();
    } while (bytesRead > 0);

    vmusb->executeCommands(&m_stopCommands, buffer, sizeof(buffer));

    setState(DAQState::Idle);
}

void ReadoutWorker::setState(DAQState state)
{
    m_state = state;
    emit stateChanged(state);
}

void ReadoutWorker::setError(const QString &message)
{
    emit error(message);
    setState(DAQState::Idle);
}

void ReadoutWorker::addFreeBuffer(DataBuffer *buffer)
{
    m_context->getFreeBuffers()->enqueue(buffer);
    qDebug() << __PRETTY_FUNCTION__ << m_context->getFreeBuffers()->size();
}

DataBuffer* ReadoutWorker::getFreeBuffer()
{
    auto queue = m_context->getFreeBuffers();

    while (!queue->size() &&
            (m_state == DAQState::Running || m_state == DAQState::Stopping))
    {
        // Avoid fast spinning by blocking if no events are pending
        processEvents(QEventLoop::WaitForMoreEvents);
    }

    return queue->dequeue();
}
