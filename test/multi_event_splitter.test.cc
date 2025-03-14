#include <gtest/gtest.h>
#include <QDebug>
#include <iostream>
#include "multi_event_splitter.h"
#include "typedefs.h"

using namespace mesytec;
using namespace mesytec::mvme::multi_event_splitter;

using DataBlock = mvlc::readout_parser::DataBlock;
using ModuleData = State::ModuleData;

TEST(MultiEventSplitter, WithSizeSameCount)
{
    // Prepare a splitter for one event with two modules.
    std::vector<std::string> filters =
    {
        "XXXX 0001 XXXX SSSS",
        "XXXX 0010 XXXX SSSS",
    };

    State splitter;
    std::error_code ec;

    std::tie(splitter, ec) = make_splitter({ filters });

    ASSERT_FALSE(ec);

    std::vector<std::vector<u32>> data =
    {
        // Data for module 0
        {
            0x0101,
            0x1111,
            0x0101,
            0x1112,
            0x0101,
            0x1113,
        },
        // Data for module 1
        {
            0x0201,
            0x2221,
            0x0201,
            0x2222,
            0x0201,
            0x2223,
        }
    };

    Callbacks callbacks;
    std::vector<std::vector<std::vector<u32>>> splitEvents(data.size());

    callbacks.eventData = [&splitEvents] (
        void *, int /*crateIndex*/, int ei, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ASSERT_EQ(ei, 0);
        ASSERT_TRUE(moduleCount == 2);

        for (unsigned mi=0; mi<moduleCount; ++mi)
        {
            const auto &moduleData = moduleDataList[mi];
            std::vector<u32> subEvent(
                moduleData.data.data,
                moduleData.data.data + moduleData.data.size);
            splitEvents[mi].emplace_back(subEvent);
        }
    };

    // Feed data to the splitter. These methods return std::error_codes.
    int eventIndex = 0;
    std::array<ModuleData, 2> moduleDataList = {};
    std::fill(std::begin(moduleDataList), std::end(moduleDataList), ModuleData{});
    moduleDataList[0].data = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[0].dynamicSize = data[0].size();
    moduleDataList[0].hasDynamic = true;
    moduleDataList[1].data = { data[1].data(), static_cast<u32>(data[1].size()) };
    moduleDataList[1].dynamicSize = data[1].size();
    moduleDataList[1].hasDynamic = true;

    ASSERT_TRUE(!event_data(
            splitter, callbacks, nullptr,
            eventIndex, moduleDataList.data(), moduleDataList.size()));

    //qDebug() << __PRETTY_FUNCTION__ << splitEvents;

    ASSERT_TRUE(splitEvents.size() == 2);

    ASSERT_TRUE(splitEvents[0].size() == 3);
    { std::vector<u32> expected = { 0x0101, 0x1111 }; ASSERT_EQ(splitEvents[0][0], expected); }
    { std::vector<u32> expected = { 0x0101, 0x1112 }; ASSERT_EQ(splitEvents[0][1], expected); }
    { std::vector<u32> expected = { 0x0101, 0x1113 }; ASSERT_EQ(splitEvents[0][2], expected); }

    ASSERT_TRUE(splitEvents[1].size() == 3);
    { std::vector<u32> expected = { 0x0201, 0x2221 }; ASSERT_EQ(splitEvents[1][0], expected); }
    { std::vector<u32> expected = { 0x0201, 0x2222 }; ASSERT_EQ(splitEvents[1][1], expected); }
    { std::vector<u32> expected = { 0x0201, 0x2223 }; ASSERT_EQ(splitEvents[1][2], expected); }

    ASSERT_EQ(splitter.processingFlags, 0);
}

