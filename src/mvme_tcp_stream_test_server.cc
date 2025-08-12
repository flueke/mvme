#if 0
#include <asio.hpp>
#include <cassert>
#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/protected.h>
#include <mesytec-mvlc/util/signal_handling.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

using asio::ip::tcp;
using namespace mesytec;
using namespace mesytec::mvlc;


// Run an acceptor in a thread.
// Run a fake data producer in a separate function.
// This function must return after data has been sent (it simulates processBuffer()).
// Unlimited number of clients.
// Each receives the next bunch of data. Either the complete data or nothing is sent.
// Remove clients that cause write errors/timeouts.

struct TcpServer
{
    TcpServer(asio::io_context &ioContext, tcp::acceptor &acceptor)
        : ioContext_(ioContext)
        , acceptor_(acceptor)
    {
    }

    asio::io_context &ioContext_;
    tcp::acceptor &acceptor_;
    mvlc::Protected<std::vector<tcp::socket>> sockets_;
    std::optional<tcp::socket> socket_;

    void start_accept()
    {
        if (!socket_.has_value())
        {
            spdlog::info("Creating new socket for acceptor");
            socket_ = tcp::socket(ioContext_);
        }

        acceptor_.async_accept(*socket_, [this](const std::error_code &ec) { handle_accept(ec); });
    }

    void handle_accept(const asio::error_code &ec)
    {
        if (!ec)
        {
            sockets_.access().ref().emplace_back(std::move(*socket_));
            socket_.reset();
            start_accept(); // Start accepting the next connection
        }
        else
        {
            spdlog::error("Accept error: {}", ec.message());
        }
    }
};

void run_acceptor(TcpServer &server)
{
#ifdef __linux__
    prctl(PR_SET_NAME,"tcp_stream_server_acceptor", 0,0,0);
#endif

    while (true)
    {
        try
        {
            server.start_accept();
            spdlog::info("acceptor: start_accept() returned");
            auto handlerCount = server.ioContext_.run();
            spdlog::info("acceptor: io_context.run() returned, continuing to accept new connections, handlerCount={}",
                handlerCount);
        }
        catch (const std::exception &e)
        {
            mvlc::get_logger("TcpServer")->error("Accept error: {}", e.what());
        }
    }
    spdlog::info("Acceptor thread exiting.");
}

void send_buffer_to_clients(mvlc::Protected<std::vector<tcp::socket>> &sockets,
                            size_t bufferNumber, const u32 *buffer, size_t bufferSize)
{
    assert(bufferSize <= std::numeric_limits<u32>::max());
    u32 bufferSizeU32 = static_cast<u32>(bufferSize);

    auto a = asio::buffer(&bufferNumber, sizeof(bufferNumber));
    auto b = asio::buffer(&bufferSizeU32, sizeof(bufferSizeU32));
    auto c = asio::buffer(reinterpret_cast<const u8 *>(buffer), bufferSize * sizeof(u32));
    std::array<asio::const_buffer, 3> buffers = { a, b, c };

    // send loop. this is in processBuffer() which must return once all clients have been served
    for (auto &socket: sockets.access().ref())
    {
        try
        {
            asio::error_code ec;
            auto bytesWritten = asio::write(socket, buffers, ec);

            if (ec)
            {
                spdlog::error("Failed to send buffer to client {}: {}",
                    socket.remote_endpoint().address().to_string(), ec.message());
                // Handle error (e.g., log it, remove socket from list)
            }
            else
            {
                spdlog::info("Sent buffer {} of size {} to client {}. bytesWritten={}",
                    bufferNumber, bufferSize * sizeof(u32),
                    socket.remote_endpoint().address().to_string(),
                    bytesWritten);
            }
        }
        catch (const std::exception &e)
        {
            // Handle exception (e.g., log it)
            spdlog::error("Exception while sending buffer to client {}: {}",
                socket.remote_endpoint().address().to_string(), e.what());
        }
    }
}

int main(int argc, char *argv[])
{
    mvlc::util::setup_signal_handlers();
    mvlc::set_global_log_level(spdlog::level::info);

    std::string host = "127.0.0.1";
    std::string port = "42333";

    if (argc > 1)
        host = argv[1];

    if (argc > 2)
        port = argv[2];

    try
    {
        asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(asio::ip::make_address(host), std::stoi(port)));
        TcpServer server(io_context, acceptor);

        std::thread acceptorThread(run_acceptor, std::ref(server));

        u32 bufferNumber = 0;
        std::vector<u32> fakeBuffer = { 1, 2, 3, 4, 5 }; // Example buffer data

        while (true)
        {
            if (util::signal_received())
            {
                spdlog::info("Signal received, shutting down server.");
                break;
            }

            send_buffer_to_clients(server.sockets_, bufferNumber, fakeBuffer.data(), fakeBuffer.size());
            spdlog::info("Sent buffer {} of size {} to {} clients",
                bufferNumber, fakeBuffer.size() * sizeof(std::uint32_t),
                server.sockets_.access().ref().size());
            ++bufferNumber;
            std::for_each(std::begin(fakeBuffer), std::end(fakeBuffer), [](u32 &val) { val++; });
            std::this_thread::sleep_for(std::chrono::seconds(1)); // Simulate some delay
        }

        spdlog::info("main: left loop, shutting down server.");

        acceptor.cancel();
        acceptor.close();
        io_context.stop();
        if (acceptorThread.joinable())
        {
            acceptorThread.join();
            spdlog::info("Acceptor thread joined.");
        }
    }
    catch(const std::exception& e)
    {
        spdlog::error("main send loop error: {}", e.what());
    }
}
#endif

