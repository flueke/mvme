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
        auto event = eventConfigs[ei];

        // FIXME: this stuff moved to the analysis side now (18/06/04)
#if 0
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
    return streaminfo_from_vmeconfig(read_config_from_listfile(listfile).get(),
                                     listfile->getFileVersion());
}
