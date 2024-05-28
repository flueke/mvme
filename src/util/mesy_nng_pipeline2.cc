#include "mesy_nng_pipeline2.h"

namespace mesytec::nng
{

    bool SocketPipeline::Element::operator==(const SocketPipeline::Element &o) const
    {
        return nng_socket_id(inputSocket) == nng_socket_id(o.inputSocket)
            && nng_socket_id(outputSocket) == nng_socket_id(o.outputSocket)
            && inputUrl == o.inputUrl
            && outputUrl == o.outputUrl;
    }

    bool SocketPipeline::Link::operator==(const SocketPipeline::Link &o) const
    {
        return nng_socket_id(listener) == nng_socket_id(o.listener)
            && nng_socket_id(dialer) == nng_socket_id(o.dialer)
            && url == o.url;
    }

}
