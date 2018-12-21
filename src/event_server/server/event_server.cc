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
#include "event_server/common/event_server_lib.h"
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

        // Copy of the structure generated for clients in beginRun(). Clients
        // that are connecting during a run will be sent this information.
        json outputInfo;
    };

    struct RunStats
    {
        size_t dataBytesPerClient = 0;
    };

    struct ClientInfo
    {
        std::unique_ptr<QTcpSocket> socket;
    };

    Private(EventServer *q)
        : m_q(q)
        , m_server(q)
    { }

    EventServer *m_q;
    QTcpServer m_server;
    QHostAddress m_listenAddress = QHostAddress::Any;
    quint16 m_listenPort = EventServer::Default_ListenPort;
    EventServer::Logger m_logger;
    std::vector<ClientInfo> m_clients;
    bool m_runInProgress = false;
    RunContext m_runContext;
    RunStats m_runStats;

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

/* Write a Plain-Old-Data type to the output device. */
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

        // ugly connect due to overloaded QAbstractSocket::error() method
        connect(clientInfo.socket.get(),
                static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(
                    &QAbstractSocket::error),
                m_q, [this, clientSocket] (QAbstractSocket::SocketError error) {
                    handleClientSocketError(clientSocket, error);
        });

        // Initial ServerInfo message

        json serverInfo;

        serverInfo["mvme_version"] = std::string(GIT_VERSION);
        serverInfo["protocol_version"] = ProtocolVersion;

        auto jsonString = QByteArray::fromStdString(serverInfo.dump());
        write_message(*clientInfo.socket, MessageType::ServerInfo, jsonString, WriteOption::Flush);

        // If a run is in progress immediately send out a BeginRun message to
        // the client. This reuses the information built in beginRun().
        if (m_runInProgress)
        {
            qDebug() << "DataServer: client connected during an active run. Sending"
                " outputInfo.";

            auto outputInfo = m_runContext.outputInfo;
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

void EventServer::Private::handleClientSocketError(QTcpSocket *socket,
                                                          QAbstractSocket::SocketError error)
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
                client.socket.release();
            }
        }
    }

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

EventServer::EventServer(Logger logger, QObject *parent)
    : EventServer(parent)
{
    setLogger(logger);
}

EventServer::~EventServer()
{}

