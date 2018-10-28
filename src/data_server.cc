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
    std::unique_ptr<QDataStream> stream;

    QDataStream &out() { return *stream; }
};

} // end anon namespace

struct AnalysisDataServer::Private
{
    Private(AnalysisDataServer *q)
        : m_q(q)
        , m_server(q)
    { }

    AnalysisDataServer *m_q;
    QTcpServer m_server;
    QHostAddress m_listenAddress;
    quint16 m_listenPort;
    AnalysisDataServer::Logger m_logger;

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

void AnalysisDataServer::Private::handleNewConnection()
{
    qDebug() << __PRETTY_FUNCTION__ << this;

    if (auto clientSocket = m_server.nextPendingConnection())
    {
        ClientInfo clientInfo = { std::unique_ptr<QTcpSocket>(clientSocket),
            std::make_unique<QDataStream>(clientSocket) };

        connect(clientInfo.socket.get(),
                static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(
                    &QAbstractSocket::error),
                m_q, [this, clientSocket] (QAbstractSocket::SocketError error) {
                    handleClientSocketError(clientSocket, error);
        });

        clientInfo.out() << "mvme data server\n";

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

void AnalysisDataServer::beginRun(const RunInfo &runInfo,
              const VMEConfig *vmeConfig,
              const analysis::Analysis *analysis,
              Logger logger)
{
    if (!(analysis->getA2AdapterState() && analysis->getA2AdapterState()->a2))
        return;

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

    QJsonArray eventOutputStructure;

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

            QJsonObject dsInfo;
            dsInfo["name"] = a1_dataSource->objectName();
            dsInfo["moduleIndex"] = moduleIndex;
            dsInfo["datatype"] = "double";
            dsInfo["output_size"] = a2_dataSource->output.size();
            dsInfo["output_lowerLimit"] = a2_dataSource->output.lowerLimits[0];
            dsInfo["output_upperLimit"] = a2_dataSource->output.upperLimits[0];

            dataSourceInfos.append(dsInfo);
        }

        QJsonObject eventInfo;
        eventInfo["eventIndex"] = eventIndex;
        eventInfo["dataSources"] = dataSourceInfos;
        eventOutputStructure.append(eventInfo);
    }

    QJsonArray eventTree;

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
        eventTree.append(eventInfo);
    }

    QJsonObject outputInfo;
    outputInfo["runId"] = ctx.runInfo.runId;
    outputInfo["isReplay"] = ctx.runInfo.isReplay;
    outputInfo["eventOutputs"] = eventOutputStructure;
    outputInfo["eventTree"] = eventTree;

    QJsonDocument doc(outputInfo);
    auto json = doc.toJson();

    using namespace mvme::data_server;

    for (auto &client: m_d->m_clients)
    {
        client.out() << MessageType::BeginRun
            << static_cast<u32>(json.size())
            << json;
    }
}

void AnalysisDataServer::endEvent(s32 eventIndex)
{
    //for (auto &client: m_d->m_clients)
    //{
    //    client.out() << QString("data for event %1").arg(eventIndex);
    //}

    if (!m_d->m_runContext.a2 || eventIndex < 0 || eventIndex >= a2::MaxVMEEvents)
    {
        InvalidCodePath;
        return;
    }

    const a2::A2 *a2 = m_d->m_runContext.a2;

    // TODO: iterate through module data sources and send out the extracted
    // values to each connected client
    for (size_t dsIndex = 0; dsIndex < a2->dataSourceCounts[eventIndex]; dsIndex++)
    {
        auto dataSource = a2->dataSources[eventIndex] + dsIndex;
        auto dataPipe = dataSource->output;
        u16 crateIndex = 0;
        u32 moduleIndex = dataSource->moduleIndex;

        // TODO: figure out what has to be done to guarantee a fixed data
        // source order.  Info sent out to the client in beginRun has to match
        // this here. Can probably use the natural A2 order and lookup config
        // info using A2AdapterState::sourceMap
        //
        // send out message to all clients:
        // size: sizeof(crateIndex) + sizeof(eventIndex) + sizeof(moduleIndex) + sizeof(dataSourceIndex)
        //       + dataPipe.data.size() * sizeof(double)
        // type: EventData
        // crateIndex
        // eventIndex
        // moduleIndex
        // dataSourceIndex
        // double values from the data pipe
    }
}


void AnalysisDataServer::endRun(const std::exception *e)
{
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
    // TODO: what to do about these?
#if 0
    for (auto &client: m_d->m_clients)
    {
        client.out() << "timetick";
    }
#endif
}
