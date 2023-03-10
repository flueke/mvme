#include <gtest/gtest.h>
#include <limits>
#include "a2_support.h"
#include <mesytec-mvlc/util/logging.h>

using namespace a2;

TEST(a2_filter_datasources, convert_to_signed)
{
    ASSERT_EQ(convert_to_signed(0xffu, 8), -1);
    ASSERT_EQ(convert_to_signed(0x80u, 8), -128);
    ASSERT_EQ(convert_to_signed(0x00u, 8), 0);
    ASSERT_EQ(convert_to_signed(0x01u, 8), 1);
    ASSERT_EQ(convert_to_signed(0x7fu, 8), 0x7f);


    ASSERT_EQ(convert_to_signed(0xffffu, 16), -1);
    ASSERT_EQ(convert_to_signed(0x8000u, 16), -32768);
    ASSERT_EQ(convert_to_signed(0x0000u, 16), 0);
    ASSERT_EQ(convert_to_signed(0x0001u, 16), 1);
    ASSERT_EQ(convert_to_signed(0x7fffu, 16), 0x7fff);

    ASSERT_EQ(convert_to_signed(0xffffffffu, 32), -1);
    ASSERT_EQ(convert_to_signed(0x80000000u, 32), std::numeric_limits<s32>::lowest());
    ASSERT_EQ(convert_to_signed(0x00000000u, 32), 0);
    ASSERT_EQ(convert_to_signed(0x00000001u, 32), 1);
    ASSERT_EQ(convert_to_signed(0x7fffffffu, 32), std::numeric_limits<s32>::max());

    ASSERT_EQ(convert_to_signed(0xffffffffffffffffu, 64), -1);
    ASSERT_EQ(convert_to_signed(0x8000000000000000u, 64), std::numeric_limits<s64>::lowest());
    ASSERT_EQ(convert_to_signed(0x0000000000000000u, 64), 0);
    ASSERT_EQ(convert_to_signed(0x0000000000000001u, 64), 1);
    ASSERT_EQ(convert_to_signed(0x7fffffffffffffffu, 64), std::numeric_limits<s64>::max());
}
