#include "mesy_nng_pipeline.h"

namespace mesytec::nng
{

std::pair<std::vector<SocketLink>, int> build_socket_pipeline(const PipelineBuildInfo &b)
{
    assert(b.urls.size() == b.linkTypes.size());

    std::vector<SocketLink> links;

    auto itUrl = b.urls.begin();
    auto itType = b.linkTypes.begin();

    for (; itUrl != b.urls.end() && itType != b.linkTypes.end(); ++itUrl, ++itType)
    {
        SocketLink link;
        int res = 0;
        auto url = fmt::format(*itUrl, b.uniqueId);

        switch (*itType)
        {
            case PipelineBuildInfo::LinkType::Pair:
                std::tie(link, res) = make_pair_link(url);
                break;

            case PipelineBuildInfo::LinkType::PubSub:
                std::tie(link, res) = make_pubsub_link(url);
                break;
        }

        if (res)
            return std::make_pair(links, res);
        links.emplace_back(link);
    }

    return std::make_pair(links, 0);
}

}
