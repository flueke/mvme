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
#include "event_server/server/event_server.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>

#include "analysis/a2/a2.h"
#include "analysis/a2_adapter.h"
#include "event_server/common/event_server_proto.h"
#include "event_server/server/event_server_util.h"
#include "git_sha1.h"

using namespace mvme::event_server;

struct EventServer::Private
{
    struct RunContext
    {
        RunInfo runInfo;
        const VMEConfig *vmeConfig = nullptr;
        const analysis::Analysis *analysis = nullptr;
        const analysis::A2AdapterState *adapterState = nullptr;
        const a2::A2 *a2 = nullptr;

        OutputDataDescription outputDescription;

        // Copy of the json structure generated for clients in beginRun().
        // Clients that are connecting during a run will be sent this
        // information.
        json outputInfoJSON;
    };

    struct RunStats
    {
        size_t dataBytesPerClient = 0;
    };

    struct ClientInfo
    {
        std::unique_ptr<QTcpSocket> socket;
    };

    static const size_t InitialOutBufferSize = Kilobytes(10);

    explicit Private(EventServer *q)
        : m_q(q)
        , m_server(q)
        , m_outBuf(InitialOutBufferSize)
        , m_enabled(false)
    { }

    EventServer *m_q;
    QTcpServer m_server;
    std::vector<u8> m_outBuf;
    QHostAddress m_listenAddress = QHostAddress::Any;
    quint16 m_listenPort = EventServer::Default_ListenPort;
    bool m_needRestart = false; // set to true if listening host and/or port are changed
    EventServer::Logger m_logger;
    std::vector<ClientInfo> m_clients;
    bool m_runInProgress = false;
    RunContext m_runContext;
    RunStats m_runStats;
    bool m_enabled;

    void handleNewConnection();
    void handleClientSocketError(QTcpSocket *socket, QAbstractSocket::SocketError error);
    void cleanupClients();
    void logMessage(const QString &msg);
};

namespace
{

enum class WriteOption
{
    None,
    Flush
};

static const int FlushTimeout_ms = 1000;

qint64 write_data(QIODevice &out, const char *data, size_t size)
{
    const char *curPtr = data;
    const char *endPtr = data + size;

    while (curPtr < endPtr)
    {
        qint64 written = out.write(data, size);

        if (written < 0)
        {
            return written;
        }

        curPtr += written;
    }

    assert(curPtr == endPtr);

    return curPtr - data;
}

/* Write a Plain-Old-Datatype to the output device. */
template<typename T>
qint64 write_pod(QIODevice &out, const T &t)
{
    return write_data(out, reinterpret_cast<const char *>(&t), sizeof(T));
}

/* Write header and size information, not contents. */
qint64 write_message_header(QIODevice &out, MessageType type, u32 size)
{
    qint64 result = 0, written = 0;

    if ((written = write_pod(out, type)) < 0) return written; else result += written;
    if ((written = write_pod(out, size)) < 0) return written; else result += written;

    return result;
}

qint64 write_message(QIODevice &out, MessageType type, const char *data, u32 size,
                     WriteOption opt = WriteOption::None)
{
    qint64 result = 0, written = 0;

    if ((written = write_message_header(out, type, size)) < 0)
        return written;
    else
        result += written;

    if ((written = write_data(out, data, size)) < 0)
        return written;
    else
        result += written;

    if (opt == WriteOption::Flush)
        out.waitForBytesWritten(FlushTimeout_ms);

    return result;
}

qint64 write_message(QIODevice &out, MessageType type, const QByteArray &contents,
                     WriteOption opt = WriteOption::None)
{
    return write_message(out, type, contents.data(),
                         static_cast<u32>(contents.size()),
                         opt);
}

} // end anon namespace

