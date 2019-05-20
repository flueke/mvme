#include <gtest/gtest.h>
#include <iostream>

#include "mvlc/mvlc_impl_eth.h"

using namespace mesytec::mvlc::eth;

// Prototype is: s32 calc_packet_loss(u16 lastPacketNumber, u16 packetNumber);

static const u16 MaxNum = 0xfff;

TEST(CalcPacketLoss, NoLoss)
{
    ASSERT_EQ(calc_packet_loss(0, 1), 0);
    ASSERT_EQ(calc_packet_loss(1, 2), 0);

    // close to overflow
    ASSERT_EQ(calc_packet_loss(MaxNum-1, MaxNum), 0);

    // overflow from 0xfff to 0
    ASSERT_EQ(calc_packet_loss(MaxNum, 0), 0);
}

TEST(CalcPacketLoss, WithLoss)
{
    ASSERT_EQ(calc_packet_loss(0, 2), 1);
    ASSERT_EQ(calc_packet_loss(0, 10), 9);

    // loss of the MaxNum packet
    ASSERT_EQ(calc_packet_loss(MaxNum-1, 0), 1);

    // loss of the 0 packet
    ASSERT_EQ(calc_packet_loss(MaxNum, 1), 1);

    // extreme case where MaxNum packets are lost
    ASSERT_EQ(calc_packet_loss(0, 0), MaxNum);
    ASSERT_EQ(calc_packet_loss(MaxNum, MaxNum), MaxNum);
}
