#include "mvme_tcp_stream_server.h"
#include <asio.hpp>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/protected.h>

namespace mesytec::mvme
{

using asio::ip::tcp;

void run_acceptor(asio::io_context &ioContext, tcp::acceptor &acceptor,
                  mvlc::Protected<std::vector<tcp::socket>> &sockets, std::atomic<bool> &quit)
{
    while (!quit)
    {
        try
        {
            tcp::socket socket(ioContext);
            acceptor.accept(socket);
            sockets.access().ref().emplace_back(std::move(socket));
        }
        catch (const std::exception &e)
        {
            mvlc::get_logger("MvmeTcpStreamServer")->error("Accept error: {}", e.what());
        }
    }
}

struct MvmeTcpStreamServer::Private
{
    std::string address_;
    u16 port_;
    std::shared_ptr<spdlog::logger> logger_;

    asio::io_context ioContext_;
    std::unique_ptr<tcp::acceptor> acceptor_;
    mvlc::Protected<std::vector<tcp::socket>> sockets_;
    std::thread acceptorThread_;
    std::atomic<bool> quitAcceptor_{false};
    StreamConsumerBase::Logger mvmeLogger_;
};

MvmeTcpStreamServer::MvmeTcpStreamServer(const std::string &address, u16 port)
    : IStreamBufferConsumer()
    , d(std::make_unique<Private>())
{
    d->logger_ = mvlc::get_logger("MvmeTcpStreamServer");
    d->address_ = address;
    d->port_ = port;
}

MvmeTcpStreamServer::~MvmeTcpStreamServer()
{
    if (d->acceptorThread_.joinable())
    {
        d->quitAcceptor_ = true;
        d->acceptorThread_.join();
    }
}

void MvmeTcpStreamServer::startup()
{
    if (!d->acceptor_)
    {
        try
        {
            d->acceptor_ = std::make_unique<tcp::acceptor>(
                d->ioContext_, tcp::endpoint(asio::ip::make_address(d->address_), d->port_));

            if (d->acceptorThread_.joinable())
            {
                d->quitAcceptor_ = true;
                d->acceptorThread_.join();
            }

            d->quitAcceptor_ = false;
            d->acceptorThread_ = std::thread(run_acceptor, std::ref(d->ioContext_), std::ref(*d->acceptor_),
                                     std::ref(d->sockets_), std::ref(d->quitAcceptor_));
        }
        catch (const std::exception &e)
        {
            d->logger_->error("Failed to listen on {}:{}: {}", d->address_, d->port_, e.what());
        }
    }

}

void MvmeTcpStreamServer::shutdown()
{
    d->quitAcceptor_ = true;
    if (d->acceptorThread_.joinable())
        d->acceptorThread_.join();
}

void MvmeTcpStreamServer::beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig,
                                   const analysis::Analysis *analysis)
{
}

void MvmeTcpStreamServer::endRun(const DAQStats &stats, const std::exception *e) {}

void MvmeTcpStreamServer::processBuffer(s32 bufferType, u32 bufferNumber, const u32 *buffer,
                                        size_t bufferSize)
{
    assert(bufferSize <= std::numeric_limits<u32>::max());
    u32 bufferSizeU32 = static_cast<u32>(bufferSize);

    for (auto &socket: d->sockets_.access().ref())
    {
        try
        {
            auto a = asio::buffer(&bufferNumber, sizeof(bufferNumber));
            auto b = asio::buffer(&bufferSizeU32, sizeof(bufferSizeU32));
            auto c = asio::buffer(reinterpret_cast<const u8 *>(buffer), bufferSize * sizeof(u32));

            std::array<asio::const_buffer, 3> buffers = { a, b, c };
            asio::error_code ec;

            asio::write(socket, buffers, ec);

            if (ec)
            {
                d->logger_->error("Failed to send buffer to client {}: {}",
                    socket.remote_endpoint().address().to_string(), ec.message());
                continue;
            }
        }
        catch (const std::exception &e)
        {
            d->logger_->error("Failed to send buffer to client {}: {}",
                socket.remote_endpoint().address().to_string(), e.what());
        }
    }
}

void MvmeTcpStreamServer::setLogger(StreamConsumerBase::Logger logger)
{
    d->mvmeLogger_ = logger;
}

StreamConsumerBase::Logger &MvmeTcpStreamServer::getLogger()
{
    return d->mvmeLogger_;
}

} // namespace mesytec::mvme
