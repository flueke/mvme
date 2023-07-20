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
#include "remote_control.h"

#include <jcon/json_rpc_logger.h>
#include <jcon/json_rpc_tcp_server.h>

#include "git_sha1.h"
#include "sis3153_readout_worker.h"
#include "mvme_context_lib.h"

namespace
{

class RpcLogger: public jcon::JsonRpcLogger
{
    public:
        explicit RpcLogger(MVMEContext */*context*/)
            //: m_context(context)
        { }

        virtual void logDebug(const QString& message) override
        {
            //m_context->logMessage("JSON RPC Debug: " + message);
            qDebug().noquote() << "JSON RPC Debug: " + message;
        }

        virtual void logInfo(const QString& message) override
        {
            //m_context->logMessage("JSON RPC Info: " + message);
            qDebug().noquote() << "JSON RPC Info: " + message;
        }

        virtual void logWarning(const QString& message) override
        {
            //m_context->logMessage("JSON RPC Warning: " + message);
            qDebug().noquote() << "JSON RPC Warning: " + message;
        }

        virtual void logError(const QString& message) override
        {
            //m_context->logMessage(message);
            qDebug().noquote() << "JSON RPC Error: " + message;
        }

    private:
        //MVMEContext *m_context;
};

QVariantMap make_error_info(int code, const QString &msg)
{
    return QVariantMap
    {
        { "code", code },
        { "message", msg }
    };
}

inline QVariant u64_to_var(u64 value)
{
    return QVariant(static_cast<qulonglong>(value));
}

} // end anon namespace

