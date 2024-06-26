#include "mesy_nng_pipeline.h"

namespace mesytec::nng
{

bool SocketLink::operator==(const SocketLink &o) const
{
    return nng_socket_id(listener) == nng_socket_id(o.listener) &&
           nng_socket_id(dialer) == nng_socket_id(o.dialer) &&
           url == o.url;
}

std::pair<std::vector<SocketLink>, int> build_socket_pipeline(const std::vector<CreateLinkInfo> &linkInfos)
{
    std::vector<SocketLink> links;

    for (const auto &info: linkInfos)
    {
        SocketLink link;
        int res = 0;

        switch (info.type)
        {
            case LinkType::Pair:
                std::tie(link, res) = make_pair_link(info.url);
                break;

            case LinkType::PubSub:
                std::tie(link, res) = make_pubsub_link(info.url);
                break;
        }

        if (res)
        {
            close_links(links);
            return std::make_pair(links, res);
        }

        links.emplace_back(link);
    }

    return std::make_pair(links, 0);
}

}
