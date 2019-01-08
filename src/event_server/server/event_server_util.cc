#include "event_server/server/event_server_util.h"

#include "analysis/a2_adapter.h"

using namespace mvme::event_server;

static StorageType get_storage_type(unsigned bits)
{
    static const std::array<unsigned, 4> typewidths = {{8, 16, 32, 64}};

    for (size_t i = 0; i < typewidths.size(); i++)
    {
        if (bits <= typewidths[i])
        {
            return static_cast<StorageType>(i);
        }
    }

    return StorageType::st_uint64_t;
}

/* Returns the name of the smallest unsigned type able to store data values
 * produced by the given data source. */
static StorageType get_data_storage_type(const analysis::SourceInterface *dataSource)
{
    unsigned bits = 0;

    if (auto ds = qobject_cast<const analysis::Extractor *>(dataSource))
    {
        bits = ds->getFilter().getDataBits();
    }
    else if (auto ds = qobject_cast<const analysis::ListFilterExtractor *>(dataSource))
    {
        bits = ds->getDataBits();
    }

    return get_storage_type(bits);
}

VMETree make_vme_tree_description(const VMEConfig *vmeConfig)
{
    VMETree result;

    auto eventConfigs = vmeConfig->getEventConfigs();

    for (s32 eventIndex = 0; eventIndex < eventConfigs.size(); eventIndex++)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto moduleConfigs = eventConfig->getModuleConfigs();

        VMEEvent vmeEvent;
        vmeEvent.eventIndex = eventIndex;
        vmeEvent.name = eventConfig->objectName().toStdString();

        for (s32 moduleIndex = 0; moduleIndex < moduleConfigs.size(); moduleIndex++)
        {
            auto moduleConfig = moduleConfigs[moduleIndex];
            VMEModule vmeModule;
            vmeModule.moduleIndex = moduleIndex;
            vmeModule.name = moduleConfig->objectName().toStdString();
            vmeModule.type = moduleConfig->getModuleMeta().typeName.toStdString();

            vmeEvent.modules.emplace_back(vmeModule);
        }

        result.events.emplace_back(vmeEvent);
    }

    return result;
}

EventDataDescriptions make_event_data_descriptions(const analysis::Analysis *analysis)
{
    assert(analysis);
    assert(analysis->getA2AdapterState());
    assert(analysis->getA2AdapterState()->a2);

    // NOTE: The order of data sources in an event is currently determined by
    // the a2 system as it does reorder data sources during the build process.
    // I think this is the only reason the a2 system is used here at all. This
    // is not great.
    EventDataDescriptions result;

    auto a2_adapter = analysis->getA2AdapterState();
    if (!a2_adapter) return result; // QSL("Error: a2_adapter not built");
    auto a2 = a2_adapter->a2;
    if (!a2) return result; // QSL("Error: a2 structure not present");


    for (s32 eventIndex = 0; eventIndex < a2::MaxVMEEvents; eventIndex++)
    {
        EventDataDescription edd;
        edd.eventIndex = eventIndex;

        u32 dataSourceCount = a2->dataSourceCounts[eventIndex];

        if (dataSourceCount == 0) continue;

        for (u32 dsIndex = 0; dsIndex < dataSourceCount; dsIndex++)
        {
            auto a2_dataSource = a2->dataSources[eventIndex] + dsIndex;
            auto a1_dataSource = a2_adapter->sourceMap.value(a2_dataSource);
            s32 moduleIndex = a2_dataSource->moduleIndex;
            u32 outputSize = a2_dataSource->output.size();
            u32 outputBits = std::ceil(std::log2(outputSize));

            DataSourceDescription dsd;
            dsd.name = a1_dataSource->objectName().toStdString();
            dsd.moduleIndex = moduleIndex;
            dsd.size = outputSize;
            dsd.lowerLimit = a2_dataSource->output.lowerLimits[0];
            dsd.upperLimit = a2_dataSource->output.upperLimits[0];
            dsd.indexType  = get_storage_type(outputBits);
            dsd.valueType  = get_data_storage_type(a1_dataSource);

            edd.dataSources.emplace_back(dsd);
        }

        result.emplace_back(edd);
    }

    return result;
}

OutputDataDescription make_output_data_description(const VMEConfig *vmeConfig,
                                                   const analysis::Analysis *analysis)
{
    return
    {
        make_event_data_descriptions(analysis),
        make_vme_tree_description(vmeConfig),
    };
}