void EventServer::Private::handleNewConnection()
{
    if (auto clientSocket = m_server.nextPendingConnection())
    {
        qDebug() << "DataServer: new connection from" << clientSocket->peerAddress()
            << ", new client count =" << m_clients.size() + 1;

        ClientInfo clientInfo = { std::unique_ptr<QTcpSocket>(clientSocket) };

        // ugly cast due to overloaded QAbstractSocket::error() method
        connect(clientInfo.socket.get(),
                static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(
                    &QAbstractSocket::error),
                m_q, [this, clientSocket] (QAbstractSocket::SocketError error) {
                    handleClientSocketError(clientSocket, error);
        });

        // Initial ServerInfo message

        json serverInfo;

        serverInfo["mvme_version"] = std::string(mvme_git_version());
        serverInfo["protocol_version"] = ProtocolVersion;

        auto jsonString = QByteArray::fromStdString(serverInfo.dump());
        write_message(*clientInfo.socket, MessageType::ServerInfo, jsonString, WriteOption::Flush);

        // If a run is in progress immediately send out a BeginRun message to
        // the client. This reuses the information built in beginRun() when the
        // run was started.
        if (m_runInProgress)
        {
            qDebug() << "DataServer: client connected during an active run. Sending"
                " outputInfo.";

            auto outputInfo = m_runContext.outputInfoJSON;
            outputInfo["runInProgress"] = true;

            auto jsonString = QByteArray::fromStdString(outputInfo.dump());

            write_message(*clientSocket, MessageType::BeginRun, jsonString, WriteOption::Flush);
        }

        if (clientInfo.socket->isValid())
        {
            m_clients.emplace_back(std::move(clientInfo));
        }
    }
}

void EventServer::Private::handleClientSocketError(
    QTcpSocket * /*socket*/,
    QAbstractSocket::SocketError /*error*/)
{
    if (!m_runInProgress)
    {
        qDebug() << __PRETTY_FUNCTION__ << "calling cleanupClients()";
        cleanupClients();
    }
}

// remove invalid clients (error, disconnected, etc)
void EventServer::Private::cleanupClients()
{
    qDebug() << __PRETTY_FUNCTION__;

    auto to_be_removed = [] (const Private::ClientInfo &ci) -> bool
    {
        if (!ci.socket) return true;
        if (!ci.socket->isValid()) return true;
        return ci.socket->state() == QAbstractSocket::UnconnectedState;
    };

    for (auto &client: m_clients)
    {
        if (to_be_removed(client))
        {
            if (client.socket)
            {
                qDebug() << __PRETTY_FUNCTION__ << "removing client " << client.socket->peerAddress();
                client.socket->deleteLater();
                (void) client.socket.release();
            }
        }
    }

    // Remove the now stale pointers from the list of clients.
    m_clients.erase(std::remove_if(m_clients.begin(), m_clients.end(), to_be_removed),
                    m_clients.end());

    qDebug() << __PRETTY_FUNCTION__ << ", new client count =" << m_clients.size();
}

void EventServer::Private::logMessage(const QString &msg)
{
    if (m_logger)
    {
        m_logger(QSL("EventServer: ") + msg);
    }
}

EventServer::EventServer(QObject *parent)
    : QObject(parent)
    , m_d(std::make_unique<Private>(this))
{
    connect(&m_d->m_server, &QTcpServer::newConnection,
            this, [this] { m_d->handleNewConnection(); });
}

EventServer::~EventServer()
{
    //qDebug() << "<<< >>> <<< >>>" << __PRETTY_FUNCTION__ << "<<< >>> <<< >>>";
}

void EventServer::startup()
{
    qDebug() << __PRETTY_FUNCTION__ << this << "enabled =" << m_d->m_enabled;
    if (m_d->m_enabled)
    {
        if (!m_d->m_server.isListening())
        {
            if (m_d->m_server.listen(m_d->m_listenAddress, m_d->m_listenPort))
            {
#if 0
                m_d->logMessage(QSL("Listening on %1:%2")
                           .arg(m_d->m_listenAddress.toString())
                           .arg(m_d->m_listenPort));
#endif
            }
            else
            {
                m_d->logMessage(QSL("Error listening on %1:%2")
                           .arg(m_d->m_listenAddress.toString())
                           .arg(m_d->m_listenPort));
            }
        }
    }
    else
    {
        shutdown();
    }
}