namespace remote_control
{

//
// RemoteControl
//

struct RemoteControl::Private
{
    MVMEContext *m_context;
    std::unique_ptr<jcon::JsonRpcTcpServer> m_server;
    QString m_listenAddress;
    int m_listenPort;
    std::unique_ptr<HostInfoWrapper> m_hostInfoWrapper;
};

RemoteControl::RemoteControl(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_d(std::make_unique<Private>())
{
    auto logger = std::make_shared<RpcLogger>(context);

    m_d->m_context = context;
    m_d->m_server = std::make_unique<jcon::JsonRpcTcpServer>(nullptr, logger);
    m_d->m_server->registerServices({
        new DAQControlService(context),
        new InfoService(context),
    });
}

RemoteControl::~RemoteControl()
{}

void RemoteControl::start()
{
    if (m_d->m_listenAddress.isEmpty())
    {
        m_d->m_server->listen(m_d->m_listenPort);

        m_d->m_context->logMessage(QSL("JSON RPC server listening on port %1.")
                              .arg(m_d->m_listenPort));
    }
    else
    {
        auto on_lookup_done = [this] (const QHostInfo &hi)
        {
            auto addresses = hi.addresses();

            qDebug() << __PRETTY_FUNCTION__
                << "host lookup done. got" << addresses.size() << "results";

            if (!addresses.isEmpty())
            {
                m_d->m_server->listen(addresses.first(), m_d->m_listenPort);

                if (m_d->m_server->isListening())
                {
                    m_d->m_context->logMessage(QSL("JSON RPC server listening on port %1:%2.")
                                               .arg(addresses.first().toString())
                                               .arg(m_d->m_listenPort));
                }
            }
            else
            {
                m_d->m_context->logMessage(
                    QSL("Error: JSON RPC server could not find a listening address for \"%1\".")
                    .arg(hi.hostName()));
            }
        };

        m_d->m_hostInfoWrapper = std::make_unique<HostInfoWrapper>(on_lookup_done);
        m_d->m_hostInfoWrapper->lookupHost(m_d->m_listenAddress);
    }
}

void RemoteControl::stop()
{
    if (m_d->m_server->isListening())
    {
        m_d->m_context->logMessage(QSL("Stopping JSON RPC server"));
        m_d->m_server->close();
    }
}

void RemoteControl::setListenAddress(const QString &address)
{
    m_d->m_listenAddress = address;
}

void RemoteControl::setListenPort(int port)
{
    m_d->m_listenPort = port;
}

QString RemoteControl::getListenAddress() const
{
    return m_d->m_listenAddress;
}

int RemoteControl::getListenPort() const
{
    return m_d->m_listenPort;
}

MVMEContext *RemoteControl::getContext() const
{
    return m_d->m_context;
}

//
// DAQControlService
//

DAQControlService::DAQControlService(MVMEContext *context)
    : QObject(context)
    , m_context(context)
{
}

bool DAQControlService::startDAQ()
{
    if (m_context->getMode() != GlobalMode::DAQ)
    {
        throw make_error_info(ErrorCodes::NotInDAQMode, "Not in DAQ mode");
    }

    if (m_context->getDAQState() != DAQState::Idle)
    {
        throw make_error_info(ErrorCodes::ReadoutWorkerBusy, "DAQ readout worker busy");
    }

    if (m_context->getMVMEStreamWorkerState() != AnalysisWorkerState::Idle)
    {
        throw make_error_info(ErrorCodes::AnalysisWorkerBusy, "DAQ analysis worker busy");
    }

    if (m_context->getVMEController()->getState() != ControllerState::Connected)
    {
        throw make_error_info(ErrorCodes::ControllerNotConnected, "VME controller not connected");
    }

    m_context->startDAQReadout();

    return true;
}

bool DAQControlService::stopDAQ()
{
    if (m_context->getMode() != GlobalMode::DAQ)
    {
        throw make_error_info(ErrorCodes::NotInDAQMode, "Not in DAQ mode");
    }

    qDebug() << __PRETTY_FUNCTION__ << QDateTime::currentDateTime()
        << "pre stopDAQ";
    m_context->stopDAQ();
    qDebug() << __PRETTY_FUNCTION__ << QDateTime::currentDateTime()
        << "post stopDAQ";

    if (m_context->getDAQState() != DAQState::Idle)
    {
        throw make_error_info(ErrorCodes::ReadoutWorkerBusy, "DAQ readout worker still busy");
    }

    qDebug() << __PRETTY_FUNCTION__ << QDateTime::currentDateTime()
        << "stream worker state =" << to_string(m_context->getMVMEStreamWorkerState());

    if (m_context->getMVMEStreamWorkerState() != AnalysisWorkerState::Idle)
    {
        throw make_error_info(ErrorCodes::AnalysisWorkerBusy, "DAQ analysis worker still busy");
    }

    return true;
}

QString DAQControlService::getSystemState()
{
    return to_string(m_context->getMVMEState());
}

QString DAQControlService::getDAQState()
{
    return DAQStateStrings.value(m_context->getDAQState());
}

QString DAQControlService::getAnalysisState()
{
    return to_string(m_context->getMVMEStreamWorkerState());
}

QString DAQControlService::reconnectVMEController()
{
    if (m_context->getMode() != GlobalMode::DAQ)
        throw make_error_info(ErrorCodes::NotInDAQMode, "Not in DAQ mode");

    if (m_context->getDAQState() != DAQState::Idle)
        throw make_error_info(ErrorCodes::ReadoutWorkerBusy, "DAQ not idle");

    m_context->reconnectVMEController();

    return QSL("Reconnection attempt initiated");
}

QString DAQControlService::getGlobalMode()
{
    return (m_context->getMode() == GlobalMode::DAQ
        ? QSL("daq")
        : QSL("replay"));
}

bool DAQControlService::loadAnalysis(const QString &filepath)
{
    QFile f(filepath);

    if (!f.open(QIODevice::ReadOnly))
        throw QVariantMap{{"code", "42"}, { "message", f.errorString()}};

    QJsonParseError parseError;
    auto doc = QJsonDocument::fromJson(f.readAll(), &parseError);

    if (parseError.error != QJsonParseError::NoError)
        throw QVariantMap{{"code", "42"}, { "message", parseError.errorString()}};

    auto result = m_context->loadAnalysisConfig(doc, filepath);
    if (result)
    {
        m_context->setAnalysisConfigFilename(filepath);
    }
    return result;
}

bool DAQControlService::loadListfile(const QString &filepath)
{
    try
    {
        // TODO: allow to pass OpenListfileOptions here. Both "loadAnalysis" and
        // "replayAllParts" are interesting!
        // FIXME: context_open_listfile() uses the gui to display error messages
        const auto &handle = context_open_listfile(m_context, filepath);
    }
    catch (const QString &err)
    {
        throw QVariantMap{{"code", "42"}, { "message", err}};
    }

    return true;
}

bool DAQControlService::startReplay(const QVariantMap &options)
{
    if (m_context->getMode() != GlobalMode::Replay)
    {
        throw make_error_info(ErrorCodes::NotInReplayMode, "Not in Replay mode");
    }

    if (m_context->getDAQState() != DAQState::Idle)
    {
        throw make_error_info(ErrorCodes::ReadoutWorkerBusy, "DAQ replay worker busy");
    }

    if (m_context->getMVMEStreamWorkerState() != AnalysisWorkerState::Idle)
    {
        throw make_error_info(ErrorCodes::AnalysisWorkerBusy, "DAQ analysis worker busy");
    }

    bool keepHistoContents = options.value("keepHistoContents", true).toBool();

    m_context->startDAQReplay(0, keepHistoContents);

    return true;
}

//
// InfoService
//

InfoService::InfoService(MVMEContext *context)
    : QObject(context)
    , m_context(context)
{
}

QString InfoService::getVersion()
{
    QString versionString = QString("mvme-%1").arg(GIT_VERSION);

    auto bitness = get_bitness_string();

    if (!bitness.isEmpty())
    {
        versionString += QString(" (%1)").arg(bitness);
    }

    return versionString;
}

QStringList InfoService::getLogMessages()
{
    return m_context->getLogBuffer();
}

QVariantMap InfoService::getDAQStats()
{
    auto stats = m_context->getDAQStats();
    auto runInfo = m_context->getRunInfo();
    QVariantMap r;

    r["state"]                  = DAQStateStrings.value(m_context->getDAQState());
    r["currentTime"]            = QDateTime::currentDateTime();
    r["startTime"]              = stats.startTime;
    r["endTime"]                = stats.endTime;
    r["totalBytesRead"]         = u64_to_var(stats.totalBytesRead);
    r["totalBuffersRead"]       = u64_to_var(stats.totalBuffersRead);
    r["buffersWithErrors"]      = u64_to_var(stats.buffersWithErrors);
    r["droppedBuffers"]         = u64_to_var(stats.droppedBuffers);
    r["listFileBytesWritten"]   = u64_to_var(stats.listFileBytesWritten);
    r["listFileFilename"]       = stats.listfileFilename;
    r["analyzedBuffers"]        = u64_to_var(stats.getAnalyzedBuffers());
    r["analysisEfficiency"]     = stats.getAnalysisEfficiency();
    r["runId"]                  = runInfo.runId;

    return r;
}

QString InfoService::getVMEControllerType()
{
    auto ctrl = m_context->getVMEController();
    assert(ctrl);

    if (!ctrl)
        throw make_error_info(ErrorCodes::NoVMEControllerFound, "No VME controller found");

    return to_string(m_context->getVMEController()->getType());
}

namespace
{

QVariantMap sis_counters_to_variantmap(const SIS3153ReadoutWorker::Counters &counters)
{
    QVariantMap r;

    {
        QVariantList sl;

        for (size_t si = 0; si < SIS3153Constants::NumberOfStackLists; si++)
        {
            QVariantMap data =
            {
                { "completeEvents", u64_to_var(counters.stackListCounts[si]) },
                { "Berr_BlockRead", u64_to_var(counters.stackListBerrCounts_Block[si]) },
                { "Berr_Read",      u64_to_var(counters.stackListBerrCounts_Read[si]) },
                { "Berr_Write",     u64_to_var(counters.stackListBerrCounts_Write[si]) },
                { "embeddedEvents", u64_to_var(counters.embeddedEvents[si]) },
                { "partialFragments", u64_to_var(counters.partialFragments[si]) },
                { "reassembledPartials", u64_to_var(counters.reassembledPartials[si]) },
            };

            sl.append(data);
        }

        r["stacklistCounters"] = sl;
        r["lostEvents"] = u64_to_var(counters.lostEvents);
        r["multiEventPackets"] = u64_to_var(counters.multiEventPackets);
        r["watchdogStackList"] = counters.watchdogStackList;
        r["receivedEventsExcludingWatchdog"] = u64_to_var(counters.receivedEventsExcludingWatchdog());
        r["receivedEventsIncludingWatchdog"] = u64_to_var(counters.receivedEventsIncludingWatchdog());
        r["receivedWatchdogEvents"] = u64_to_var(counters.receivedWatchdogEvents());
    }

    return r;
}

} // end anon namespace

QVariantMap InfoService::getVMEControllerStats()
{
    auto ctrl = m_context->getVMEController();
    assert(ctrl);

    if (!ctrl)
        throw make_error_info(ErrorCodes::NoVMEControllerFound, "No VME controller found");

    QVariantMap result;

    switch (ctrl->getType())
    {
        case VMEControllerType::VMUSB:
        case VMEControllerType::MVLC_USB:
        case VMEControllerType::MVLC_ETH:
            {
                // no specific stats present
            } break;

        case VMEControllerType::SIS3153:
            {
                auto readoutWorker = qobject_cast<SIS3153ReadoutWorker *>(
                    m_context->getReadoutWorker());
                assert(readoutWorker);

                const auto counters = readoutWorker->getCounters();

                result = sis_counters_to_variantmap(counters);

            } break;
    }

    result["controllerType"] = getVMEControllerType();
    result["controllerState"] = to_string(ctrl->getState());

    return result;
}

QString InfoService::getVMEControllerState()
{
    auto ctrl = m_context->getVMEController();
    assert(ctrl);

    if (!ctrl)
        throw make_error_info(ErrorCodes::NoVMEControllerFound, "No VME controller found");

    return to_string(ctrl->getState());
}

HostInfoWrapper::HostInfoWrapper(Callback callback, QObject *parent)
    : QObject(parent)
    , m_callback(callback)
{
}

void HostInfoWrapper::lookupHost(const QString &name)
{
    QHostInfo::lookupHost(name, this, SLOT(lookedUp(const QHostInfo &)));
}

void HostInfoWrapper::lookedUp(const QHostInfo &hi)
{
    if (m_callback) m_callback(hi);
}

} // end namespace remote_control
