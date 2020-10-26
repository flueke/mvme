#include <gtest/gtest.h>
#include "multi_event_splitter.h"

using namespace mvme::multi_event_splitter;

TEST(MultiEventSplitter, OneEventTwoModules)
{
    std::vector<std::string> filters =
    {
        "1111 SSSS",
        "1111 SSSS",
    };

    auto splitter = make_splitter({ filters });
}