#if 0
//
// server.cpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2024 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <asio.hpp>
#include <ctime>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

using asio::ip::tcp;

std::string make_daytime_string()
{
  using namespace std; // For time_t, time and ctime;
  time_t now = time(0);
  return ctime(&now);
}

class tcp_connection
  : public std::enable_shared_from_this<tcp_connection>
{
public:
  typedef std::shared_ptr<tcp_connection> pointer;

  static pointer create(asio::io_context& io_context)
  {
    return pointer(new tcp_connection(io_context));
  }

  tcp::socket& socket()
  {
    return socket_;
  }

  void start()
  {
    message_ = make_daytime_string();

    asio::async_write(socket_, asio::buffer(message_),
        std::bind(&tcp_connection::handle_write, shared_from_this(),
          asio::placeholders::error,
          asio::placeholders::bytes_transferred));
  }

private:
  tcp_connection(asio::io_context& io_context)
    : socket_(io_context)
  {
  }

  void handle_write(const std::error_code& /*error*/,
      size_t /*bytes_transferred*/)
  {
  }

  tcp::socket socket_;
  std::string message_;
};

class tcp_server
{
public:
  tcp_server(asio::io_context& io_context)
    : io_context_(io_context),
      acceptor_(io_context, tcp::endpoint(tcp::v4(), 42333))
  {
    start_accept();
  }

private:
  void start_accept()
  {
    tcp_connection::pointer new_connection =
      tcp_connection::create(io_context_);

    acceptor_.async_accept(new_connection->socket(),
        std::bind(&tcp_server::handle_accept, this, new_connection,
          asio::placeholders::error));
  }

  void handle_accept(tcp_connection::pointer new_connection,
      const std::error_code& error)
  {
    if (!error)
    {
      new_connection->start();
    }

    start_accept();
  }

  asio::io_context& io_context_;
  tcp::acceptor acceptor_;
};

