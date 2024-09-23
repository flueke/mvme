#include <gtest/gtest.h>
#include <mesytec-mvlc/util/string_util.h>
#include <mesytec-mvlc/util/logging.h>

#include "multi_crate_nng.h"

using namespace mesytec;
using namespace mesytec::mvme;
using namespace mesytec::mvme::multi_crate;

// Cannot really check the protocol numbers (NNG_OPT_PROTO) as they are somewhat
// hidden in nng or I didn't manage to find them. It seems pair0 is 1*16, pubsub
// is 2*16.
static const int PAIR0_PROTO  = 16 * 1;
static const int PUBSUB_PROTO = 16 * 2;


TEST(MultiCrateNng, socket_links_ok)
{
    {
        auto [pair0, res0] = nng::make_pair_link("inproc://test_pair0");
        ASSERT_EQ(res0, 0);
        ASSERT_TRUE(nng_socket_id(pair0.listener) > 0);
        ASSERT_TRUE(nng_socket_id(pair0.dialer) > 0);

        auto [pair1, res1] = nng::make_pair_link("inproc://test_pair1");
        ASSERT_EQ(res1, 0);
        ASSERT_TRUE(nng_socket_id(pair1.listener) > 0);
        ASSERT_TRUE(nng_socket_id(pair1.dialer) > 0);

        nng::close_link(pair0);
        nng::close_link(pair1);
    }
}

TEST(MultiCrateNng, socket_links_duplicate_url)
{
    {
        auto [pair0, res0] = nng::make_pair_link("inproc://test_pair0");
        ASSERT_EQ(res0, 0);
        ASSERT_TRUE(nng_socket_id(pair0.listener) > 0);
        ASSERT_TRUE(nng_socket_id(pair0.dialer) > 0);

        auto [pair1, res1] = nng::make_pair_link("inproc://test_pair0");
        ASSERT_NE(res1, 0);
        ASSERT_TRUE(nng_socket_id(pair1.listener) < 0);
        ASSERT_TRUE(nng_socket_id(pair1.dialer) < 0);

        nng::close_link(pair0);
    }
}

TEST(MultiCrateNng, build_stage_socket_links)
{
    // replay and all components enabled
    {
        Stage1BuildInfo info;
        info.uniqueUrlPart = "foo_";
        info.isReplay = true;
        info.withSplitter = true;
        info.withEventBuilder = true;
        info.crateId = 3;

        auto [links, res] = build_stage1_socket_links(info);
        ASSERT_EQ(res, 0);
        ASSERT_EQ(links.size(), 4);

        for (const auto &link: links)
        {
            ASSERT_TRUE(mvlc::util::startswith(link.url, fmt::format("inproc://{}", info.uniqueUrlPart)));
            ASSERT_TRUE(nng_socket_id(link.listener) != 0);
            ASSERT_TRUE(nng_socket_id(link.dialer) != 0);

            int protoNumber = 0;
            nng_socket_get_int(link.listener, NNG_OPT_PROTO, &protoNumber);
            ASSERT_EQ(protoNumber, PAIR0_PROTO);
        }
        ASSERT_EQ(nng::close_links(links), 0);
    }

    // no replay and all components enabled
    {
        Stage1BuildInfo info;
        info.uniqueUrlPart = "bar_";
        info.isReplay = false;
        info.withSplitter = true;
        info.withEventBuilder = true;
        info.crateId = 4;

        auto [links, res] = build_stage1_socket_links(info);
        ASSERT_EQ(res, 0);
        ASSERT_EQ(links.size(), 4);

        for (const auto &link: links)
        {
            ASSERT_TRUE(mvlc::util::startswith(link.url, fmt::format("inproc://{}", info.uniqueUrlPart)));
            ASSERT_TRUE(nng_socket_id(link.listener) != 0);
            ASSERT_TRUE(nng_socket_id(link.dialer) != 0);

            int protoNumber = 0;
            nng_socket_get_int(link.listener, NNG_OPT_PROTO, &protoNumber);

            if (link == *links.begin())
                ASSERT_EQ(protoNumber, PUBSUB_PROTO);
            else
                ASSERT_EQ(protoNumber, PAIR0_PROTO);
        }
        ASSERT_EQ(nng::close_links(links), 0);
    }

    // no replay, only splitter enabled
    {
        Stage1BuildInfo info;
        info.uniqueUrlPart = "bar_";
        info.isReplay = false;
        info.withSplitter = true;
        info.withEventBuilder = false;
        info.crateId = 5;

        auto [links, res] = build_stage1_socket_links(info);
        ASSERT_EQ(res, 0);
        ASSERT_EQ(links.size(), 3);

        for (const auto &link: links)
        {
            ASSERT_TRUE(mvlc::util::startswith(link.url, fmt::format("inproc://{}", info.uniqueUrlPart)));
            ASSERT_TRUE(nng_socket_id(link.listener) != 0);
            ASSERT_TRUE(nng_socket_id(link.dialer) != 0);

            int protoNumber = 0;
            nng_socket_get_int(link.listener, NNG_OPT_PROTO, &protoNumber);

            if (link == *links.begin())
                ASSERT_EQ(protoNumber, PUBSUB_PROTO);
            else
                ASSERT_EQ(protoNumber, PAIR0_PROTO);
        }
        ASSERT_EQ(nng::close_links(links), 0);
    }

    // no replay, only eb enabled
    {
        Stage1BuildInfo info;
        info.uniqueUrlPart = "bar_";
        info.isReplay = false;
        info.withSplitter = false;
        info.withEventBuilder = true;
        info.crateId = 6;

        auto [links, res] = build_stage1_socket_links(info);
        ASSERT_EQ(res, 0);
        ASSERT_EQ(links.size(), 3);

        for (const auto &link: links)
        {
            ASSERT_TRUE(mvlc::util::startswith(link.url, fmt::format("inproc://{}", info.uniqueUrlPart)));
            ASSERT_TRUE(nng_socket_id(link.listener) != 0);
            ASSERT_TRUE(nng_socket_id(link.dialer) != 0);

            int protoNumber = 0;
            nng_socket_get_int(link.listener, NNG_OPT_PROTO, &protoNumber);

            if (link == *links.begin())
                ASSERT_EQ(protoNumber, PUBSUB_PROTO);
            else
                ASSERT_EQ(protoNumber, PAIR0_PROTO);
        }
        ASSERT_EQ(nng::close_links(links), 0);
    }

    // no replay, neither splitter nor eb enabled
    {
        Stage1BuildInfo info;
        info.uniqueUrlPart = "bar_";
        info.isReplay = false;
        info.withSplitter = false;
        info.withEventBuilder = false;
        info.crateId = 7;

        auto [links, res] = build_stage1_socket_links(info);
        ASSERT_EQ(res, 0);
        ASSERT_EQ(links.size(), 2);

        for (const auto &link: links)
        {
            ASSERT_TRUE(mvlc::util::startswith(link.url, fmt::format("inproc://{}", info.uniqueUrlPart)));
            ASSERT_TRUE(nng_socket_id(link.listener) != 0);
            ASSERT_TRUE(nng_socket_id(link.dialer) != 0);

            int protoNumber = 0;
            nng_socket_get_int(link.listener, NNG_OPT_PROTO, &protoNumber);

            if (link == *links.begin())
                ASSERT_EQ(protoNumber, PUBSUB_PROTO);
            else
                ASSERT_EQ(protoNumber, PAIR0_PROTO);
        }
        ASSERT_EQ(nng::close_links(links), 0);
    }
}

