#include "mvlc_readout_worker.h"

#include <QCoreApplication>
#include <QtConcurrent>
#include <QThread>

#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_vme_controller.h"
#include "mvlc/mvlc_util.h"
#include "mvlc_daq.h"
#include "vme_analysis_common.h"

using namespace mesytec::mvlc;

static const size_t LocalEventBufferSize = Megabytes(1);
static const size_t ReadBufferSize = Megabytes(1);

MVLCReadoutWorker::MVLCReadoutWorker(QObject *parent)
    : VMEReadoutWorker(parent)
    , m_state(DAQState::Idle)
    , m_desiredState(DAQState::Idle)
    , m_readBuffer(ReadBufferSize)
    , m_previousData(ReadBufferSize)
    , m_localEventBuffer(LocalEventBufferSize)
{
}

void MVLCReadoutWorker::start(quint32 cycles)
{
    if (m_state != DAQState::Idle)
    {
        logMessage("Readout state != Idle, aborting startup");
        return;
    }

    auto mvlc = qobject_cast<MVLC_VMEController *>(getContext().controller);

    if (!mvlc)
    {
        logMessage("MVLC controller required");
        InvalidCodePath;
        return;
    }

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

    setState(DAQState::Starting);

    m_cyclesToRun = cycles;
    m_logBuffers = (cycles > 0); // log buffers to GUI if number of cycles has been passed in

    auto logger = [this](const QString &msg)
    {
        this->logMessage(msg);
    };

    try
    {
        auto results = vme_daq_init(getContext().vmeConfig, mvlc, logger);
        log_errors(results, logger);

        logMessage("Initializing MVLC");

        if (auto ec = setup_mvlc(*mvlc->getMVLCObject(), *getContext().vmeConfig, logger))
            throw ec;

        if (mvlc->getMVLCObject()->connectionType() == ConnectionType::ETH)
        {
            logMessage(QSL("Connection type is UDP. Sending initial empty request"
                           " using the data socket."));

            static const std::array<u32, 2> EmptyRequest = { 0xF1000000, 0xF2000000 };
            size_t bytesTransferred = 0;

            if (auto ec = mvlc->getMVLCObject()->write(
                    Pipe::Data,
                    reinterpret_cast<const u8 *>(EmptyRequest.data()),
                    EmptyRequest.size() * sizeof(u32),
                    bytesTransferred))
            {
                throw ec;
            }
        }

        // build a structure for faster access to vme config data
        {
            auto lst = m_workerContext.vmeConfig->getEventConfigs();
            m_events.clear();
            m_events.reserve(lst.size());

            for (const auto &eventConfig: m_workerContext.vmeConfig->getEventConfigs())
            {
                EventWithModules ewm;
                ewm.event = eventConfig;
                auto modules = eventConfig->getModuleConfigs();

                for (const auto &moduleConfig: eventConfig->getModuleConfigs())
                {
                    ewm.modules.push_back(moduleConfig);
                    ewm.moduleTypes.push_back(moduleConfig->getModuleMeta().typeId);
                }

                m_events.push_back(ewm);
            }
        }


        logMessage("");
        logMessage(QSL("Entering readout loop"));
        m_workerContext.daqStats->start();
        m_rdoState = {};
        m_listfileHelper = std::make_unique<DAQReadoutListfileHelper>(m_workerContext);
        m_listfileHelper->beginRun();

        // Keep this after DAQReadoutListfileHelper::beginRun() so that the
        // real output filename is available.
        preReadout();

        readoutLoop();

        postReadout();

        logMessage(QSL("Leaving readout loop"));
        logMessage(QSL(""));

        vme_daq_shutdown(getContext().vmeConfig, mvlc, logger);

        // Note: endRun() collects the log contents, which means it should be one of the
        // last actions happening in here. Log messages generated after this point won't
        // show up in the listfile.
        m_listfileHelper->endRun();
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

void MVLCReadoutWorker::preReadout()
{
    QVariantMap controllerSettings = m_workerContext.vmeConfig->getControllerSettings();

    if (controllerSettings.value("WriteRawBufferFile").toBool())
    {
        auto mvlc = qobject_cast<MVLC_VMEController *>(getContext().controller);
        assert(mvlc);

        const char *prefix = nullptr;

        switch (mvlc->getMVLCObject()->connectionType())
        {
            case ConnectionType::USB:
                prefix = "mvlc_usb_";
                break;

            case ConnectionType::ETH:
                prefix = "mvlc_eth_";
                break;
        }

        QString filename = prefix
            + generate_output_basename(*m_workerContext.listfileOutputInfo)
            + ".bin";

        m_rawBufferOut.setFileName(filename);
        if (!m_rawBufferOut.open(QIODevice::WriteOnly))
        {
            auto msg = (QString("Error opening MVLC raw buffers file for writing: %1")
                        .arg(m_rawBufferOut.errorString()));
            logMessage(msg);
        }
        else
        {
            auto msg = (QString("Writing raw MVLC buffers to %1")
                        .arg(m_rawBufferOut.fileName()));
            logMessage(msg);
        }
    }
}

void MVLCReadoutWorker::postReadout()
{
    if (m_rawBufferOut.isOpen())
    {
        logMessage(QString("Closing MVLC raw buffers file %1")
                    .arg(m_rawBufferOut.fileName()));
        m_rawBufferOut.close();
    }
}

void MVLCReadoutWorker::readoutLoop()
{
    using vme_analysis_common::TimetickGenerator;

    static const int LogInterval_ReadError_ms = 5000;
    QTime logReadErrorTimer;
    u32 readErrorCount = 0;
    auto mvlc = qobject_cast<MVLC_VMEController *>(getContext().controller);
    assert(mvlc);

    setState(DAQState::Running);

    TimetickGenerator timetickGen;
    m_listfileHelper->writeTimetickSection();
    logReadErrorTimer.start();

    while (true)
    {
        int elapsedSeconds = timetickGen.generateElapsedSeconds();

        while (elapsedSeconds >= 1)
        {
            m_listfileHelper->writeTimetickSection();
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

            if (ec && ec != MVLCErrorCode::NeedMoreData)
            {
                maybePutBackBuffer();

                readErrorCount++;

                if (bytesTransferred == 0 && logReadErrorTimer.elapsed() >= LogInterval_ReadError_ms)
                {
                    logMessage(QSL("MVLC Readout Warning: received no data"
                                   " with the past %1 read operations").arg(readErrorCount),
                               false);

                    if (ec)
                    {
                        logMessage(QSL("MVLC Readout Warning: last read error: %1")
                                   .arg(ec.message().c_str()), false);
                    }

                    logReadErrorTimer.restart();
                    readErrorCount = 0;
                }
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
            static const double PauseMaxSleep_ms = 125.0;
            QThread::msleep(std::min(PauseMaxSleep_ms, timetickGen.getTimeToNextTick_ms()));
        }
        else
        {
            InvalidCodePath;
        }
    }

    setState(DAQState::Stopping);

    auto f = QtConcurrent::run([&mvlc]()
    {
        return disable_all_triggers(*mvlc->getMVLCObject());
    });

    size_t bytesTransferred = 0u;
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

// TODO MVLCReadoutWorker:
// - keep track of stats
// - better error codes for data consistency checks
// - test performance implications of buffered vs unbuffered reads
// - support other structures than just block reads. This means inside F3
//   buffers other contents than F5 sections are allowed.
//   The data could just be copied over but that does not allow for consistency checks.
std::error_code MVLCReadoutWorker::readAndProcessBuffer(size_t &bytesTransferred)
{
    auto mvlc = qobject_cast<MVLC_VMEController *>(getContext().controller)->getMVLCObject();

    bytesTransferred = 0u;
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
    m_rdoState.streamWriter.setOutputBuffer(outputBuffer);

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
        m_outputBuffer = outputBuffer;
    }

    return outputBuffer;
}

void MVLCReadoutWorker::maybePutBackBuffer()
{
    if (m_outputBuffer && m_outputBuffer != &m_localEventBuffer)
    {
        // We still hold onto one of the buffers obtained from the free queue.
        // This can happen for the SkipInput case. Put the buffer back into the
        // free queue.
        enqueue(m_workerContext.freeBuffers, m_outputBuffer);
    }

    m_outputBuffer = nullptr;
}

void MVLCReadoutWorker::flushCurrentOutputBuffer()
{
    auto outputBuffer = m_outputBuffer;

    if (outputBuffer)
    {
        m_listfileHelper->writeBuffer(outputBuffer);

        if (outputBuffer != &m_localEventBuffer)
        {
            enqueue_and_wakeOne(m_workerContext.fullBuffers, outputBuffer);
        }
        else
        {
            m_workerContext.daqStats->droppedBuffers++;
        }
        m_outputBuffer = nullptr;
        m_rdoState.streamWriter.setOutputBuffer(nullptr);
    }
}

void MVLCReadoutWorker::pauseDAQ()
{
    auto mvlc = qobject_cast<MVLC_VMEController *>(getContext().controller);
    assert(mvlc);

    disable_all_triggers(*mvlc->getMVLCObject());

    size_t bytesTransferred = 0u;

    do
    {
        readAndProcessBuffer(bytesTransferred);
    } while (bytesTransferred > 0);


    m_listfileHelper->writePauseSection();

    setState(DAQState::Paused);
    logMessage(QString(QSL("MVLC readout paused")));
}

void MVLCReadoutWorker::resumeDAQ()
{
    auto mvlc = qobject_cast<MVLC_VMEController *>(getContext().controller);
    assert(mvlc);

    enable_triggers(*mvlc->getMVLCObject(), *getContext().vmeConfig);

    m_listfileHelper->writeResumeSection();

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
