#include <gtest/gtest.h>
#include <QDebug>
#include "multi_event_splitter.h"
#include "typedefs.h"

using namespace mvme::multi_event_splitter;

TEST(MultiEventSplitter, WithSizeSameCount)
{
    // Prepare a splitter for one event with two modules.
    std::vector<std::string> filters =
    {
        "XXXX 0001 XXXX SSSS",
        "XXXX 0010 XXXX SSSS",
    };

    auto splitter = make_splitter({ filters });

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
        int ei, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ASSERT_EQ(ei, 0);
        ASSERT_TRUE(moduleCount == 2);

        for (unsigned mi=0; mi<moduleCount; ++mi)
        {
            const auto &moduleData = moduleDataList[mi];
            std::vector<u32> subEvent(
                moduleData.dynamic.data,
                moduleData.dynamic.data + moduleData.dynamic.size);
            splitEvents[mi].emplace_back(subEvent);
        }
    };

    // Feed data to the splitter. These methods return std::error_codes.
    int eventIndex = 0;
    std::array<ModuleData, 2> moduleDataList = {};
    moduleDataList[0].dynamic = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[1].dynamic = { data[1].data(), static_cast<u32>(data[1].size()) };

    ASSERT_TRUE(!event_data(
            splitter, callbacks,
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

    auto splitter = make_splitter({ filters });

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
        int ei, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ASSERT_EQ(ei, 0);
        ASSERT_TRUE(moduleCount == 2);

        for (unsigned mi=0; mi<moduleCount; ++mi)
        {
            const auto &moduleData = moduleDataList[mi];
            if (moduleData.dynamic.data)
            {
                std::vector<u32> subEvent(
                    moduleData.dynamic.data,
                    moduleData.dynamic.data + moduleData.dynamic.size);
                splitEvents[mi].emplace_back(subEvent);
            }
        }
    };

    // Feed data to the splitter. These methods return std::error_codes.
    int eventIndex = 0;
    std::array<ModuleData, 2> moduleDataList;
    moduleDataList[0].dynamic = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[1].dynamic = { data[1].data(), static_cast<u32>(data[1].size()) };

    ASSERT_TRUE(!event_data(
            splitter, callbacks,
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

    auto splitter = make_splitter({ filters });

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
        int ei, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ASSERT_EQ(ei, 0);
        ASSERT_TRUE(moduleCount == 2);

        for (unsigned mi=0; mi<moduleCount; ++mi)
        {
            const auto &moduleData = moduleDataList[mi];
            if (moduleData.dynamic.data)
            {
                std::vector<u32> subEvent(
                    moduleData.dynamic.data,
                    moduleData.dynamic.data + moduleData.dynamic.size);
                splitEvents[mi].emplace_back(subEvent);
            }
        }
    };

    // Feed data to the splitter. These methods return std::error_codes.
    int eventIndex = 0;
    std::array<ModuleData, 2> moduleDataList = {};
    moduleDataList[0].dynamic = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[1].dynamic = { data[1].data(), static_cast<u32>(data[1].size()) };

    ASSERT_TRUE(!event_data(
            splitter, callbacks,
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

TEST(MultiEventSplitter, NoSizeSameCount)
{
    // Prepare a splitter for one event with two modules.
    std::vector<std::string> filters =
    {
        "XXXX 0001 XXXX XXXX",
        "XXXX 0010 XXXX XXXX",
    };

    auto splitter = make_splitter({ filters });

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
        int ei, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ASSERT_EQ(ei, 0);
        ASSERT_TRUE(moduleCount == 2);

        for (unsigned mi=0; mi<moduleCount; ++mi)
        {
            const auto &moduleData = moduleDataList[mi];
            if (moduleData.dynamic.data)
            {
                std::vector<u32> subEvent(
                    moduleData.dynamic.data,
                    moduleData.dynamic.data + moduleData.dynamic.size);
                splitEvents[mi].emplace_back(subEvent);
            }
        }
    };

    // Feed data to the splitter. These methods return std::error_codes.
    int eventIndex = 0;
    std::array<ModuleData, 2> moduleDataList = {};
    moduleDataList[0].dynamic = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[1].dynamic = { data[1].data(), static_cast<u32>(data[1].size()) };

    ASSERT_TRUE(!event_data(
            splitter, callbacks,
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

TEST(MultiEventSplitter, NoSizeMissingCount)
{
    // Prepare a splitter for one event with two modules.
    std::vector<std::string> filters =
    {
        "XXXX 0001 XXXX XXXX",
        "XXXX 0010 XXXX XXXX",
    };

    auto splitter = make_splitter({ filters });

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
        int ei, const ModuleData *moduleDataList, unsigned moduleCount)
    {
        ASSERT_EQ(ei, 0);
        ASSERT_TRUE(moduleCount == 2);

        for (unsigned mi=0; mi<moduleCount; ++mi)
        {
            const auto &moduleData = moduleDataList[mi];
            if (moduleData.dynamic.data)
            {
                std::vector<u32> subEvent(
                    moduleData.dynamic.data,
                    moduleData.dynamic.data + moduleData.dynamic.size);
                splitEvents[mi].emplace_back(subEvent);
            }
        }
    };

    // Feed data to the splitter. These methods return std::error_codes.
    int eventIndex = 0;
    std::array<ModuleData, 2> moduleDataList = {};
    moduleDataList[0].dynamic = { data[0].data(), static_cast<u32>(data[0].size()) };
    moduleDataList[1].dynamic = { data[1].data(), static_cast<u32>(data[1].size()) };

    ASSERT_TRUE(!event_data(
            splitter, callbacks,
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
