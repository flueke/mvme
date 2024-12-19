#include <gtest/gtest.h>
#include <QDebug>
#include <iostream>
#include "multi_event_splitter.h"
#include "typedefs.h"

using namespace mesytec::mvme::multi_event_splitter;

#if 0
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
    moduleDataList[0].data = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[1].data = { data[1].data(), static_cast<u32>(data[1].size()) };

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
    moduleDataList[0].data = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[1].data = { data[1].data(), static_cast<u32>(data[1].size()) };

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
    moduleDataList[0].data = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[1].data = { data[1].data(), static_cast<u32>(data[1].size()) };

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
    moduleDataList[0].data = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[1].data = { data[1].data(), static_cast<u32>(data[1].size()) };

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
    moduleDataList[0].data = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[1].data = { data[1].data(), static_cast<u32>(data[1].size()) };

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
#endif

// No suffix handling!
void split_dynamic_part(const mesytec::mvlc::util::FilterWithCaches &filter,
                       const ModuleData &input, std::vector<ModuleData> &output)
{
    assert(input.hasDynamic && input.dynamicSize > 0);

    const auto filterSizeCache = mesytec::mvlc::util::get_cache_entry(filter, 'S');

    ModuleData current = input;
    std::basic_string_view<u32> data(dynamic_span(input).data, dynamic_span(input).size);
    assert(!data.empty());

    while (!data.empty())
    {
        if (mesytec::mvlc::util::matches(filter.filter, data.front()))
        {
            if (filterSizeCache)
            {
                u32 size = 1 + mesytec::mvlc::util::extract(*filterSizeCache, data.front());
                current.dynamicSize = std::min(size, static_cast<u32>(data.size()));
                current.hasDynamic = true;
                data.remove_prefix(current.dynamicSize);
            }
            else
            {
                current.dynamicSize = 1;
                data.remove_prefix(1);

                while (!data.empty())
                {
                    if (mesytec::mvlc::util::matches(filter.filter, data.front()))
                        break;

                    ++current.dynamicSize;
                    data.remove_prefix(1);
                }

                current.hasDynamic = true;
            }

            current.data.size = current.prefixSize + current.dynamicSize;

            assert(size_consistency_check(current));
            output.push_back(current);
        }
        else
        {
            // no header match: consume all remaining data
            current.dynamicSize = data.size();
            current.hasDynamic = true;
            data.remove_prefix(data.size());

            assert(size_consistency_check(current));
            output.push_back(current);
        }

        current = {};
        current.data.data = data.data();
        current.data.size = data.size();
    }
}

void split_module_data(const mesytec::mvlc::util::FilterWithCaches &filter,
                       const ModuleData &input,
                       std::vector<ModuleData> &output,
                       std::vector<u32> &buffer)
{
    // handle prefix-only and all empty-dynamic cases (with or without prefix/suffix)
    if (!input.hasDynamic || input.dynamicSize == 0)
    {
        output.emplace_back(input);
        return;
    }

    assert(size_consistency_check(input));
    split_dynamic_part(filter, input, output);
    for (const auto &moduleData: output)
    {
        assert(size_consistency_check(moduleData));
    }
    assert(!output.empty());
    assert(output.front().data.data == input.data.data);


    // TODO: handle input.prefix and input.suffix.
}

using DataBlock = mesytec::mvlc::readout_parser::DataBlock;

namespace mesytec::mvlc::readout_parser
{
bool operator==(const DataBlock &lhs, const std::vector<u32> &rhs)
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
    std::vector<u32> buffer;

    split_module_data(filter, input, output, buffer);

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
    std::vector<u32> buffer;

    split_module_data(filter, input, output, buffer);

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
    std::vector<u32> buffer;

    split_module_data(filter, input, output, buffer);

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
    std::vector<u32> buffer;

    split_module_data(filter, input, output, buffer);

    ASSERT_EQ(output.size(), 1);
    { std::vector<u32> expected = { 0xaaaa, 0xaaaa }; ASSERT_EQ(prefix_span(output[0]), expected); }
    { std::vector<u32> expected = { 0x0201, 0x1111, 0x0101, 0x1112, 0x0101, 0x1113 }; ASSERT_EQ(dynamic_span(output[0]), expected); }
}
