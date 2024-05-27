#include <gtest/gtest.h>

#include "typedefs.h"
#include "util/mesy_nng_pipeline2.h"

using namespace mesytec::nng;

inline unique_msg make_message(const std::string &data)
{
    nng_msg *msg = nullptr;
    if (int res = nng_msg_alloc(&msg, data.size()))
    {
        mesy_nng_error("nng_msg_alloc", res);
        return unique_msg(nullptr, nng_msg_free);
    }

    memcpy(nng_msg_body(msg), data.data(), data.size());
    return unique_msg(msg, nng_msg_free);
}

TEST(MesyNngPipeline2, SocketWriterReader)
{
    nng_socket s1 = make_pair_socket();
    nng_socket s2 = make_pair_socket();
    int res = 0;

    res = marry_listen_dial(s1, s2, "inproc://test");

    ASSERT_EQ(res, 0);

    SocketOutputWriter writer(s1);
    SocketInputReader reader(s2);

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

    auto writer0 = std::make_unique<SocketOutputWriter>(s00);
    auto writer1 = std::make_unique<SocketOutputWriter>(s10);

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

TEST(MesyNngPipeline2, SocketPipeline)
{
    // Last digit: 0 = input, 1 = output
    nng_socket s01 = { 0 }; // out0 - first stage producer

    nng_socket s10 = { 1 }; // in1  - second stage in
    nng_socket s11 = { 2 }; // out1 - second stage out

    nng_socket s20 = { 3 }; // in2  - third stage in
    nng_socket s21 = { 4 }; // out2 - third stage out

    nng_socket s30 = { 5 }; // in3 - last stage consumer

    // => Couples are (s01, s10), (s11, s20), (s21, s30)

    std::string url0_1 = "inproc://test0_1";
    std::string url1_2 = "inproc://test1_2";
    std::string url2_3 = "inproc://test2_3";

    SocketPipeline pipeline;

    {
        pipeline.addProducer(s01, url0_1);

        ASSERT_EQ(pipeline.pipeline.size(), 1);
        ASSERT_EQ(pipeline.couples.size(), 1);
        ASSERT_EQ(nng_socket_id(pipeline.pipeline[0].inputSocket), -1);
        ASSERT_EQ(nng_socket_id(pipeline.pipeline[0].outputSocket), nng_socket_id(s01));
        ASSERT_EQ(nng_socket_id(pipeline.couples[0].listener), nng_socket_id(s01));
        ASSERT_EQ(nng_socket_id(pipeline.couples[0].dialer), -1);
        ASSERT_EQ(pipeline.pipeline[0].inputUrl, "");
        ASSERT_EQ(pipeline.pipeline[0].outputUrl, url0_1);
        ASSERT_EQ(pipeline.couples[0].url, url0_1);
    }

    {
        pipeline.addElement(s10, s11, url0_1, url1_2);

        ASSERT_EQ(pipeline.pipeline.size(), 2);
        ASSERT_EQ(pipeline.couples.size(), 2);

        ASSERT_EQ(nng_socket_id(pipeline.pipeline[0].inputSocket), -1);
        ASSERT_EQ(nng_socket_id(pipeline.pipeline[0].outputSocket), nng_socket_id(s01));
        ASSERT_EQ(nng_socket_id(pipeline.couples[0].listener), nng_socket_id(s01));
        ASSERT_EQ(nng_socket_id(pipeline.couples[0].dialer), nng_socket_id(s10));
        ASSERT_EQ(pipeline.pipeline[0].inputUrl, "");
        ASSERT_EQ(pipeline.pipeline[0].outputUrl, url0_1);
        ASSERT_EQ(pipeline.couples[0].url, url0_1);

        ASSERT_EQ(nng_socket_id(pipeline.pipeline[1].inputSocket), nng_socket_id(s10));
        ASSERT_EQ(nng_socket_id(pipeline.pipeline[1].outputSocket), nng_socket_id(s11));
        ASSERT_EQ(nng_socket_id(pipeline.couples[1].listener), nng_socket_id(s11));
        ASSERT_EQ(nng_socket_id(pipeline.couples[1].dialer), -1);
        ASSERT_EQ(pipeline.pipeline[1].inputUrl, url0_1);
        ASSERT_EQ(pipeline.pipeline[1].outputUrl, url1_2);
        ASSERT_EQ(pipeline.couples[1].url, url1_2);
    }

    {
        pipeline.addElement(s20, s21, url1_2, url2_3);

        ASSERT_EQ(pipeline.pipeline.size(), 3);
        ASSERT_EQ(pipeline.couples.size(), 3);

        ASSERT_EQ(nng_socket_id(pipeline.pipeline[0].inputSocket), -1);
        ASSERT_EQ(nng_socket_id(pipeline.pipeline[0].outputSocket), nng_socket_id(s01));
        ASSERT_EQ(nng_socket_id(pipeline.couples[0].listener), nng_socket_id(s01));
        ASSERT_EQ(nng_socket_id(pipeline.couples[0].dialer), nng_socket_id(s10));
        ASSERT_EQ(pipeline.pipeline[0].inputUrl, "");
        ASSERT_EQ(pipeline.pipeline[0].outputUrl, url0_1);
        ASSERT_EQ(pipeline.couples[0].url, url0_1);

        ASSERT_EQ(nng_socket_id(pipeline.pipeline[1].inputSocket), nng_socket_id(s10));
        ASSERT_EQ(nng_socket_id(pipeline.pipeline[1].outputSocket), nng_socket_id(s11));
        ASSERT_EQ(nng_socket_id(pipeline.couples[1].listener), nng_socket_id(s11));
        ASSERT_EQ(nng_socket_id(pipeline.couples[1].dialer), nng_socket_id(s20));
        ASSERT_EQ(pipeline.pipeline[1].inputUrl, url0_1);
        ASSERT_EQ(pipeline.pipeline[1].outputUrl, url1_2);
        ASSERT_EQ(pipeline.couples[1].url, url1_2);

        ASSERT_EQ(nng_socket_id(pipeline.pipeline[2].inputSocket), nng_socket_id(s20));
        ASSERT_EQ(nng_socket_id(pipeline.pipeline[2].outputSocket), nng_socket_id(s21));
        ASSERT_EQ(nng_socket_id(pipeline.couples[2].listener), nng_socket_id(s21));
        ASSERT_EQ(nng_socket_id(pipeline.couples[2].dialer), -1);
        ASSERT_EQ(pipeline.pipeline[2].inputUrl, url1_2);
        ASSERT_EQ(pipeline.pipeline[2].outputUrl, url2_3);
        ASSERT_EQ(pipeline.couples[2].url, url2_3);
    }

    {
        pipeline.addConsumer(s30, url2_3);

        ASSERT_EQ(pipeline.pipeline.size(), 4);
        ASSERT_EQ(pipeline.couples.size(), 4);

        ASSERT_EQ(nng_socket_id(pipeline.pipeline[0].inputSocket), -1);
        ASSERT_EQ(nng_socket_id(pipeline.pipeline[0].outputSocket), nng_socket_id(s01));
        ASSERT_EQ(nng_socket_id(pipeline.couples[0].listener), nng_socket_id(s01));
        ASSERT_EQ(nng_socket_id(pipeline.couples[0].dialer), nng_socket_id(s10));
        ASSERT_EQ(pipeline.pipeline[0].inputUrl, "");
        ASSERT_EQ(pipeline.pipeline[0].outputUrl, url0_1);
        ASSERT_EQ(pipeline.couples[0].url, url0_1);

        ASSERT_EQ(nng_socket_id(pipeline.pipeline[1].inputSocket), nng_socket_id(s10));
        ASSERT_EQ(nng_socket_id(pipeline.pipeline[1].outputSocket), nng_socket_id(s11));
        ASSERT_EQ(nng_socket_id(pipeline.couples[1].listener), nng_socket_id(s11));
        ASSERT_EQ(nng_socket_id(pipeline.couples[1].dialer), nng_socket_id(s20));
        ASSERT_EQ(pipeline.pipeline[1].inputUrl, url0_1);
        ASSERT_EQ(pipeline.pipeline[1].outputUrl, url1_2);
        ASSERT_EQ(pipeline.couples[1].url, url1_2);

        ASSERT_EQ(nng_socket_id(pipeline.pipeline[2].inputSocket), nng_socket_id(s20));
        ASSERT_EQ(nng_socket_id(pipeline.pipeline[2].outputSocket), nng_socket_id(s21));
        ASSERT_EQ(nng_socket_id(pipeline.couples[2].listener), nng_socket_id(s21));
        ASSERT_EQ(nng_socket_id(pipeline.couples[2].dialer), nng_socket_id(s30));
        ASSERT_EQ(pipeline.pipeline[2].inputUrl, url1_2);
        ASSERT_EQ(pipeline.pipeline[2].outputUrl, url2_3);
        ASSERT_EQ(pipeline.couples[2].url, url2_3);

        ASSERT_EQ(nng_socket_id(pipeline.pipeline[3].inputSocket), nng_socket_id(s30));
        ASSERT_EQ(nng_socket_id(pipeline.pipeline[3].outputSocket), -1);
        ASSERT_EQ(nng_socket_id(pipeline.couples[3].listener), -1);
        ASSERT_EQ(nng_socket_id(pipeline.couples[3].dialer), -1);
        ASSERT_EQ(pipeline.pipeline[3].inputUrl, url2_3);
        ASSERT_EQ(pipeline.pipeline[3].outputUrl, "");
        ASSERT_EQ(pipeline.couples[3].url, "");
    }
}
