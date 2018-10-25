#include "analysis_data_server.h"
#include <QTcpServer>
#include <QTcpSocket>

struct AnalysisDataServer::Private
{
    Private(AnalysisDataServer *q): m_q(q)
    {
        m_server.setParent(m_q);
    }

    AnalysisDataServer *m_q;
    QHostAddress m_listenAddress;
    quint16 m_listenPort;
    QTcpServer m_server;
    AnalysisDataServer::Logger m_logger;

    QSet<QTcpSocket *> m_activeClients;

    struct RunContext
    {
        RunInfo runInfo;
        const VMEConfig *vmeConfig = nullptr;
        const analysis::Analysis *analysis = nullptr;
    };

    RunContext m_runContext;

    void handleNewConnection();
};

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

void AnalysisDataServer::setLogger(Logger logger)
{
    m_d->m_logger = logger;
}

void AnalysisDataServer::setListeningInfo(const QHostAddress &address, quint16 port)
{
    m_d->m_listenAddress = address;
    m_d->m_listenPort = port;
}

void AnalysisDataServer::startListening()
{
    if (bool res = m_d->m_server.listen(m_d->m_listenAddress, m_d->m_listenPort))
    {
        logMessage(QSL("Listening on %1:%2")
                   .arg(m_d->m_listenAddress.toString())
                   .arg(m_d->m_listenPort));
    }
    else
    {
        logMessage(QSL("Error listening on %1:%2")
                   .arg(m_d->m_listenAddress.toString())
                   .arg(m_d->m_listenPort));
    }
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
    m_d->m_runContext = { runInfo, vmeConfig, analysis };
}

void AnalysisDataServer::endRun(const std::exception *e)
{
    m_d->m_runContext = {};
}

void AnalysisDataServer::endEvent(s32 eventIndex)
{
    // TODO: iterate through module data sources and send out the extracted
    // values to each connected client
}

void AnalysisDataServer::beginEvent(s32 eventIndex)
{
    // Noop
}

void AnalysisDataServer::processModuleData(s32 eventIndex, s32 moduleIndex,
                       const u32 *data, u32 size)
{
    // Noop for this server case. We're interested in the endEvent() call as at
    // that point all data from all modules has been processed and is available
    // at the output pipes of the data sources.
}

void AnalysisDataServer::processTimetick()
{
    // TODO: send timetick info to client
}

void AnalysisDataServer::logMessage(const QString &msg)
{
    if (m_d->m_logger)
    {
        m_d->m_logger(QSL("AnalysisDataServer: ") + msg);
    }
}

void AnalysisDataServer::Private::handleNewConnection()
{
    qDebug() << __PRETTY_FUNCTION__ << this;

    auto clientSocket = m_server.nextPendingConnection();
    assert(clientSocket);
}
