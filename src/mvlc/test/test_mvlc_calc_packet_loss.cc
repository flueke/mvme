/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <gtest/gtest.h>
#include <iostream>

#include <mesytec-mvlc/mvlc_impl_eth.h>
#include "typedefs.h"

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