void EventServer::startup()
{
    if (bool res = m_d->m_server.listen(m_d->m_listenAddress, m_d->m_listenPort))
    {
        m_d->logMessage(QSL("Listening on %1:%2")
                   .arg(m_d->m_listenAddress.toString())
                   .arg(m_d->m_listenPort));
    }
    else
    {
        m_d->logMessage(QSL("Error listening on %1:%2")
                   .arg(m_d->m_listenAddress.toString())
                   .arg(m_d->m_listenPort));
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

void EventServer::setListeningInfo(const QHostAddress &address, quint16 port)
{
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

void EventServer::beginRun(const RunInfo &runInfo,
              const VMEConfig *vmeConfig,
              const analysis::Analysis *analysis,
              Logger logger)
{
    assert(!m_d->m_runInProgress);

    qDebug() << __PRETTY_FUNCTION__ << "calling cleanupClients()";
    m_d->cleanupClients();

    if (!(analysis->getA2AdapterState() && analysis->getA2AdapterState()->a2))
        return;

    setLogger(logger);

    m_d->m_runContext =
    {
        runInfo, vmeConfig, analysis,
        analysis->getA2AdapterState(),
        analysis->getA2AdapterState()->a2
    };

    auto &ctx = m_d->m_runContext;

    auto outputDescr = make_output_data_description(vmeConfig, analysis);
    json outputInfo;
    outputInfo["vmeTree"] = to_json(outputDescr.vmeTree);
    outputInfo["eventDataSources"] = to_json(outputDescr.eventDataDescriptions);

    for (auto key: runInfo.infoDict.keys())
    {
        outputInfo[key.toStdString()] = runInfo.infoDict[key].toString().toStdString();
    }

    outputInfo["runId"] = ctx.runInfo.runId.toStdString();
    outputInfo["isReplay"] = ctx.runInfo.isReplay;
    outputInfo["runInProgress"] = false;

    // Store this information so it can be sent out to clients connecting while
    // the DAQ run is in progress.
    m_d->m_runContext.outputInfo = outputInfo;
    m_d->m_runStats = {};

    qDebug() << __PRETTY_FUNCTION__ << "outputInfo to be sent to clients:";
    qDebug().noquote() << QString::fromStdString(outputInfo.dump(2));

    auto jsonString = QByteArray::fromStdString(outputInfo.dump());

    for (auto &client: m_d->m_clients)
    {
        write_message(*client.socket, MessageType::BeginRun, jsonString, WriteOption::Flush);
    }

    m_d->m_runInProgress = true;
}

void EventServer::endEvent(s32 eventIndex)
{
    assert(m_d->m_runInProgress);

    if (!m_d->m_runContext.a2 || eventIndex < 0 || eventIndex >= a2::MaxVMEEvents)
    {
        InvalidCodePath;
        return;
    }

    const a2::A2 *a2 = m_d->m_runContext.a2;
    const u32 dataSourceCount = a2->dataSourceCounts[eventIndex];

    if (getNumberOfClients() == 0)
    {
        // allow QTcpServer to handle new connections
        QCoreApplication::processEvents();
        return;
    }

    if (!dataSourceCount)
        return;

    // XXX: leftoff here. TODO: write out the new indexed format using the
    // smallest data type possible for the value and always uint16_t for the
    // index. Create and use a local buffer, fill it, put the final size at the
    // start and transmit the buffer. This is very similar to listfile
    // generation.

    // pre calculate the output message size. TODO: this should be cached.
    // TODO: send out a sequence number so that clients can figure out how many
    // events they missed so far.
    u32 msgSize = sizeof(u32); // eventIndex

    for (u32 dsIndex = 0; dsIndex < dataSourceCount; dsIndex++)
    {
        auto ds = a2->dataSources[eventIndex] + dsIndex;

        // data source index, length of the data source output in bytes
        msgSize += sizeof(u32) + sizeof(u32);
        // size of the output * sizeof(double)
        msgSize += ds->output.size() * ds->output.data.element_size;
        m_d->m_runStats.dataBytesPerClient += ds->output.size() * ds->output.data.element_size;
    }

    // Write out the message header and calculated size, followed by the
    // eventindex to each client socket

    for (auto &client: m_d->m_clients)
    {
        if (!client.socket->isValid()) continue;

        write_message_header(*client.socket, MessageType::EventData, msgSize);
        // TODO: write out an event sequence number
        write_pod(*client.socket, static_cast<u32>(eventIndex));
    }

    // Iterate through module data sources and send out the extracted values to
    // each connected client.
    // Format is:
    // MessageType::EventData
    // u32 eventIndex
    // for each dataSource:
    //  u32 dataSourceIndex
    //  u32 data size in bytes
    //  data values (doubles) from the data pipe

    for (u32 dsIndex = 0; dsIndex < dataSourceCount; dsIndex++)
    {
        a2::DataSource *ds = a2->dataSources[eventIndex] + dsIndex;
        const a2::PipeVectors &dataPipe = ds->output;

        auto dBegin = reinterpret_cast<const char *>(dataPipe.data.begin());
        auto dEnd   = reinterpret_cast<const char *>(dataPipe.data.end());
        const u32 dataBytesToWrite = dEnd - dBegin;

        for (auto &client: m_d->m_clients)
        {
            if (!client.socket->isValid()) continue;

            // Note: technically the size does not need to be transmitted
            // again. The client got the information about the indiviudal
            // outputs sizes in the BeginRun message. This information is here
            // for consistency checks only.
            write_pod(*client.socket, dsIndex);
            write_pod(*client.socket, dataBytesToWrite);
            write_data(*client.socket, dBegin, dataBytesToWrite);
        }
    }

    // block if there's enough pending data
    for (auto &client: m_d->m_clients)
    {
        static const qint64 WriteFlushTreshold = Kilobytes(128);

        if (client.socket->isValid() && client.socket->bytesToWrite() > WriteFlushTreshold)
        {
            client.socket->waitForBytesWritten();
        }
    }

    // allow QTcpServer to handle new connections
    QCoreApplication::processEvents();
}

void EventServer::endRun(const std::exception *e)
{
    for (auto &client: m_d->m_clients)
    {
        if (!client.socket->isValid()) continue;
        write_message(*client.socket, MessageType::EndRun, {});
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
    // Noop
    assert(m_d->m_runInProgress);
}

void EventServer::processModuleData(s32 eventIndex, s32 moduleIndex,
                       const u32 *data, u32 size)
{
    // Noop for this server case. We're interested in the endEvent() call as at
    // that point all data from all modules has been processed by the a2
    // analysis system and is available at the output pipes of the data
    // sources.
    assert(m_d->m_runInProgress);
}

void EventServer::processTimetick()
{
    // TODO: how to handle timeticks? handle them at all?
    assert(m_d->m_runInProgress);
}
