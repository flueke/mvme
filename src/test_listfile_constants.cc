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