TEST(MultiCrateNng, stress_recreate_links)
{
    // Repeatedly create, use and close a crate pipeline.

    const size_t MaxCycles = 1000;
    u32 messageNumber = 42;

    for (size_t cycle = 0; cycle < MaxCycles; ++cycle)
    {
        Stage1BuildInfo info;
        info.uniqueUrlPart = "foo_";
        info.isReplay = true;
        info.withSplitter = true;
        info.withEventBuilder = true;
        info.crateId = 3;

        auto [links, res] = build_stage1_socket_links(info);
        ASSERT_EQ(res, 0);
        ASSERT_EQ(links.size(), 4);

        for (const auto &link: links)
        {
            ASSERT_TRUE(mvlc::util::startswith(link.url, fmt::format("inproc://{}", info.uniqueUrlPart)));
            ASSERT_TRUE(nng_socket_id(link.listener) != 0);
            ASSERT_TRUE(nng_socket_id(link.dialer) != 0);

            int protoNumber = 0;
            nng_socket_get_int(link.listener, NNG_OPT_PROTO, &protoNumber);
            ASSERT_EQ(protoNumber, PAIR0_PROTO);
        }

        {
            for (auto &link: links)
            {
                // Create and send message through the link
                {
                    ParsedEventsMessageHeader header;
                    header.messageNumber = messageNumber++;
                    auto msg = allocate_prepare_message<ParsedEventsMessageHeader>(header);
                    ASSERT_NE(msg.get(), nullptr);
                    int res = nng::send_message_retry(link.listener, msg.get());
                    ASSERT_EQ(res, 0);
                    msg.release();
                }

                // Receive message from the link and check if it fits the one just sent.
                {
                    auto [msg, res] = nng::receive_message(link.dialer);
                    ASSERT_EQ(res, 0);
                    const auto msgLen = nng_msg_len(msg.get());
                    ASSERT_TRUE(msgLen >= sizeof(multi_crate::ParsedEventsMessageHeader));
                    auto inputHeader = nng::msg_trim_read<multi_crate::ParsedEventsMessageHeader>(msg.get()).value();
                    ASSERT_EQ(inputHeader.messageType, MessageType::ParsedEvents);
                    ASSERT_EQ(inputHeader.messageNumber, messageNumber - 1);
                }
            }
        }

        ASSERT_EQ(nng::close_links(links), 0);
    }
}
