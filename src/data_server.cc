#include "data_server.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>

#include "data_server_protocol.h"
#include "analysis/a2_adapter.h"
#include "analysis/a2/a2.h"

namespace
{

struct ClientInfo
{
    std::unique_ptr<QTcpSocket> socket;
};

} // end anon namespace

using namespace mvme::data_server;

struct AnalysisDataServer::Private
{
    Private(AnalysisDataServer *q)
        : m_q(q)
        , m_server(q)
    { }

    AnalysisDataServer *m_q;
    QTcpServer m_server;
    QHostAddress m_listenAddress = QHostAddress::Any;
    quint16 m_listenPort = AnalysisDataServer::Default_ListenPort;
    AnalysisDataServer::Logger m_logger;
    quint64 m_writeThreshold = AnalysisDataServer::Default_WriteThresholdBytes;

    std::vector<ClientInfo> m_clients;

    struct RunContext
    {
        RunInfo runInfo;
        const VMEConfig *vmeConfig = nullptr;
        const analysis::Analysis *analysis = nullptr;
        const analysis::A2AdapterState *adapterState = nullptr;
        const a2::A2 *a2 = nullptr;
    };

    RunContext m_runContext;

    void handleNewConnection();
    void handleClientSocketError(QTcpSocket *socket, QAbstractSocket::SocketError error);
    void logMessage(const QString &msg);
};

