#include "gtest/gtest.h"
#include "mvme_listfile.h"
#include <iostream>

TEST(MVMEListfile, EventIndex)
{
    for (u32 eventIndex = 0; eventIndex < 16; eventIndex++)
    {
        for (u32 crateIndex = 0; crateIndex < 8; crateIndex++)
        {
            for (u32 version = 0; version <= CurrentListfileVersion; version++)
            {
                auto lfc = listfile_constants(version);

                u32 eventHeader = lfc.makeEventSectionHeader(eventIndex, crateIndex);

                ASSERT_EQ(lfc.getEventIndex(eventHeader), eventIndex);

                if (lfc.hasCrateIndex())
                {
                    ASSERT_EQ(lfc.getCrateIndex(eventHeader), crateIndex);
                }
            }
        }
    }
}

#if 0

using std::cout;
using std::endl;

TEST(mvme_listfile, Manual)
{
    auto lfc = listfile_constants();
    u32 eventHeader = 0x20400025u;

    cout << lfc.getEventIndex(eventHeader) << endl;

    eventHeader = 0x20000025u;

    cout << lfc.getEventIndex(eventHeader) << endl;
}

#endif