TEST(MultiEventSplitter, WithSizeMissingCount)
{
    // Prepare a splitter for one event with two modules.
    std::vector<std::string> filters =
    {
        "XXXX 0001 XXXX SSSS",
        "XXXX 0010 XXXX SSSS",
    };

    State splitter;
    std::error_code ec;

    std::tie(splitter, ec) = make_splitter({ filters });

    ASSERT_FALSE(ec);

    std::vector<std::vector<u32>> data =
    {
        // Data for module 0, 3 events
        {
            0x0101,
            0x1111,
            0x0101,
            0x1112,
            0x0101,
            0x1113,
        },
        // Data for module 1, 2 events
        {
            0x0201,
            0x2221,
            0x0201,
            0x2222,
        }
    };

    Callbacks callbacks;
    std::vector<std::vector<std::vector<u32>>> splitEvents(data.size());

    callbacks.eventData = [&splitEvents] (
        void *, int /*crateIndex*/, int ei, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ASSERT_EQ(ei, 0);
        ASSERT_TRUE(moduleCount == 2);

        for (unsigned mi=0; mi<moduleCount; ++mi)
        {
            const auto &moduleData = moduleDataList[mi];
            if (moduleData.data.data)
            {
                std::vector<u32> subEvent(
                    moduleData.data.data,
                    moduleData.data.data + moduleData.data.size);
                splitEvents[mi].emplace_back(subEvent);
            }
        }
    };

    // Feed data to the splitter. These methods return std::error_codes.
    int eventIndex = 0;
    std::array<ModuleData, 2> moduleDataList;
    std::fill(std::begin(moduleDataList), std::end(moduleDataList), ModuleData{});
    moduleDataList[0].data = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[0].dynamicSize = data[0].size();
    moduleDataList[0].hasDynamic = true;
    moduleDataList[1].data = { data[1].data(), static_cast<u32>(data[1].size()) };
    moduleDataList[1].dynamicSize = data[1].size();
    moduleDataList[1].hasDynamic = true;

    ASSERT_TRUE(!event_data(
            splitter, callbacks, nullptr,
            eventIndex, moduleDataList.data(), moduleDataList.size()));

    //qDebug() << __PRETTY_FUNCTION__ << splitEvents;

    ASSERT_TRUE(splitEvents.size() == 2);

    ASSERT_TRUE(splitEvents[0].size() == 3);
    { std::vector<u32> expected = { 0x0101, 0x1111 }; ASSERT_EQ(splitEvents[0][0], expected); }
    { std::vector<u32> expected = { 0x0101, 0x1112 }; ASSERT_EQ(splitEvents[0][1], expected); }
    { std::vector<u32> expected = { 0x0101, 0x1113 }; ASSERT_EQ(splitEvents[0][2], expected); }

    ASSERT_TRUE(splitEvents[1].size() == 2);
    { std::vector<u32> expected = { 0x0201, 0x2221 }; ASSERT_EQ(splitEvents[1][0], expected); }
    { std::vector<u32> expected = { 0x0201, 0x2222 }; ASSERT_EQ(splitEvents[1][1], expected); }

    ASSERT_EQ(splitter.processingFlags, 0);
}

TEST(MultiEventSplitter, WithSizeExceeded)
{
    // Prepare a splitter for one event with two modules.
    std::vector<std::string> filters =
    {
        "XXXX 0001 XXXX SSSS",
        "XXXX 0010 XXXX SSSS",
    };

    State splitter;
    std::error_code ec;

    std::tie(splitter, ec) = make_splitter({ filters });

    ASSERT_FALSE(ec);

    std::vector<std::vector<u32>> data =
    {
        // Data for module 0
        {
            0x0101,
            0x1111,
            0x0101,
            0x1112,
            0x0101,
            0x1113,
        },
        // Data for module 1, the last header size exceeds the buffer size
        {
            0x0201,
            0x2221,
            0x0201,
            0x2222,
            0x0202,
            0x2223,
        }
    };

    Callbacks callbacks;
    std::vector<std::vector<std::vector<u32>>> splitEvents(data.size());

    callbacks.eventData = [&splitEvents] (
        void *, int /*crateIndex*/, int ei, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ASSERT_EQ(ei, 0);
        ASSERT_TRUE(moduleCount == 2);

        for (unsigned mi=0; mi<moduleCount; ++mi)
        {
            const auto &moduleData = moduleDataList[mi];
            if (moduleData.data.data)
            {
                std::vector<u32> subEvent(
                    moduleData.data.data,
                    moduleData.data.data + moduleData.data.size);
                splitEvents[mi].emplace_back(subEvent);
            }
        }
    };

    // Feed data to the splitter. These methods return std::error_codes.
    int eventIndex = 0;
    std::array<ModuleData, 2> moduleDataList = {};
    std::fill(std::begin(moduleDataList), std::end(moduleDataList), ModuleData{});
    moduleDataList[0].data = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[0].dynamicSize = data[0].size();
    moduleDataList[0].hasDynamic = true;
    moduleDataList[1].data = { data[1].data(), static_cast<u32>(data[1].size()) };
    moduleDataList[1].dynamicSize = data[1].size();
    moduleDataList[1].hasDynamic = true;

    ASSERT_TRUE(!event_data(
            splitter, callbacks, nullptr,
            eventIndex, moduleDataList.data(), moduleDataList.size()));

    //qDebug() << __PRETTY_FUNCTION__ << splitEvents;

    ASSERT_TRUE(splitEvents.size() == 2);

    ASSERT_EQ(splitEvents[0].size(), 3);
    { std::vector<u32> expected = { 0x0101, 0x1111 }; ASSERT_EQ(splitEvents[0][0], expected); }
    { std::vector<u32> expected = { 0x0101, 0x1112 }; ASSERT_EQ(splitEvents[0][1], expected); }
    { std::vector<u32> expected = { 0x0101, 0x1113 }; ASSERT_EQ(splitEvents[0][2], expected); }

    ASSERT_EQ(splitEvents[1].size(), 3);
    { std::vector<u32> expected = { 0x0201, 0x2221 }; ASSERT_EQ(splitEvents[1][0], expected); }
    { std::vector<u32> expected = { 0x0201, 0x2222 }; ASSERT_EQ(splitEvents[1][1], expected); }
    { std::vector<u32> expected = { 0x0202, 0x2223 }; ASSERT_EQ(splitEvents[1][2], expected); }

    ASSERT_NE(splitter.processingFlags, 0);
}