void EventServer::shutdown()
{
    m_d->m_server.close();
    m_d->m_clients.clear();
}

void EventServer::setLogger(Logger logger)
{
    m_d->m_logger = logger;
}

StreamConsumerBase::Logger &EventServer::getLogger()
{
    return m_d->m_logger;
}

void EventServer::setListeningInfo(const QHostAddress &address, quint16 port)
{
    if (address != m_d->m_listenAddress || port != m_d->m_listenPort)
        m_d->m_needRestart = true;

    m_d->m_listenAddress = address;
    m_d->m_listenPort = port;
}

bool EventServer::isListening() const
{
    return m_d->m_server.isListening();
}

size_t EventServer::getNumberOfClients() const
{
    return m_d->m_clients.size();
}

void EventServer::setEnabled(bool b)
{
    if (b != m_d->m_enabled || m_d->m_needRestart)
    {
        shutdown();
        m_d->m_enabled = b;
        m_d->m_needRestart = false;
        startup();
    }
}

// Build a description of the datastream that is going to be produced by the
// analysis datasources. Send this description out to clients.
void EventServer::beginRun(const RunInfo &runInfo,
              const VMEConfig *vmeConfig,
              analysis::Analysis *analysis)
{
    if (!m_d->m_enabled) return;

    assert(!m_d->m_runInProgress);

    qDebug() << __PRETTY_FUNCTION__ << "calling cleanupClients()";
    m_d->cleanupClients();

    if (!(analysis->getA2AdapterState() && analysis->getA2AdapterState()->a2))
        return;

    m_d->m_runContext =
    {
        runInfo, vmeConfig, analysis,
        analysis->getA2AdapterState(),
        analysis->getA2AdapterState()->a2,
        {}, {}
    };

    auto &ctx = m_d->m_runContext;
    auto outputDescription = make_output_data_description(vmeConfig, analysis);

    json outputInfo;
    outputInfo["vmeTree"] = to_json(outputDescription.vmeTree);
    outputInfo["eventDataSources"] = to_json(outputDescription.eventDataDescriptions);
    outputInfo["runId"] = ctx.runInfo.runId.toStdString();
    outputInfo["isReplay"] = ctx.runInfo.isReplay;
    outputInfo["runInProgress"] = false;

    // Copy RunInfo::infoDict keys and values into the output info
    for (auto key: runInfo.infoDict.keys())
    {
        outputInfo[key.toStdString()] = runInfo.infoDict[key].toString().toStdString();
    }

    // Store this information so it can be sent out to clients connecting while
    // the DAQ run is in progress.
    m_d->m_runContext.outputDescription = outputDescription;
    m_d->m_runContext.outputInfoJSON = outputInfo;
    m_d->m_runStats = {};

    qDebug() << "EventServer::beginRun: outputInfo to be sent to clients:";
    qDebug().noquote() << QString::fromStdString(outputInfo.dump(2));

    auto jsonString = QByteArray::fromStdString(outputInfo.dump());

    for (auto &client: m_d->m_clients)
    {
        write_message(*client.socket, MessageType::BeginRun, jsonString, WriteOption::Flush);
    }

    m_d->m_runInProgress = true;
}

