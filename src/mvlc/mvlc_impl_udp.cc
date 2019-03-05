#include "mvlc/mvlc_impl_udp.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <limits>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "mvlc/mvlc_error.h"

namespace
{


// Does IPv4 host lookup for a UDP socket. On success the resulting struct
// sockaddr_in is copied to dest.
std::error_code lookup(const std::string &host, u16 port, sockaddr_in &dest)
{
    using namespace mesytec::mvlc;

    dest = {};
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *result = nullptr, *rp = nullptr;

    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(),
                         &hints, &result);

    // TODO: check getaddrinfo specific error codes. make and use getaddrinfo error category
    if (rc != 0)
        return make_error_code(MVLCErrorCode::HostLookupError);

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        if (rp->ai_addrlen == sizeof(dest))
        {
            std::memcpy(&dest, rp->ai_addr, rp->ai_addrlen);
            break;
        }
    }

    freeaddrinfo(result);

    if (!rp)
        return make_error_code(MVLCErrorCode::HostLookupError);

    return {};
}

void set_socket_timeout(int optname, int sock, unsigned ms)
{
    unsigned seconds = ms / 1000;
    ms -= seconds * 1000;

    struct timeval tv;
    tv.tv_sec  = seconds;
    tv.tv_usec = ms * 1000;

    int res = setsockopt(sock, SOL_SOCKET, optname, &tv, sizeof(tv));
    assert(res == 0);
}

void set_socket_write_timeout(int sock, unsigned ms)
{
    set_socket_timeout(SO_SNDTIMEO, sock, ms);
}

void set_socket_read_timeout(int sock, unsigned ms)
{
    set_socket_timeout(SO_RCVTIMEO, sock, ms);
}

const u16 FirstDynamicPort = 49152;
static const int SocketReceiveBufferSize = 1024 * 1024;

} // end anon namespace

namespace mesytec
{
namespace mvlc
{
namespace udp
{

Impl::Impl(const std::string &host)
    : m_host(host)
{
}

Impl::~Impl()
{
    disconnect();
}

// A note about using ::bind() and then ::connect():
//
// Under linux this has the effect of changing the local bound address from
// INADDR_ANY to the address of the interface that's used to reach the remote
// address. E.g. when connecting to localhost the following will happen: after
// the bind() call the local "listen" address will be 0.0.0.0, after the
// connect() call this will change to 127.0.0.1. The local port specified in
// the bind() call will be kept. This is nice.

// Things happening in Impl::connect:
// * Remote host lookup to get the IPv4 address of the MVLC.
// * Create two UDP sockets and bind them to two consecutive local ports.
//   Ports are tried starting from FirstDynamicPort (49152).
// * Use ::connect() on both sockets with the MVLC address and the default
//   command and data ports. This way the sockets will only receive datagrams
//   originating from the MVLC.
// * TODO: Send an initial request and read the response. Preferably this
//   should tells us if another client is currently using the MVLC. It could be
//   some sort of "DAQ mode register" or a way to check where the MVLC is
//   currently sending its data output.
std::error_code Impl::connect()
{
    if (isConnected())
        return make_error_code(MVLCErrorCode::IsConnected);

    m_cmdSock = -1;
    m_dataSock = -1;

    // lookup remote host
    // create and bind two UDP sockets on consecutive local ports
    // => cmd and data sockets
    // send an initial request on the data socket, receive the response or timeout
    // verify the response
    // if ok: set connected and return success
    // else return error

    if (auto ec = lookup(m_host, CommandPort, m_cmdAddr))
        return ec;

    assert(m_cmdAddr.sin_port == htons(CommandPort));

    // Copy address and replace the port with DataPort
    m_dataAddr = m_cmdAddr;
    m_dataAddr.sin_port = htons(DataPort);

    // Lookup succeeded and we have now have two remote addresses, one for the
    // command and one for the data pipe.
    //
    // Now create two IPv4 UDP sockets and try to bind them to two consecutive
    // local ports.
    for (u16 localCmdPort = FirstDynamicPort;
         // Using 'less than' to leave one spare port for the data pipe
         localCmdPort < std::numeric_limits<u16>::max();
         localCmdPort++)
    {
        assert(m_cmdSock < 0 && m_dataSock < 0);

        // Not being able to create the sockets is considered a fatal error.

        m_cmdSock = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_cmdSock < 0)
            return std::error_code(errno, std::system_category());

        m_dataSock = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_dataSock < 0)
        {
            if (m_cmdSock >= 0)
            {
                ::close(m_cmdSock);
                m_cmdSock = -1;
            }
            return std::error_code(errno, std::system_category());
        }

        assert(m_cmdSock >= 0 && m_dataSock >= 0);

        // Setup the local address structures using two consecutive port
        // numbers.
        struct sockaddr_in cmdLocal = {};
        cmdLocal.sin_family = AF_INET;
        cmdLocal.sin_addr.s_addr = INADDR_ANY;
        cmdLocal.sin_port = htons(localCmdPort);

