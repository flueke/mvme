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
#include "mvme_stream_util.h"
#include "vme_config.h"

mvme_stream::StreamInfo streaminfo_from_vmeconfig(VMEConfig *vmeConfig, u32 listfileVersion)
{
    using namespace mvme_stream;

    StreamInfo streamInfo;
    streamInfo.version = listfileVersion;

    auto eventConfigs = vmeConfig->getEventConfigs();

    for (s32 ei = 0; ei < eventConfigs.size(); ei++)
    {
#if 0
        auto event = eventConfigs[ei];

        // FIXME: this stuff moved to the analysis side now (18/06/04)
        streamInfo.multiEventEnabled.set(ei, event->isMultiEventProcessingEnabled());

        auto moduleConfigs = event->getModuleConfigs();

        for (s32 mi = 0; mi < moduleConfigs.size(); mi++)
        {
            auto module = moduleConfigs[mi];

            if (module->getEventHeaderFilter().isEmpty())
            {
                // Override the event-wide setting if any of the modules for
                // this event does not have an event header filter set.
                streamInfo.multiEventEnabled.set(ei, false);
            }
            else
            {
                StreamInfo::FilterWithCache fc;
                fc.filter = a2::data_filter::make_filter(module->getEventHeaderFilter().toStdString());
                fc.cache  = a2::data_filter::make_cache_entry(fc.filter, 'S');

                streamInfo.moduleHeaderFilters[ei][mi] = fc;
            }
        }
#else
        streamInfo.multiEventEnabled.set(ei, false);
#endif
    }

    return streamInfo;
}

mvme_stream::StreamInfo streaminfo_from_listfile(ListFile *listfile)
{
    auto resultPair = read_config_from_listfile(listfile);
    return streaminfo_from_vmeconfig(resultPair.first.get(),
                                     listfile->getFileVersion());
}