// Send out event data to clients. At this point the analysis has processed an
// event and extracted module data is available at the a2 datasource outputs.
void EventServer::endEvent(s32 eventIndex)
{
    if (!m_d->m_enabled) return;

    assert(m_d->m_runInProgress);

    if (!m_d->m_runInProgress || !m_d->m_runContext.a2
        || eventIndex < 0 || eventIndex >= a2::MaxVMEEvents)
    {
        InvalidCodePath;
        return;
    }

    if (getNumberOfClients() == 0)
    {
        // Allows QTcpServer to accept new connections
        // FIXME: measure performance without this call. I think this can have
        // a huge impact. If so only perform it based on a timeout.
        QCoreApplication::processEvents();
        return;
    }

    const a2::A2 *a2 = m_d->m_runContext.a2;
    const u32 dataSourceCount = a2->dataSourceCounts[eventIndex];
    const auto &edd = m_d->m_runContext.outputDescription.eventDataDescriptions[eventIndex];

    assert(dataSourceCount == edd.dataSources.size());

    if (!dataSourceCount)
        return;

    while (true)
    {
        try
        {
            using BufferIterator = mvme::event_server::BufferIterator;
            BufferIterator out(m_d->m_outBuf.data(), m_d->m_outBuf.size());

            // Push message type, space for the message size and the eventIndex
            // onto the output buffer:
            // u8  MessageType   -> Part of the header
            // u32 ContentsSize  -> Part of the header
            // u8  eventIndex    -> Part of the contents of an EventData message
            out.push(MessageType::EventData);
            u32 *msgSizePtr = out.push(static_cast<u32>(0u));
            out.push(static_cast<u8>(eventIndex));

            for (size_t dsIndex = 0; dsIndex < edd.dataSources.size(); dsIndex++)
            {
                // For each data source push its index and space for the number
                // of following (index, value) pairs.
                // u8  dataSourceIndex
                // u16 elementCount
                out.push(static_cast<u8>(dsIndex));
                u16 *countPtr = out.push(static_cast<u16>(0u));

                const a2::DataSource *ds = a2->dataSources[eventIndex] + dsIndex;
                // TODO: support multi output data sources
                a2::PipeVectors dataPipe = {};
                dataPipe.data = ds->outputs[0];
                dataPipe.lowerLimits = ds->outputLowerLimits[0];
                dataPipe.upperLimits = ds->outputUpperLimits[0];
                const auto &dsd = edd.dataSources[dsIndex];
                u16 count = 0u; // Count of valid values.

                // Write out the (index, value) pairs for valid parameters
                // using the data types specified in the DataSourceDescription.
                for (s32 paramIndex = 0; paramIndex < dataPipe.size(); paramIndex++)
                {
                    double dParamValue = dataPipe.data[paramIndex];

                    if (a2::is_param_valid(dParamValue))
                    {
                        switch (dsd.indexType)
                        {
                            case StorageType::st_uint8_t:
                                out.push(static_cast<u8>(paramIndex));
                                break;
                            case StorageType::st_uint16_t:
                                out.push(static_cast<u16>(paramIndex));
                                break;
                            case StorageType::st_uint32_t:
                                out.push(static_cast<u32>(paramIndex));
                                break;
                            case StorageType::st_uint64_t:
                                out.push(static_cast<u64>(paramIndex));
                                break;
                        }

                        // Strip the random added by the datasource. Use floor
                        // to make sure we round down in all cases (datasources
                        // do add a random in the range [0, 1)).
                        u64 iParamValue = std::floor(dParamValue);

                        switch (dsd.valueType)
                        {
                            case StorageType::st_uint8_t:
                                out.push(static_cast<u8>(iParamValue));
                                break;
                            case StorageType::st_uint16_t:
                                out.push(static_cast<u16>(iParamValue));
                                break;
                            case StorageType::st_uint32_t:
                                out.push(static_cast<u32>(iParamValue));
                                break;
                            case StorageType::st_uint64_t:
                                out.push(static_cast<u64>(iParamValue));
                                break;
                        }

                        ++count; // cound this valid parameter
                    }
                }

                // write the element count to the buffer
                *countPtr = count;
            }

            u32 contentsBytes = out.asU8() - reinterpret_cast<u8 *>((msgSizePtr + 1));
            *msgSizePtr = contentsBytes;

            for (auto &client: m_d->m_clients)
            {
                if (!client.socket->isValid()) continue;
                write_data(*client.socket, reinterpret_cast<const char *>(out.data),
                           out.used());
            }

            m_d->m_runStats.dataBytesPerClient += out.used();

            break;
        } catch (const mvme::event_server::end_of_buffer &)
        {
            // Ran out of space in the output buffer. Double the buffer size
            // and retry.
            qDebug() << __PRETTY_FUNCTION__ << "doubling buffer size from"
                << m_d->m_outBuf.size() << "to" << m_d->m_outBuf.size() * 2;
            m_d->m_outBuf.resize(m_d->m_outBuf.size() * 2);
        }
    }

    // block if there's enough pending data
    for (auto &client: m_d->m_clients)
    {
        static const qint64 WriteFlushTreshold = Megabytes(10);

        if (client.socket->isValid() && client.socket->bytesToWrite() > WriteFlushTreshold)
        {
            client.socket->waitForBytesWritten();
        }
    }

    // allow QTcpServer to handle new connections
    QCoreApplication::processEvents();
}

