#include <gtest/gtest.h>

#include "typedefs.h"
#include "util/mesy_nng_pipeline2.h"

using namespace mesytec::nng;

inline unique_msg_handle make_message(const std::string &data)
{
    nng_msg *msg = nullptr;
    if (int res = nng_msg_alloc(&msg, data.size()))
    {
        mesy_nng_error("nng_msg_alloc", res);
        return unique_msg_handle(nullptr, nng_msg_free);
    }

    memcpy(nng_msg_body(msg), data.data(), data.size());
    return unique_msg_handle(msg, nng_msg_free);
}

TEST(MesyNngPipeline2, SocketWriterReader)
{
    nng_socket s1 = make_pair_socket();
    nng_socket s2 = make_pair_socket();
    int res = 0;

    res = marry_listen_dial(s1, s2, "inproc://test");

    ASSERT_EQ(res, 0);

    SocketOutputWriter writer;
    writer.socket = s1;

    SocketInputReader reader;
    reader.socket = s2;

    auto outMsg = make_message("1234");

    res = writer.writeMessage(std::move(outMsg));

    ASSERT_EQ(res, 0);

    auto [inMsg, res2] = reader.readMessage();

    ASSERT_EQ(res2, 0);

    ASSERT_EQ(nng_msg_len(inMsg.get()), 4);
    ASSERT_EQ(memcmp(nng_msg_body(inMsg.get()), "1234", 4), 0);
}

TEST(MesyNngPipeline2, SocketMultiOutputWriter)
{
    nng_socket s00 = make_pair_socket();
    nng_socket s01 = make_pair_socket();
    nng_socket s10 = make_pair_socket();
    nng_socket s11 = make_pair_socket();

    int res = 0;

    res = marry_listen_dial(s00, s01, "inproc://test0");
    ASSERT_EQ(res, 0);

    res = marry_listen_dial(s10, s11, "inproc://test1");
    ASSERT_EQ(res, 0);

    auto writer0 = std::make_unique<SocketOutputWriter>();
    writer0->socket = s00;

    auto writer1 = std::make_unique<SocketOutputWriter>();
    writer1->socket = s10;

    MultiOutputWriter multiWriter;
    multiWriter.addWriter(std::move(writer0));
    multiWriter.addWriter(std::move(writer1));

    auto outMsg = make_message("1234");
    res = multiWriter.writeMessage(std::move(outMsg));
    ASSERT_EQ(res, 0);

    auto [inMsg0, res0] = receive_message(s01);
    ASSERT_EQ(res0, 0);
    ASSERT_EQ(nng_msg_len(inMsg0.get()), 4);
    ASSERT_EQ(memcmp(nng_msg_body(inMsg0.get()), "1234", 4), 0);

    auto [inMsg1, res1] = receive_message(s11);
    ASSERT_EQ(res1, 0);
    ASSERT_EQ(nng_msg_len(inMsg1.get()), 4);
    ASSERT_EQ(memcmp(nng_msg_body(inMsg1.get()), "1234", 4), 0);
}
