#include "mvme_stream_server.h"

#include <boost/endian/conversion.hpp>
#include <cassert>
#include <mesytec-mvlc/mesytec-mvlc.h>

#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_mvlc_listfile.h"
#include "mvme_workspace.h"
#include "util/expand_env_vars.h"
#include "util/qt_str.h"

namespace mesytec::mvme
{

struct MvmeStreamServer::Private
{
    // Holds the StreamServer/* settings. Used to detect config changes.
    std::map<QString, QVariant> serverSettings;
    bool enabled_ = false;
    bool sendRawFormat_ = true;
    std::vector<std::string> listenUris_;
    std::shared_ptr<spdlog::logger> logger_;
    StreamConsumerBase::Logger mvmeLogger_;
    std::unique_ptr<mvlc::stream::IStreamServer> server_;
    std::mutex mutex_; // protects everything! :)
    size_t startupResult_ = false;
};

const std::vector<std::string> MvmeStreamServer::DefaultListenUris = {
    "tcp4://*:42333",
#ifndef WIN32
    "ipc://${XDG_RUNTIME_DIR}/mvme_stream_server.sock",
#endif
};

MvmeStreamServer::MvmeStreamServer()
    : IStreamBufferConsumer()
    , d(std::make_unique<Private>())
{
    d->logger_ = mvlc::get_logger("mvme_stream_server");
    d->server_ = std::make_unique<mvlc::stream::StreamServer>();
}

MvmeStreamServer::~MvmeStreamServer() {}

void MvmeStreamServer::startup()
{

    std::unique_lock<std::mutex> lock(d->mutex_);
    if (d->enabled_ && !d->listenUris_.empty())
    {
        d->startupResult_ = 0;
        for (const auto &uri: d->listenUris_)
        {
            if (!d->server_->listen(uri))
            {
                d->logger_->error("MvmeStreamServer: Failed to listen on URI: {}", uri);
                ++d->startupResult_;
            }
            else
            {
                d->logger_->info("MvmeStreamServer: Listening on URI: {}", uri);
            }
        }
    }
}

void MvmeStreamServer::shutdown()
{
    std::unique_lock<std::mutex> lock(d->mutex_);
    d->server_->stop();
    d->startupResult_ = 0;
    d->logger_->info("MvmeStreamServer: stopped");
}

void MvmeStreamServer::beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig,
                                const analysis::Analysis *analysis)
{
    Q_UNUSED(runInfo);
    Q_UNUSED(analysis);

    std::unique_lock<std::mutex> lock(d->mutex_);
    auto crateConfig = mvme::vmeconfig_to_crateconfig(vmeConfig);
    mvlc::listfile::BufferedWriteHandle bwh;
    mvlc::listfile::listfile_write_crate_config(bwh, crateConfig);
    mvme_mvlc::listfile_write_mvme_config(bwh, crateConfig.crateId, *vmeConfig);
    auto preambleBody = bwh.getBuffer();
    assert(preambleBody.size() % sizeof(u32) == 0);

    // Use an IOV array to handle the framed format case.
    // FIXME: what to do with the freaking buffer number with the preamble? concept breaks here.
    std::array<u32, 2> frameHeader = {
        boost::endian::native_to_little(0),
        boost::endian::native_to_little(static_cast<u32>(preambleBody.size() / sizeof(u32)))
    };
    std::array<mvlc::stream::IStreamServer::IOV, 2> iovs;
    iovs.fill({});

    if (!d->sendRawFormat_)
    {
        iovs[0] = { frameHeader.data(), frameHeader.size() * sizeof(u32) };
    }

    iovs[d->sendRawFormat_ ? 0 : 1] = {
        reinterpret_cast<const void *>(preambleBody.data()),
        preambleBody.size()
    };

    d->logger_->debug("Setting MVME StreamServer preamble, size {} bytes", preambleBody.size());
    d->server_->setPreamble(preambleBody);
}

void MvmeStreamServer::endRun(const DAQStats &stats, const std::exception *e)
{
    Q_UNUSED(stats);
    Q_UNUSED(e);
}

void MvmeStreamServer::processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer,
                                     size_t bufferSize)
{
    std::unique_lock<std::mutex> lock(d->mutex_);
    Q_UNUSED(bufferType);
    assert(bufferSize > 0);
    assert(bufferSize <= std::numeric_limits<u32>::max());

    if (!d->enabled_)
    {
        return;
    }

    std::array<u32, 2> frameHeader = {
        boost::endian::native_to_little(bufferNumber),
        boost::endian::native_to_little(static_cast<u32>(bufferSize))};

    assert(frameHeader[1] != 0); // size should not be zero, neither in little nor in big endian :)

    // Use an IOV array so we can send the (optional) header and the contents in one go.
    std::array<mvlc::stream::IStreamServer::IOV, 2> iovs;
    iovs.fill({});

    if (!d->sendRawFormat_)
    {
        d->logger_->debug(
            "Sending framed MVME buffer with seq num {} ({:#010x}), size {} ({:#010x}) words",
            frameHeader[0], frameHeader[0], frameHeader[1], frameHeader[1]);

        iovs[0] = {frameHeader.data(), frameHeader.size() * sizeof(u32)};
    }

    iovs[d->sendRawFormat_ ? 0 : 1] = {reinterpret_cast<const void *>(buffer),
                                       bufferSize * sizeof(u32)};

    d->server_->sendToAllClients(iovs.data(), d->sendRawFormat_ ? 1 : 2);

    d->logger_->debug("Sent {} buffer {}, size {} words. Sent a total of {} bytes",
                      d->sendRawFormat_ ? "raw" : "framed", bufferNumber, bufferSize,
                      iovs[0].len + iovs[1].len);

    auto debugView = std::basic_string_view<u32>(buffer, std::min<size_t>(bufferSize, 8));
    d->logger_->debug("start of buffer contents: {:#010x}", fmt::join(debugView, ", "));
}

void MvmeStreamServer::setLogger(StreamConsumerBase::Logger logger) { d->mvmeLogger_ = logger; }

StreamConsumerBase::Logger &MvmeStreamServer::getLogger() { return d->mvmeLogger_; }

void MvmeStreamServer::reloadConfiguration()
{
    auto prevServerSettings = d->serverSettings;
    d->serverSettings.clear();

    auto settings = make_workspace_settings();
    settings.beginGroup("StreamServer");

    for (const auto &key: settings.allKeys())
    {
        d->serverSettings[key] = settings.value(key);
    }

    d->enabled_ = settings.value(QSL("Enabled")).toBool();
    d->sendRawFormat_ = settings.value(QSL("SendRawFormat"), true).toBool();

    d->logger_->trace("MvmeStreamServer::reloadConfiguration(): StreamServer/Enabled={}",
                      d->enabled_);

    if (!d->enabled_ && d->server_->isListening())
    {
        logMessage(QSL("StreamServer is disabled, shutting down"));
        shutdown();
        return;
    }

    if (!d->enabled_)
        return;

    if (d->serverSettings == prevServerSettings)
    {
        d->logger_->trace("MvmeStreamServer::reloadConfiguration(): no relevant config changes");
        return;
    }

    d->listenUris_.clear();

    for (const auto &qtUri: settings.value(QSL("ListenUris")).toStringList())
    {
        auto expanded = util::expand_env_vars(qtUri.toStdString());
        d->listenUris_.emplace_back(expanded);
    }

    shutdown();

    if (!d->listenUris_.empty() && d->enabled_)
    {
        startup();
    }
}

} // namespace mesytec::mvme