TEST(MultiEventSplitter, NoSizeSameCount)
{
    // Prepare a splitter for one event with two modules.
    std::vector<std::string> filters =
    {
        "XXXX 0001 XXXX XXXX",
        "XXXX 0010 XXXX XXXX",
    };

    State splitter;
    std::error_code ec;

    std::tie(splitter, ec) = make_splitter({ filters });

    ASSERT_FALSE(ec);

    std::vector<std::vector<u32>> data =
    {
        // Data for module 0
        {
            0x0100,
            0x1011,
            0x1012,

            0x0100,
            0x1021,
            0x1022,

            0x0100,
            0x1031,
            0x1032,
        },
        // Data for module 1
        {
            0x0200,
            0x2011,
            0x2012,

            0x0200,
            0x2021,
            0x2022,

            0x0200,
            0x2031,
            0x2032,
        }
    };

    Callbacks callbacks;
    std::vector<std::vector<std::vector<u32>>> splitEvents(data.size());

    callbacks.eventData = [&splitEvents] (
        void *, int /*crateIndex*/, int ei, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ASSERT_EQ(ei, 0);
        ASSERT_TRUE(moduleCount == 2);

        for (unsigned mi=0; mi<moduleCount; ++mi)
        {
            const auto &moduleData = moduleDataList[mi];
            if (moduleData.data.data)
            {
                std::vector<u32> subEvent(
                    moduleData.data.data,
                    moduleData.data.data + moduleData.data.size);
                splitEvents[mi].emplace_back(subEvent);
            }
        }
    };

    // Feed data to the splitter. These methods return std::error_codes.
    int eventIndex = 0;
    std::array<ModuleData, 2> moduleDataList = {};
    std::fill(std::begin(moduleDataList), std::end(moduleDataList), ModuleData{});
    moduleDataList[0].data = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[0].dynamicSize = data[0].size();
    moduleDataList[0].hasDynamic = true;
    moduleDataList[1].data = { data[1].data(), static_cast<u32>(data[1].size()) };
    moduleDataList[1].dynamicSize = data[1].size();
    moduleDataList[1].hasDynamic = true;

    ASSERT_TRUE(!event_data(
            splitter, callbacks, nullptr,
            eventIndex, moduleDataList.data(), moduleDataList.size()));

    //qDebug() << __PRETTY_FUNCTION__ << splitEvents;

    ASSERT_TRUE(splitEvents.size() == 2);

    ASSERT_TRUE(splitEvents[0].size() == 3);
    { std::vector<u32> expected = { 0x0100, 0x1011, 0x1012 }; ASSERT_EQ(splitEvents[0][0], expected); }
    { std::vector<u32> expected = { 0x0100, 0x1021, 0x1022 }; ASSERT_EQ(splitEvents[0][1], expected); }
    { std::vector<u32> expected = { 0x0100, 0x1031, 0x1032 }; ASSERT_EQ(splitEvents[0][2], expected); }

    ASSERT_TRUE(splitEvents[1].size() == 3);
    { std::vector<u32> expected = { 0x0200, 0x2011, 0x2012 }; ASSERT_EQ(splitEvents[1][0], expected); }
    { std::vector<u32> expected = { 0x0200, 0x2021, 0x2022 }; ASSERT_EQ(splitEvents[1][1], expected); }
    { std::vector<u32> expected = { 0x0200, 0x2031, 0x2032 }; ASSERT_EQ(splitEvents[1][2], expected); }

    ASSERT_EQ(splitter.processingFlags, 0);
}

