#include <gtest/gtest.h>
#include "util/version_compare.h"

using namespace mesytec::mvme::util;

TEST(VersionCompare, ParseVersion)
{
    auto v = parse_version("1");
    ASSERT_EQ(v, std::vector<unsigned>({ 1, }));

    v = parse_version("1.");
    ASSERT_EQ(v, std::vector<unsigned>({ 1, }));

    v = parse_version("1..");
    ASSERT_EQ(v, std::vector<unsigned>({ 1, }));

    v = parse_version(".");
    ASSERT_EQ(v, std::vector<unsigned>({  }));

    v = parse_version(".1");
    ASSERT_EQ(v, std::vector<unsigned>({  }));

    v = parse_version("1.9.0");
    ASSERT_EQ(v, std::vector<unsigned>({ 1, 9, 0 }));

    v = parse_version("1.9.0.2");
    ASSERT_EQ(v, std::vector<unsigned>({ 1, 9, 0, 2 }));

    v = parse_version("1.9.0-2");
    ASSERT_EQ(v, std::vector<unsigned>({ 1, 9, 0, 2 }));
}

TEST(VersionCompare, CompareVersion)
{
    ASSERT_TRUE(version_less_than("0", "1"));
    ASSERT_FALSE(version_less_than("1", "0"));
    ASSERT_FALSE(version_less_than("1", "1"));

    ASSERT_TRUE(version_less_than("1.8", "1.9.1"));
    ASSERT_TRUE(version_less_than("1.8.1", "1.9"));
    ASSERT_TRUE(version_less_than("1.8.1", "1.9.1"));
    ASSERT_FALSE(version_less_than("1.9.1", "1.8"));
}