int main()
{
  try
  {
    asio::io_context io_context;
    tcp_server server(io_context);
    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
#endif

// Based on
// https://beej.us/guide/bgnet/html/split/client-server-background.html#a-simple-stream-server
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/protected.h>
#include <mesytec-mvlc/util/signal_handling.h>
#include <mesytec-mvlc/util/stopwatch.h>
#include <set>
#include <system_error>

using namespace mesytec;
using namespace mesytec::mvlc;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// https://stackoverflow.com/a/1549344
bool set_socket_blocking(int sock, bool blocking)
{
    if (sock < 0)
        return false;

#ifdef _WIN32
    unsigned long mode = blocking ? 0 : 1;
    return (ioctlsocket(fd, FIONBIO, &mode) == 0);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1)
        return false;
    flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return (fcntl(sock, F_SETFL, flags) == 0);
#endif
}

struct timeval ms_to_timeval(unsigned ms)
{
    unsigned seconds = ms / 1000;
    ms -= seconds * 1000;

    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = ms * 1000;

    return tv;
}

std::error_code set_socket_timeout(int optname, int sock, unsigned ms)
{
    struct timeval tv = ms_to_timeval(ms);

    int res = setsockopt(sock, SOL_SOCKET, optname, &tv, sizeof(tv));

    if (res != 0)
        return std::error_code(errno, std::system_category());

    return {};
}

std::error_code set_socket_write_timeout(int sock, unsigned ms)
{
    return set_socket_timeout(SO_SNDTIMEO, sock, ms);
}

std::error_code set_socket_read_timeout(int sock, unsigned ms)
{
    return set_socket_timeout(SO_RCVTIMEO, sock, ms);
}

std::error_code run_acceptor(int serverSocket, mvlc::Protected<std::set<int>> &sockets, std::atomic<bool> &quit)
{
    while (!quit)
    {
        // Note: select() modifies the timeval struct. Have to reset it each time!
        struct timeval tv = ms_to_timeval(50);
        // use select to determine if accept() will (most likely) get a new connection
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(serverSocket, &fds);
        int sres = ::select(serverSocket + 1, &fds, nullptr, nullptr, &tv);

        if (sres < 0)
        {
            perror("select");
            return std::error_code(errno, std::system_category());
        }
        else if (sres > 0)
        {
            struct sockaddr_storage clientAddress;
            socklen_t addrLen(sizeof(clientAddress));
            auto clientSocket =
                accept(serverSocket, reinterpret_cast<struct sockaddr *>(&clientAddress), &addrLen);

            spdlog::debug("accept returned {}", clientSocket);

            if (clientSocket < 0)
            {
                perror("accept");
                continue;
            }

            char s[INET6_ADDRSTRLEN];
            inet_ntop(clientAddress.ss_family, get_in_addr((struct sockaddr *)&clientAddress), s,
                      sizeof s);
            spdlog::info("server: got connection from {}\n", s);
            sockets.access()->insert(clientSocket);
        }
    }

    return {};
}

std::error_code write_to_socket(
    int socket, const u8 *buffer, size_t size, size_t &bytesTransferred)
{
    bytesTransferred = 0;

    ssize_t res = ::send(socket, reinterpret_cast<const char *>(buffer), size, 0);

    if (res < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return std::error_code(EAGAIN, std::system_category());

        return std::error_code(errno, std::system_category());
    }

    bytesTransferred = res;
    return {};
}

int main()
{
    mvlc::set_global_log_level(spdlog::level::debug);
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (serverSocket < 0)
    {
        return -1;
    }

    if (!set_socket_blocking(serverSocket, false))
    {
        return -1;
    }

    if (auto ec = set_socket_write_timeout(serverSocket, 500))
    {
        spdlog::error("set_socket_write_timeout failed: {}", ec.message());
        return -1;
    }

    if (auto ec = set_socket_read_timeout(serverSocket, 500))
    {
        spdlog::error("set_socket_read_timeout failed: {}", ec.message());
        return -1;
    }

    int yes = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        perror("setsockopt");
        return -1;
    }

    struct sockaddr_in localAddr = {};
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(42333);

    if (::bind(serverSocket, reinterpret_cast<struct sockaddr *>(&localAddr), sizeof(localAddr)))
    {
        // ec = std::error_code(errno, std::system_category());
        // close_socket(sock);
        return -1;
    }

    const int BACKLOG = 10;

    if (::listen(serverSocket, BACKLOG))
    {
        return -1;
    }

    mvlc::Protected<std::set<int>> clientSockets;

    std::atomic<bool> quitAcceptor = false;
    std::thread acceptorThread(run_acceptor, serverSocket, std::ref(clientSockets), std::ref(quitAcceptor));

    std::vector<u32> fakeBuffer = { 1, 2, 3, 4, 5 }; // Example buffer data
    u32 bufferNumber = 0;
    u32 bufferSize = fakeBuffer.size();
    std::set<int> clientSocketsToRemove;

    while (!util::signal_received())
    {
        for (const auto &clientSocket: clientSockets.access().ref())
        {
            std::error_code ec;
            size_t bytesTransferred = 0;
            if (ec = write_to_socket(clientSocket, reinterpret_cast<const u8 *>(&bufferNumber), sizeof(bufferNumber), bytesTransferred), ec)
            {
                spdlog::error("Failed to send buffer number to client {}: {}", clientSocket, ec.message());
                goto fail;
            }

            if (ec = write_to_socket(clientSocket, reinterpret_cast<const u8 *>(&bufferSize), sizeof(bufferSize), bytesTransferred), ec)
            {
                spdlog::error("Failed to send buffer size to client {}: {}", clientSocket, ec.message());
                goto fail;
            }

            if (ec = write_to_socket(clientSocket, reinterpret_cast<const u8 *>(fakeBuffer.data()), fakeBuffer.size() * sizeof(u32), bytesTransferred), ec)
            {
                spdlog::error("Failed to send buffer to client {}: {}", clientSocket, ec.message());
                goto fail;
            }

            fail:
                if (ec)
                {
                    clientSocketsToRemove.insert(clientSocket);
                }
        }

        spdlog::info("Sent buffer {} of size {} to {} clients",
                     bufferNumber, bufferSize * sizeof(u32), clientSockets.access().ref().size());
        ++bufferNumber;
        std::for_each(std::begin(fakeBuffer), std::end(fakeBuffer), [](u32 &val) { val++; });

        spdlog::info("Removing {} dead clients", clientSocketsToRemove.size());

        auto socketsAccess = clientSockets.access();
        std::for_each(std::begin(clientSocketsToRemove), std::end(clientSocketsToRemove),
            [](int socket) { ::close(socket); });
        std::for_each(std::begin(clientSocketsToRemove), std::end(clientSocketsToRemove),
            [&socketsAccess](int socket) { socketsAccess->erase(socket); });
    }

    if (acceptorThread.joinable())
    {
        quitAcceptor = true;
        acceptorThread.join();
    }
}