TEST(MultiEventSplitter, NoSizeMissingCount)
{
    // Prepare a splitter for one event with two modules.
    std::vector<std::string> filters =
    {
        "XXXX 0001 XXXX XXXX",
        "XXXX 0010 XXXX XXXX",
    };

    State splitter;
    std::error_code ec;

    std::tie(splitter, ec) = make_splitter({ filters });

    ASSERT_FALSE(ec);

    std::vector<std::vector<u32>> data =
    {
        // Data for module 0, 3 events
        {
            0x0100,
            0x1011,
            0x1012,

            0x0100,
            0x1021,
            0x1022,

            0x0100,
            0x1031,
            0x1032,
        },
        // Data for module 1, 2 events
        {
            0x0200,
            0x2011,
            0x2012,

            0x0200,
            0x2021,
            0x2022,
        }
    };

    Callbacks callbacks;
    std::vector<std::vector<std::vector<u32>>> splitEvents(data.size());

    callbacks.eventData = [&splitEvents] (
        void *, int /*crateIndex*/, int ei, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ASSERT_EQ(ei, 0);
        ASSERT_TRUE(moduleCount == 2);

        for (unsigned mi=0; mi<moduleCount; ++mi)
        {
            const auto &moduleData = moduleDataList[mi];
            if (moduleData.data.data)
            {
                std::vector<u32> subEvent(
                    moduleData.data.data,
                    moduleData.data.data + moduleData.data.size);
                splitEvents[mi].emplace_back(subEvent);
            }
        }
    };

    // Feed data to the splitter. These methods return std::error_codes.
    int eventIndex = 0;
    std::array<ModuleData, 2> moduleDataList = {};
    std::fill(std::begin(moduleDataList), std::end(moduleDataList), ModuleData{});
    moduleDataList[0].data = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[0].dynamicSize = data[0].size();
    moduleDataList[0].hasDynamic = true;
    moduleDataList[1].data = { data[1].data(), static_cast<u32>(data[1].size()) };
    moduleDataList[1].dynamicSize = data[1].size();
    moduleDataList[1].hasDynamic = true;

    ASSERT_TRUE(!event_data(
            splitter, callbacks, nullptr,
            eventIndex, moduleDataList.data(), moduleDataList.size()));

    //qDebug() << __PRETTY_FUNCTION__ << splitEvents;

    ASSERT_EQ(splitEvents.size(), 2);

    ASSERT_EQ(splitEvents[0].size(), 3);
    { std::vector<u32> expected = { 0x0100, 0x1011, 0x1012 }; ASSERT_EQ(splitEvents[0][0], expected); }
    { std::vector<u32> expected = { 0x0100, 0x1021, 0x1022 }; ASSERT_EQ(splitEvents[0][1], expected); }
    { std::vector<u32> expected = { 0x0100, 0x1031, 0x1032 }; ASSERT_EQ(splitEvents[0][2], expected); }

    ASSERT_EQ(splitEvents[1].size(), 2);
    { std::vector<u32> expected = { 0x0200, 0x2011, 0x2012 }; ASSERT_EQ(splitEvents[1][0], expected); }
    { std::vector<u32> expected = { 0x0200, 0x2021, 0x2022 }; ASSERT_EQ(splitEvents[1][1], expected); }

    format_counters(std::cout, splitter.counters);
    std::cout << std::endl;
    format_counters_tabular(std::cout, splitter.counters);
}

namespace mesytec::mvlc::readout_parser
{
inline bool operator==(const DataBlock &lhs, const std::vector<u32> &rhs)
{
    if (lhs.size != rhs.size())
        return false;

    for (size_t i=0; i<lhs.size; ++i)
    {
        if (lhs.data[i] != rhs[i])
            return false;
    }

    return true;
}
}

TEST(MultiEventSplitter, SplitSizeMatch)
{
    const std::string headerFilter("0000 0001 XXXX SSSS");
    const auto filter = mesytec::mvlc::util::make_filter_with_caches(headerFilter);

    const std::vector<u32> data =
    {
        0xaaaa, // prefix
        0xaaaa, // prefix
        0x0101, // header
        0x1111,
        0x0101, // header
        0x1112,
        0x0101, // header
        0x1113,
    };

    ModuleData input = {};
    input.data = { data.data(), static_cast<u32>(data.size()) };
    input.prefixSize = 2;
    input.dynamicSize = data.size() - input.prefixSize;
    input.hasDynamic = true;

    std::vector<ModuleData> output;
    ASSERT_EQ(split_module_data(filter, input, output), State::ProcessingFlags::Ok);

    ASSERT_EQ(output.size(), 3);
    { std::vector<u32> expected = { 0xaaaa, 0xaaaa }; ASSERT_EQ(prefix_span(output[0]), expected); }
    { std::vector<u32> expected = { 0x0101, 0x1111 }; ASSERT_EQ(dynamic_span(output[0]), expected); }
    { std::vector<u32> expected = { 0x0101, 0x1112 }; ASSERT_EQ(dynamic_span(output[1]), expected); }
    { std::vector<u32> expected = { 0x0101, 0x1113 }; ASSERT_EQ(dynamic_span(output[2]), expected); }

}

