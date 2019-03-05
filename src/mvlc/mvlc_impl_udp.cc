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

const u16 FirstDynamicPort = 49152;

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

    // Copy address and replace the port with DataPort
    m_dataAddr = m_cmdAddr;
    m_dataAddr.sin_port = htons(DataPort);

    // Lookup succeeded and we have now have two remote addresses, one for the
    // command and one for the data pip.
    //
    // Now create two IPv4 UDP sockets and try to bind them to two consecutive
    // local ports.

    for (u16 localCmdPort = FirstDynamicPort;
         // Using less than to leave one spare port for the data pipe
         localCmdPort < std::numeric_limits<u16>::max();
         localCmdPort++)
    {
        // Not being able to create the sockets is treated as fatal and an
        // error is returned immediately.

        m_cmdSock = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_cmdSock < 0)
            return std::error_code(errno, std::system_category());

        m_dataSock = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_dataSock < 0)
        {
            if (m_cmdSock >= 0)
                close(m_cmdSock);
            return std::error_code(errno, std::system_category());
        }

        assert(m_cmdSock >= 0 && m_dataSock >= 0);

        // Setup the local address structures using two consecutive port numbers
        struct sockaddr_in cmdLocal = {};
        cmdLocal.sin_family = AF_INET;
        cmdLocal.sin_addr.s_addr = INADDR_ANY;
        cmdLocal.sin_port = htons(localCmdPort);

        struct sockaddr_in dataLocal = {};
        dataLocal = cmdLocal;
        dataLocal.sin_port = htons(cmdLocal.sin_port + 1);

        // Bind both sockets. In case of an error close the sockets and
        // continue with the loop.
        if (int res = ::bind(m_cmdSock, reinterpret_cast<struct sockaddr *>(&cmdLocal),
                             sizeof(cmdLocal)))
        {
            close(m_cmdSock);
            close(m_dataSock);
            continue;
        }

        if (int res = ::bind(m_dataSock, reinterpret_cast<struct sockaddr *>(&dataLocal),
                                  sizeof(dataLocal)))
        {
            close(m_cmdSock);
            close(m_dataSock);
            continue;
        }

        break;
    }

    return {};
}

std::error_code Impl::disconnect()
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    close(m_cmdSock);
    close(m_dataSock);
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
}

void Impl::setReadTimeout(Pipe pipe, unsigned ms)
{
}

unsigned Impl::getWriteTimeout(Pipe pipe) const
{
}

unsigned Impl::getReadTimeout(Pipe pipe) const
{
}

std::error_code Impl::write(Pipe pipe, const u8 *buffer, size_t size,
                            size_t &bytesTransferred)
{
}

std::error_code Impl::read(Pipe pipe, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
}

} // end namespace udp
} // end namespace mvlc
} // end namespace mesytec