void EventServer::endRun(const DAQStats &daqStats, const std::exception * /*e*/)
{
    if (!m_d->m_enabled) return;

    json endRunInfo;
    // FIXME: I think during a replay these contain the current (real time)
    // time values instead of the values from the replay
    //endRunInfo["startTime"] = daqStats.startTime.toString(Qt::ISODate).toStdString();
    //endRunInfo["endTime"] = daqStats.endTime.toString(Qt::ISODate).toStdString();
    endRunInfo["vme_totalBytesRead"] = std::to_string(daqStats.totalBytesRead);
    endRunInfo["vme_totalBuffersRead"] = std::to_string(daqStats.totalBuffersRead);
    endRunInfo["vme_buffersWithErrors"] = std::to_string(daqStats.buffersWithErrors);
    endRunInfo["analysis_droppedBuffers"] = std::to_string(daqStats.droppedBuffers);
    endRunInfo["analysis_processedBuffers"] = std::to_string(daqStats.getAnalyzedBuffers());
    endRunInfo["analysis_efficiency"] = std::to_string(daqStats.getAnalysisEfficiency());

    qDebug() << "EventServer::endRun: endRunInfo to be sent to clients:";
    qDebug().noquote() << QString::fromStdString(endRunInfo.dump(2));

    auto jsonString = QByteArray::fromStdString(endRunInfo.dump());

    for (auto &client: m_d->m_clients)
    {
        if (!client.socket->isValid()) continue;
        write_message(*client.socket, MessageType::EndRun, jsonString, WriteOption::Flush);
    }

    // flush all data on endrun
    for (auto &client: m_d->m_clients)
    {
        while (client.socket->isValid() && client.socket->bytesToWrite() > 0)
            client.socket->waitForBytesWritten();
    }

    m_d->m_runContext = {};
    m_d->m_runInProgress = false;

    qDebug() << __PRETTY_FUNCTION__ << "dataPerClient ="
        << m_d->m_runStats.dataBytesPerClient
        << "bytes, " << m_d->m_runStats.dataBytesPerClient / (1024.0 * 1024.0)
        << "MB";

    m_d->cleanupClients();
}

void EventServer::beginEvent(s32 eventIndex)
{
    Q_UNUSED(eventIndex);
    if (!m_d->m_enabled) return;
    // Noop
    assert(m_d->m_runInProgress);
}

void EventServer::processModuleData(s32 eventIndex, s32 moduleIndex,
                       const u32 *data, u32 size)
{
    Q_UNUSED(eventIndex);
    Q_UNUSED(moduleIndex);
    Q_UNUSED(data);
    Q_UNUSED(size);

    if (!m_d->m_enabled) return;
    // Noop for this server case. We're interested in the endEvent() call as at
    // that point all data from all modules has been processed by the a2
    // analysis system and is available at the output pipes of the data
    // sources.
    assert(m_d->m_runInProgress);
}

void EventServer::processModuleData(s32 crateIndex, s32 eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
{
    Q_UNUSED(crateIndex);
    Q_UNUSED(eventIndex);
    Q_UNUSED(moduleDataList);
    Q_UNUSED(moduleCount);

    if (!m_d->m_enabled) return;
    // Noop for this server case. We're interested in the endEvent() call as at
    // that point all data from all modules has been processed by the a2
    // analysis system and is available at the output pipes of the data
    // sources.
    assert(m_d->m_runInProgress);
}

void EventServer::processTimetick()
{
    if (!m_d->m_enabled) return;
    // TODO: how to handle timeticks? handle them at all?
    assert(m_d->m_runInProgress);
}
