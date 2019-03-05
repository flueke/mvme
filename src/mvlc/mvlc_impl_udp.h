#ifndef __MVLC_IMPL_UDP_H__
#define __MVLC_IMPL_UDP_H__

#include <netinet/ip.h>
#include <string>
#include "mvlc/mvlc_impl_abstract.h"

namespace mesytec
{
namespace mvlc
{
namespace udp
{

// TODO
// How will the MVLC handle the case where multiple "connections" are
// made to the command pipe? Anyone attempting to connect should at least be
// able to read a register to determine whether a DAQ is active. The response
// to this read request must not interfere in any way with a possibly running
// DAQ: the response must only be sent to the client socket, no additional data
// should be sent to any other clients, the data pipe should not sent out
// additional information.
//
// Make a std::error_category for getaddrinfo (enum, category, make_error_code).
//
// If keeping the method of binding the local sockets to consecutive ports try
// to find a way to only bind to the correct interface. This seems cleaner than
// specifying INADDR_ANY.

class Impl: public AbstractImpl
{
    public:
        explicit Impl(const std::string &host);
        ~Impl();

        std::error_code connect() override;
        std::error_code disconnect() override;
        bool isConnected() const override;

        void setWriteTimeout(Pipe pipe, unsigned ms) override;
        void setReadTimeout(Pipe pipe, unsigned ms) override;

        unsigned getWriteTimeout(Pipe pipe) const override;
        unsigned getReadTimeout(Pipe pipe) const override;

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred) override;

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred) override;

    private:
        std::string m_host;
        bool m_isConnected = false;
        int m_cmdSock = -1;
        int m_dataSock = -1;
        struct sockaddr_in m_cmdAddr = {};
        struct sockaddr_in m_dataAddr = {};
};

} // end namespace udp
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_IMPL_UDP_H__ */
