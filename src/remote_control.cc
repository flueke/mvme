#include "remote_control.h"

#include "jcon/json_rpc_logger.h"
#include "jcon/json_rpc_tcp_server.h"

namespace
{

class RpcLogger: public jcon::JsonRpcLogger
{
    public:
        RpcLogger(MVMEContext *context)
            : m_context(context)
        { }

        virtual void logDebug(const QString& message) override
        {
            m_context->logMessage("JSON RPC Debug: " + message);
        }

        virtual void logInfo(const QString& message) override
        {
            m_context->logMessage("JSON RPC Info: " + message);
        }

        virtual void logWarning(const QString& message) override
        {
            m_context->logMessage("JSON RPC Warning: " + message);
        }

        virtual void logError(const QString& message) override
        {
            m_context->logMessage("JSON RPC Error: " + message);
        }

    private:
        MVMEContext *m_context;
};

} // end anon namespace

namespace remote_control
{

//
// RemoteControl
//

struct RemoteControl::Private
{
    std::unique_ptr<jcon::JsonRpcTcpServer> m_server;
};

RemoteControl::RemoteControl(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_d(std::make_unique<Private>())
{
    auto logger = std::make_shared<RpcLogger>(context);

    m_d->m_server = std::make_unique<jcon::JsonRpcTcpServer>(nullptr, logger);
    m_d->m_server->registerServices({
        new DAQControlService(context),
        new StatusInfoService(context),
    });
}

RemoteControl::~RemoteControl()
{}

void RemoteControl::start()
{
    // FIXME: configurability please! also there's an
    // overload taking a QHostAddress to bind to a specific interface
    m_d->m_server->listen(6002);
}

void RemoteControl::stop()
{
    m_d->m_server->close();
}


//
// DAQControlService
//

DAQControlService::DAQControlService(MVMEContext *context)
    : m_context(context)
{
}

void DAQControlService::startDAQ()
{
    qDebug() << __PRETTY_FUNCTION__;
}

void DAQControlService::stopDAQ()
{
    qDebug() << __PRETTY_FUNCTION__;
}

void DAQControlService::pauseDAQ()
{
    qDebug() << __PRETTY_FUNCTION__;
}

void DAQControlService::resumeDAQ()
{
    qDebug() << __PRETTY_FUNCTION__;
}


//
// StatusInfoService
//

StatusInfoService::StatusInfoService(MVMEContext *context)
    : m_context(context)
{
}

QVariantMap StatusInfoService::getDAQState()
{
    qDebug() << __PRETTY_FUNCTION__;
    return {};
}

QVariantMap StatusInfoService::getDAQStats()
{
    qDebug() << __PRETTY_FUNCTION__;
    return {};
}

QVariantMap StatusInfoService::getAnalysisStats()
{
    qDebug() << __PRETTY_FUNCTION__;
    return {};
}

QString StatusInfoService::getLogMessages()
{
    qDebug() << __PRETTY_FUNCTION__;
    return {};
}

} // end namespace remote_control
