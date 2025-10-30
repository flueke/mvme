#include "mvme_tcp_stream_server.h"

#include <cassert>
#include <mesytec-mvlc/util/logging.h>

#include "mvme_workspace.h"
#include "nng_stream_server.h"
#include "util/expand_env_vars.h"
#include "util/qt_str.h"

namespace mesytec::mvme
{

struct MvmeTcpStreamServer::Private
{
    bool enabled_ = false;
    std::vector<std::string> listenUris_;
    std::shared_ptr<spdlog::logger> logger_;
    StreamConsumerBase::Logger mvmeLogger_;
    nng::NngStreamServer serverContext_;
    std::mutex mutex_; // protects everything! :)
    bool startupResult_ = false;
};

const std::vector<std::string> MvmeTcpStreamServer::DefaultListenUris = {
    "tcp4://*:42333",
#ifndef WIN32
    "ipc:///${XDG_RUNTIME_DIR}/mvme_tcp_stream_server.socket",
#endif
};

MvmeTcpStreamServer::MvmeTcpStreamServer()
    : IStreamBufferConsumer()
    , d(std::make_unique<Private>())
{
    d->logger_ = mvlc::get_logger("mvme_tcp_stream_server");
}

MvmeTcpStreamServer::~MvmeTcpStreamServer()
{

    std::unique_lock<std::mutex> lock(d->mutex_);
    d->serverContext_.shutdown = true;
}

void MvmeTcpStreamServer::startup()
{

    std::unique_lock<std::mutex> lock(d->mutex_);
    d->startupResult_ = d->serverContext_.start(d->listenUris_);
}

void MvmeTcpStreamServer::shutdown()
{

    std::unique_lock<std::mutex> lock(d->mutex_);
    d->serverContext_.stop();
    d->startupResult_ = false;
}

void MvmeTcpStreamServer::beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig,
                                   const analysis::Analysis *analysis)
{
    Q_UNUSED(runInfo);
    Q_UNUSED(vmeConfig);
    Q_UNUSED(analysis);
}

void MvmeTcpStreamServer::endRun(const DAQStats &stats, const std::exception *e)
{
    Q_UNUSED(stats);
    Q_UNUSED(e);
}

void MvmeTcpStreamServer::processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer,
                                        size_t bufferSize)
{
    std::unique_lock<std::mutex> lock(d->mutex_);
    Q_UNUSED(bufferType);
    assert(bufferSize <= std::numeric_limits<u32>::max());
    auto &ctx = d->serverContext_;
    send_to_all_clients(&ctx, bufferNumber, buffer, bufferSize);
}

void MvmeTcpStreamServer::setLogger(StreamConsumerBase::Logger logger)
{
    d->mvmeLogger_ = logger;
}

StreamConsumerBase::Logger &MvmeTcpStreamServer::getLogger()
{
    return d->mvmeLogger_;
}

void MvmeTcpStreamServer::reloadConfiguration()
{
    auto settings = make_workspace_settings();

    d->enabled_ = settings.value(QSL("TcpStreamServer/Enabled")).toBool();

    d->logger_->trace("MvmeTcpStreamServer::reloadConfiguration(): TcpStreamServer/Enabled={}",
                 d->enabled_);

    if (!d->enabled_ && d->serverContext_.isRunning())
    {
        logMessage(QSL("TcpStreamServer is disabled, shutting down"));
        shutdown();
        return;
    }

    d->listenUris_.clear();

    for (const auto &qtUri: settings.value(QSL("TcpStreamServer/ListenUris")).toStringList())
    {
        auto expanded = util::expand_env_vars(qtUri.toStdString());
        d->listenUris_.emplace_back(expanded);
    }

    logMessage(fmt::format("TcpStreamServer listening on: {}", fmt::join(d->listenUris_, ", ")).c_str());

    shutdown();
    startup();
    if (!d->startupResult_)
        logMessage(QSL("TcpStreamServer failed to listen on at least one URI! See the console log for details."));
}

} // namespace mesytec::mvme