        struct sockaddr_in dataLocal = cmdLocal;
        dataLocal.sin_port = htons(localCmdPort + 1);

        // Bind both sockets. In case of an error close the sockets and
        // continue with the loop.
        if (int res = ::bind(m_cmdSock, reinterpret_cast<struct sockaddr *>(&cmdLocal),
                             sizeof(cmdLocal)))
        {
            goto try_again;
        }

        if (int res = ::bind(m_dataSock, reinterpret_cast<struct sockaddr *>(&dataLocal),
                             sizeof(dataLocal)))
        {
            goto try_again;
        }

        break;

        try_again:
        {
            ::close(m_cmdSock);
            ::close(m_dataSock);
            m_cmdSock = -1;
            m_dataSock = -1;
        }
    }

    if (m_cmdSock < 0 || m_dataSock < 0)
        return make_error_code(MVLCErrorCode::BindLocalError);

    // Call connect on the sockets so that we receive only datagrams from the
    // MVLC.
    if (int res = ::connect(m_cmdSock, reinterpret_cast<struct sockaddr *>(&m_cmdAddr),
                            sizeof(m_cmdAddr)))
    {
        ::close(m_cmdSock);
        ::close(m_dataSock);
        m_cmdSock = -1;
        m_dataSock = -1;
        return std::error_code(errno, std::system_category());
    }

    if (int res = ::connect(m_dataSock, reinterpret_cast<struct sockaddr *>(&m_dataAddr),
                            sizeof(m_dataAddr)))
    {
        ::close(m_cmdSock);
        ::close(m_dataSock);
        m_cmdSock = -1;
        m_dataSock = -1;
        return std::error_code(errno, std::system_category());
    }

    // Set read and write timeouts
    for (auto pipe: { Pipe::Command, Pipe::Data })
    {
        set_socket_write_timeout(getSocket(pipe), getWriteTimeout(pipe));
        set_socket_read_timeout(getSocket(pipe), getReadTimeout(pipe));
    }

    // Set socket receive buffer size
    for (auto pipe: { Pipe::Command, Pipe::Data })
    {
        int res = setsockopt(getSocket(pipe), SOL_SOCKET, SO_RCVBUF,
                             &SocketReceiveBufferSize, sizeof(SocketReceiveBufferSize));
        assert(res == 0);
    }

    // TODO: send the initial request to verify there's an MVLC on the other side

    assert(m_cmdSock >= 0 && m_dataSock >= 0);
    return {};
}

std::error_code Impl::disconnect()
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    ::close(m_cmdSock);
    ::close(m_dataSock);
    m_cmdSock = -1;
    m_dataSock = -1;
    return {};
}

bool Impl::isConnected() const
{
    return m_cmdSock >= 0 && m_dataSock >= 0;
}

void Impl::setWriteTimeout(Pipe pipe, unsigned ms)
{
    if (static_cast<unsigned>(pipe) >= PipeCount) return;
    m_writeTimeouts[static_cast<unsigned>(pipe)] = ms;
    if (isConnected())
        set_socket_write_timeout(getSocket(pipe), ms);
}

void Impl::setReadTimeout(Pipe pipe, unsigned ms)
{
    if (static_cast<unsigned>(pipe) >= PipeCount) return;
    m_readTimeouts[static_cast<unsigned>(pipe)] = ms;
    if (isConnected())
        set_socket_read_timeout(getSocket(pipe), ms);
}

unsigned Impl::getWriteTimeout(Pipe pipe) const
{
    if (static_cast<unsigned>(pipe) >= PipeCount) return 0u;
    return m_writeTimeouts[static_cast<unsigned>(pipe)];
}

unsigned Impl::getReadTimeout(Pipe pipe) const
{
    if (static_cast<unsigned>(pipe) >= PipeCount) return 0u;
    return m_readTimeouts[static_cast<unsigned>(pipe)];
}

// FIXME: this is wrong and also depends on the Jumbo Frames option
static const size_t DatagramMaxPayloadSize = 9000;

std::error_code Impl::write(Pipe pipe, const u8 *buffer, size_t size,
                            size_t &bytesTransferred)
{
    // Note: it should not be necessary to split this into multiple calls to
    // send() because outgoing MVLC command buffers should be smaller than the
    // maximum ethernet MTU.
    assert(buffer);
    assert(size <= DatagramMaxPayloadSize);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    bytesTransferred = 0;

    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    ssize_t res = ::send(getSocket(pipe), buffer, size, 0);

    if (res < 0)
        return std::error_code(errno, std::system_category());

    bytesTransferred = res;
    return {};
}

std::error_code Impl::read(Pipe pipe, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    // TODO: split into multiple read calls

    assert(buffer);
    assert(size <= DatagramMaxPayloadSize);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    bytesTransferred = 0;

    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    ssize_t res = ::recv(getSocket(pipe), buffer, size, 0);

    if (res < 0)
        return std::error_code(errno, std::system_category());

    bytesTransferred = res;
    return {};
}

} // end namespace udp
} // end namespace mvlc
} // end namespace mesytec