namespace
{

qint64 write_data(QIODevice &out, const char *data, size_t size)
{
    const char *curPtr = data;
    const char *endPtr = data + size;

    while (curPtr < endPtr)
    {
        qint64 written = out.write(data, size);

        if (written < 0)
        {
            throw std::runtime_error("write_data failed (FIXME: this needs information");
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
    qint64 result = 0;
    result += write_pod(out, type);
    result += write_pod(out, size);
    return result;
}

qint64 write_message(QIODevice &out, MessageType type, const char *data, u32 size)
{
    qint64 result = 0;
    result += write_message_header(out, type, size);
    result += write_data(out, data, size);
    return result;
}

qint64 write_message(QIODevice &out, MessageType type, const QByteArray &contents)
{
    return write_message(out, type, contents.data(),
                         static_cast<u32>(contents.size()));
}

} // end anon namespace

void AnalysisDataServer::Private::handleNewConnection()
{
    qDebug() << __PRETTY_FUNCTION__ << this;

    if (auto clientSocket = m_server.nextPendingConnection())
    {
        ClientInfo clientInfo = { std::unique_ptr<QTcpSocket>(clientSocket) };

        // ugly connect due to overloaded QAbstractSocket::error() method
        connect(clientInfo.socket.get(),
                static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(
                    &QAbstractSocket::error),
                m_q, [this, clientSocket] (QAbstractSocket::SocketError error) {
                    handleClientSocketError(clientSocket, error);
        });

        // Empty "Hello" message
        write_message(*clientInfo.socket, MessageType::Hello, "mvme analysis data server");

        m_clients.emplace_back(std::move(clientInfo));
    }
}

void AnalysisDataServer::Private::handleClientSocketError(QTcpSocket *socket,
                                                          QAbstractSocket::SocketError error)
{
    // Find the client info object and remove the it, thereby closing the
    // client socket.

    auto socket_match = [socket] (const ClientInfo &clientInfo)
    {
        return clientInfo.socket.get() == socket;
    };

    auto it = std::remove_if(m_clients.begin(), m_clients.end(), socket_match);

    if (it != m_clients.end())
    {
        // Have to delete when next entering the event loop. Otherwise pending
        // signal invocations can lead to a crash.
        it->socket->deleteLater();
        it->socket.release();
        m_clients.erase(it, m_clients.end());
    }
}

void AnalysisDataServer::Private::logMessage(const QString &msg)
{
    if (m_logger)
    {
        m_logger(QSL("AnalysisDataServer: ") + msg);
    }
}

AnalysisDataServer::AnalysisDataServer(QObject *parent)
    : QObject(parent)
    , m_d(std::make_unique<Private>(this))
{
    connect(&m_d->m_server, &QTcpServer::newConnection,
            this, [this] { m_d->handleNewConnection(); });
}

AnalysisDataServer::AnalysisDataServer(Logger logger, QObject *parent)
    : AnalysisDataServer(parent)
{
    setLogger(logger);
}

AnalysisDataServer::~AnalysisDataServer()
{}

void AnalysisDataServer::startup()
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

void AnalysisDataServer::shutdown()
{
    m_d->m_server.close();
    m_d->m_clients.clear();
}

void AnalysisDataServer::setLogger(Logger logger)
{
    m_d->m_logger = logger;
}

void AnalysisDataServer::setListeningInfo(const QHostAddress &address, quint16 port)
{
    m_d->m_listenAddress = address;
    m_d->m_listenPort = port;
}

bool AnalysisDataServer::isListening() const
{
    return m_d->m_server.isListening();
}

size_t AnalysisDataServer::getNumberOfClients() const
{
    return m_d->m_clients.size();
}

void AnalysisDataServer::setWriteThresholdBytes(qint64 threshold)
{
    m_d->m_writeThreshold = threshold;
}

qint64 AnalysisDataServer::getWriteThresholdBytes() const
{
    return m_d->m_writeThreshold;
}

void AnalysisDataServer::beginRun(const RunInfo &runInfo,
              const VMEConfig *vmeConfig,
              const analysis::Analysis *analysis,
              Logger logger)
{
    if (!(analysis->getA2AdapterState() && analysis->getA2AdapterState()->a2))
        return;

    setLogger(logger);

    m_d->m_runContext =
    {
        runInfo, vmeConfig, analysis,
        analysis->getA2AdapterState(),
        analysis->getA2AdapterState()->a2
    };

    // How the data stream looks:
    // eventIndex (known in endEvent)
    // first data source output
    // second data source output
    // ...
    //
    // What the receiver has to know
    // The data sources for each event index.
    // The modules for each event index.
    // The relationship of a module and its datasources

    auto &ctx = m_d->m_runContext;
    const a2::A2 *a2 = ctx.a2;

    QJsonArray eventDataSources;

    for (s32 eventIndex = 0; eventIndex < a2::MaxVMEEvents; eventIndex++)
    {
        const u32 dataSourceCount = a2->dataSourceCounts[eventIndex];

        if (!dataSourceCount) continue;

        QJsonArray dataSourceInfos;

        for (u32 dsIndex = 0; dsIndex < dataSourceCount; dsIndex++)
        {
            auto a2_dataSource = a2->dataSources[eventIndex] + dsIndex;
            auto a1_dataSource = ctx.adapterState->sourceMap.value(a2_dataSource);
            s32 moduleIndex = a2_dataSource->moduleIndex;

            qDebug() << "DataServer" << "structure: eventIndex=" << eventIndex << "dsIndex=" << dsIndex
                << "a2_ds=" << a2_dataSource << ", a1_dataSource=" << a1_dataSource
                << "a2_ds_moduleIndex=" << moduleIndex;

            qint64 output_size  = a2_dataSource->output.size();
            qint64 output_bytes = output_size * a2_dataSource->output.data.element_size;

            QJsonObject dsInfo;
            dsInfo["name"] = a1_dataSource->objectName();
            dsInfo["moduleIndex"] = moduleIndex;
            dsInfo["datatype"] = "double";
            dsInfo["output_size"]  = output_size;
            dsInfo["output_bytes"] = output_bytes;
            dsInfo["output_lowerLimit"] = a2_dataSource->output.lowerLimits[0];
            dsInfo["output_upperLimit"] = a2_dataSource->output.upperLimits[0];

            dataSourceInfos.append(dsInfo);
        }

        QJsonObject eventInfo;
        eventInfo["eventIndex"] = eventIndex;
        eventInfo["dataSources"] = dataSourceInfos;
        eventDataSources.append(eventInfo);
    }

    QJsonArray vmeTree;

    for (s32 eventIndex = 0; eventIndex < a2::MaxVMEEvents; eventIndex++)
    {
        auto eventConfig = vmeConfig->getEventConfig(eventIndex);
        if (!eventConfig) continue;

        auto moduleConfigs = eventConfig->getModuleConfigs();

        QJsonArray moduleInfos;

        for (s32 moduleIndex = 0; moduleIndex < moduleConfigs.size(); moduleIndex++)
        {
            auto moduleConfig = moduleConfigs[moduleIndex];
            QJsonObject moduleInfo;
            moduleInfo["name"] = moduleConfig->objectName();
            moduleInfo["type"] = moduleConfig->getModuleMeta().typeName;
            moduleInfo["moduleIndex"] = moduleIndex;
            moduleInfos.append(moduleInfo);
        }

        QJsonObject eventInfo;
        eventInfo["eventIndex"] = eventIndex;
        eventInfo["modules"] = moduleInfos;
        eventInfo["name"] = eventConfig->objectName();
        vmeTree.append(eventInfo);
    }

    QJsonObject outputInfo;
    outputInfo["runId"] = ctx.runInfo.runId;
    outputInfo["isReplay"] = ctx.runInfo.isReplay;
    outputInfo["eventDataSources"] = eventDataSources;
    outputInfo["vmeTree"] = vmeTree;

    QJsonDocument doc(outputInfo);
    QByteArray json = doc.toJson();

    qDebug() << __PRETTY_FUNCTION__ << "beginRunInfo to be sent to clients:" << endl
        << json;

    using namespace mvme::data_server;

    for (auto &client: m_d->m_clients)
    {
        write_message(*client.socket, MessageType::BeginRun, json);
    }
}

void AnalysisDataServer::endEvent(s32 eventIndex)
{
    if (!m_d->m_runContext.a2 || eventIndex < 0 || eventIndex >= a2::MaxVMEEvents)
    {
        InvalidCodePath;
        return;
    }

    const a2::A2 *a2 = m_d->m_runContext.a2;
    const u32 dataSourceCount = a2->dataSourceCounts[eventIndex];

    if (!dataSourceCount || !getNumberOfClients()) return;

    // pre calculate the output message size. TODO: this should be cached.
    u32 msgSize = sizeof(u32); // eventIndex

    for (u32 dsIndex = 0; dsIndex < dataSourceCount; dsIndex++)
    {
        auto ds = a2->dataSources[eventIndex] + dsIndex;

        // data source index, length of the data source output in bytes
        msgSize += sizeof(u32) + sizeof(u32);
        // size of the output * sizeof(double)
        msgSize += ds->output.size() * ds->output.data.element_size;
    }

    using namespace mvme::data_server;

    // Write out the message header and calculated size, followed by the
    // eventindex to each client socket

    for (auto &client: m_d->m_clients)
    {
        write_message_header(*client.socket, MessageType::EventData, msgSize);
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

        for (auto &client: m_d->m_clients)
        {
            auto dBegin = reinterpret_cast<const char *>(dataPipe.data.begin());
            auto dEnd   = reinterpret_cast<const char *>(dataPipe.data.end());
            const u32 dataBytesToWrite = dEnd - dBegin;

            // Note: technically the size does not need to be transmitted
            // again. The client got the information about the indiviudal
            // outputs sizes in the BeginRun message. This information is here
            // for consistency checks only.
            write_pod(*client.socket, dsIndex);
            write_pod(*client.socket, dataBytesToWrite);
            write_data(*client.socket, dBegin, dataBytesToWrite);
        }
    }

    // Check write treshold for each client and block if necessary
    for (auto &client: m_d->m_clients)
    {
        if (client.socket->bytesToWrite() > getWriteThresholdBytes())
        {
            client.socket->waitForBytesWritten();
        }
    }
}

void AnalysisDataServer::endRun(const std::exception *e)
{
    for (auto &clientInfo: m_d->m_clients)
    {
        write_message(*clientInfo.socket, MessageType::EndRun, {});
    }

    m_d->m_runContext = {};
}

void AnalysisDataServer::beginEvent(s32 eventIndex)
{
    // Noop
}

void AnalysisDataServer::processModuleData(s32 eventIndex, s32 moduleIndex,
                       const u32 *data, u32 size)
{
    // Noop for this server case. We're interested in the endEvent() call as at
    // that point all data from all modules has been processed by the a2
    // analysis system and is available at the output pipes of the data
    // sources.
}

void AnalysisDataServer::processTimetick()
{
    // TODO: how to handle timeticks?
}