TEST(MultiEventSplitter, SplitSizeMatchOverflow)
{
    const std::string headerFilter("0000 0001 XXXX SSSS");
    const auto filter = mesytec::mvlc::util::make_filter_with_caches(headerFilter);

    const std::vector<u32> data =
    {
        0xaaaa, // prefix
        0xaaaa, // prefix
        0x0101, // header
        0x1111,
        0x0104, // header overflow
        0x1112,
        0x0101, // header
        0x1113,
    };

    ModuleData input = {};
    input.data = { data.data(), static_cast<u32>(data.size()) };
    input.prefixSize = 2;
    input.dynamicSize = data.size() - input.prefixSize;
    input.hasDynamic = true;

    std::vector<ModuleData> output;
    ASSERT_EQ(split_module_data(filter, input, output), State::ProcessingFlags::ModuleSizeExceedsBuffer);

    ASSERT_EQ(output.size(), 2);
    { std::vector<u32> expected = { 0xaaaa, 0xaaaa }; ASSERT_EQ(prefix_span(output[0]), expected); }
    { std::vector<u32> expected = { 0x0101, 0x1111 }; ASSERT_EQ(dynamic_span(output[0]), expected); }
    { std::vector<u32> expected = { 0x0104, 0x1112, 0x0101, 0x1113 }; ASSERT_EQ(dynamic_span(output[1]), expected); }
}

TEST(MultiEventSplitter, SplitNoSize)
{
    const std::string headerFilter("0000 0001 XXXX XXXX");
    const auto filter = mesytec::mvlc::util::make_filter_with_caches(headerFilter);

    const std::vector<u32> data =
    {
        0xaaaa, // prefix
        0xaaaa, // prefix
        0x0101, // header
        0x1111,
        0x0101, // header
        0x1112,
        0x0101, // header
        0x1113,
    };

    ModuleData input = {};
    input.data = { data.data(), static_cast<u32>(data.size()) };
    input.prefixSize = 2;
    input.dynamicSize = data.size() - input.prefixSize;
    input.hasDynamic = true;

    std::vector<ModuleData> output;
    ASSERT_EQ(split_module_data(filter, input, output), State::ProcessingFlags::Ok);

    ASSERT_EQ(output.size(), 3);
    { std::vector<u32> expected = { 0xaaaa, 0xaaaa }; ASSERT_EQ(prefix_span(output[0]), expected); }
    { std::vector<u32> expected = { 0x0101, 0x1111 }; ASSERT_EQ(dynamic_span(output[0]), expected); }
    { std::vector<u32> expected = { 0x0101, 0x1112 }; ASSERT_EQ(dynamic_span(output[1]), expected); }
    { std::vector<u32> expected = { 0x0101, 0x1113 }; ASSERT_EQ(dynamic_span(output[2]), expected); }

}

TEST(MultiEventSplitter, SplitNoMatch)
{
    const std::string headerFilter("0000 0001 XXXX SSSS");
    const auto filter = mesytec::mvlc::util::make_filter_with_caches(headerFilter);

    const std::vector<u32> data =
    {
        0xaaaa, // prefix
        0xaaaa, // prefix
        0x0201, // header does not match
        0x1111,
        0x0101, // header
        0x1112,
        0x0101, // header
        0x1113,
    };

    ModuleData input = {};
    input.data = { data.data(), static_cast<u32>(data.size()) };
    input.prefixSize = 2;
    input.dynamicSize = data.size() - input.prefixSize;
    input.hasDynamic = true;

    std::vector<ModuleData> output;
    ASSERT_EQ(split_module_data(filter, input, output), State::ProcessingFlags::ModuleHeaderMismatch);

    ASSERT_EQ(output.size(), 1);
    { std::vector<u32> expected = { 0xaaaa, 0xaaaa }; ASSERT_EQ(prefix_span(output[0]), expected); }
    { std::vector<u32> expected = { 0x0201, 0x1111, 0x0101, 0x1112, 0x0101, 0x1113 }; ASSERT_EQ(dynamic_span(output[0]), expected); }
}
