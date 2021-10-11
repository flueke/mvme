#include <gtest/gtest.h>
#include "event_builder/event_builder.h"

using namespace mvme::event_builder;

TEST(event_builder, TimestampMatch)
{
    WindowMatchResult mr = {};

    mr = timestamp_match(150,  99, { -50, 50 });
    ASSERT_EQ(mr.match, WindowMatch::too_old);
    ASSERT_EQ(mr.invscore, 51u);

    mr = timestamp_match(150, 100, { -50, 50 });
    ASSERT_EQ(mr.match, WindowMatch::in_window);
    ASSERT_EQ(mr.invscore, 50u);

    mr = timestamp_match(150, 200, { -50, 50 });
    ASSERT_EQ(mr.match, WindowMatch::in_window);
    ASSERT_EQ(mr.invscore, 50u);

    mr = timestamp_match(150, 201, { -50, 50 });
    ASSERT_EQ(mr.match, WindowMatch::too_new);
    ASSERT_EQ(mr.invscore, 51u);
}

TEST(event_builder, ConstructDescruct)
{
    std::vector<EventSetup> setup;
    EventBuilder eventbuilder(setup);
}

TEST(event_builder, OneCrateOneEvent)
{
    auto test_timestamp_extractor = [] (const u32 *moduleData, size_t size) -> u32
    {
        if (size > 0)
            return moduleData[0];
        return 0u;
    };

    // Returns a Callback compatible vector of ModuleData structures pointing
    // into the moduleTestData storage.
    auto module_data_list_from_test_data = [] (const auto &moduleTestData)
    {
        std::vector<ModuleData> result(moduleTestData.size());

        for (size_t mi = 0; mi < moduleTestData.size(); ++mi)
        {
            result[mi] = {};
            result[mi].dynamic.data = moduleTestData[mi].data();
            result[mi].dynamic.size = moduleTestData[mi].size();
        }

        return result;
    };

    EventSetup eventSetup;
    eventSetup.enabled = true;
    eventSetup.mainModule = { 0, 1 }; // crate0, module1
    eventSetup.crateSetups.resize(1);
    eventSetup.crateSetups[0].moduleTimestampExtractors = {
        test_timestamp_extractor, // crate0, module0
        test_timestamp_extractor, // crate0, module1 (**)
        test_timestamp_extractor, // crate0, module2
    };
    eventSetup.crateSetups[0].moduleMatchWindows = {
        {  -50,  75 }, // crate0, module0
        {    0,   0 }, // crate0, module1 (**)
        {    0, 150 }, // crate0, module2
    };

    const size_t moduleCount = 3;
    int crateIndex = 0;
    int eventIndex = 0;

    // Storage for the module data
    std::vector<std::array<std::vector<u32>, moduleCount>> moduleTestData =
    {
        {
            {
                { 100 }, // crate0, module0         in window
                { 150 }, // crate0, module1 (**)
                { 200 }, // crate0, module2         in window
            }
        },
        {
            {
                {  25 }, // crate0, module0         too old
                { 151 }, // crate0, module1 (**)
                { 350 }, // crate0, module2         too new
            }
        },
        {
            {
                { 225 }, // crate0, module0         in window but yielded in event#2
                { 252 }, // crate0, module1 (**)
                { 666 }, // crate0, module2         too new but the previous 350 should be yielded
            }
        },
    };



    std::vector<EventSetup> setup = { eventSetup };

    {
        EventBuilder eventBuilder(setup);

        auto moduleDataList = module_data_list_from_test_data(moduleTestData[0]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        size_t dataCallbackCount = 0;
        size_t systemCallbackCount = 0;

        auto eventDataCallback = [&] (void *, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
        {
            ++dataCallbackCount;
            ASSERT_EQ(moduleDataList[0].dynamic.size, 1);
            ASSERT_EQ(moduleDataList[0].dynamic.data[0], 100);

            ASSERT_EQ(moduleDataList[1].dynamic.size, 1);
            ASSERT_EQ(moduleDataList[1].dynamic.data[0], 150);

            ASSERT_EQ(moduleDataList[2].dynamic.size, 1);
            ASSERT_EQ(moduleDataList[2].dynamic.data[0], 200);
        };

        auto systemEventCallback = [&] (void *, const u32 *header, u32 size)
        {
            ++systemCallbackCount;
        };

        eventBuilder.buildEvents({ eventDataCallback, systemEventCallback });

        ASSERT_EQ(dataCallbackCount, 1);
        ASSERT_EQ(systemCallbackCount, 0);
        dataCallbackCount = 0;
        systemCallbackCount = 0;
    }

    {
        EventBuilder eventBuilder(setup);

        auto moduleDataList = module_data_list_from_test_data(moduleTestData[0]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        moduleDataList = module_data_list_from_test_data(moduleTestData[1]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        size_t dataCallbackCount = 0;
        size_t systemCallbackCount = 0;

        auto eventDataCallback = [&] (void *, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
        {
            switch (dataCallbackCount++)
            {
                case 0:
                    {
                        ASSERT_EQ(moduleDataList[0].dynamic.size, 1);
                        ASSERT_EQ(moduleDataList[0].dynamic.data[0], 100);

                        ASSERT_EQ(moduleDataList[1].dynamic.size, 1);
                        ASSERT_EQ(moduleDataList[1].dynamic.data[0], 150);

                        ASSERT_EQ(moduleDataList[2].dynamic.size, 1);
                        ASSERT_EQ(moduleDataList[2].dynamic.data[0], 200);
                    } break;
                case 1:
                    {
                        ASSERT_EQ(moduleDataList[0].dynamic.size, 0);

                        ASSERT_EQ(moduleDataList[1].dynamic.size, 1);
                        ASSERT_EQ(moduleDataList[1].dynamic.data[0], 151);

                        ASSERT_EQ(moduleDataList[2].dynamic.size, 0);
                    } break;
            }
        };

        auto systemEventCallback = [&] (void *, const u32 *header, u32 size)
        {
            ++systemCallbackCount;
        };

        eventBuilder.buildEvents({ eventDataCallback, systemEventCallback });

        ASSERT_EQ(dataCallbackCount, 2);
        ASSERT_EQ(systemCallbackCount, 0);
        dataCallbackCount = 0;
        systemCallbackCount = 0;
    }

    {
        EventBuilder eventBuilder(setup);

        auto moduleDataList = module_data_list_from_test_data(moduleTestData[0]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        moduleDataList = module_data_list_from_test_data(moduleTestData[1]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        moduleDataList = module_data_list_from_test_data(moduleTestData[2]);
        eventBuilder.recordEventData(crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

        size_t dataCallbackCount = 0;
        size_t systemCallbackCount = 0;

        auto eventDataCallback = [&] (void *, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
        {
            switch (dataCallbackCount++)
            {
                case 0:
                    {
                        ASSERT_EQ(moduleDataList[0].dynamic.size, 1);
                        ASSERT_EQ(moduleDataList[0].dynamic.data[0], 100);

                        ASSERT_EQ(moduleDataList[1].dynamic.size, 1);
                        ASSERT_EQ(moduleDataList[1].dynamic.data[0], 150);

                        ASSERT_EQ(moduleDataList[2].dynamic.size, 1);
                        ASSERT_EQ(moduleDataList[2].dynamic.data[0], 200);
                    } break;
                case 1:
                    {
                        ASSERT_EQ(moduleDataList[0].dynamic.size, 1);
                        ASSERT_EQ(moduleDataList[0].dynamic.data[0], 225);

                        ASSERT_EQ(moduleDataList[1].dynamic.size, 1);
                        ASSERT_EQ(moduleDataList[1].dynamic.data[0], 151);

                        ASSERT_EQ(moduleDataList[2].dynamic.size, 0);
                    } break;
                case 2:
                    {
                        ASSERT_EQ(moduleDataList[0].dynamic.size, 0);

                        ASSERT_EQ(moduleDataList[1].dynamic.size, 1);
                        ASSERT_EQ(moduleDataList[1].dynamic.data[0], 252);

                        ASSERT_EQ(moduleDataList[2].dynamic.size, 1);
                        ASSERT_EQ(moduleDataList[2].dynamic.data[0], 350);
                    } break;
            }
        };

        auto systemEventCallback = [&] (void *, const u32 *header, u32 size)
        {
            ++systemCallbackCount;
        };

        eventBuilder.buildEvents({ eventDataCallback, systemEventCallback });

        ASSERT_EQ(dataCallbackCount, 3);
        ASSERT_EQ(systemCallbackCount, 0);
        dataCallbackCount = 0;
        systemCallbackCount = 0;
    }
}
