#include "event_server/event_server_util.h"

#include <QJsonArray>

#include "analysis/a2_adapter.h"

QJsonValue make_vme_tree_description(const VMEConfig *vmeConfig)
{
    QJsonArray vmeTree;

    auto eventConfigs = vmeConfig->getEventConfigs();

    for (s32 eventIndex = 0; eventIndex < eventConfigs.size(); eventIndex++)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto moduleConfigs = eventConfig->getModuleConfigs();

        QJsonArray moduleInfos;

        for (s32 moduleIndex = 0; moduleIndex < moduleConfigs.size(); moduleIndex++)
        {
            auto moduleConfig = moduleConfigs[moduleIndex];
            QJsonObject moduleInfo;
            moduleInfo["name"] = moduleConfig->objectName();
            moduleInfo["type"] = moduleConfig->getModuleMeta().typeName;
            moduleInfo["moduleIndex"] = moduleIndex;
            moduleInfos.append(moduleInfo);
        }

        QJsonObject eventInfo;
        eventInfo["eventIndex"] = eventIndex;
        eventInfo["modules"] = moduleInfos;
        eventInfo["name"] = eventConfig->objectName();
        vmeTree.append(eventInfo);
    }

    return vmeTree;
}

static QString get_value_type(const analysis::SourceInterface *dataSource)
{
    u32 bits = 0;

    if (auto ds = qobject_cast<const analysis::Extractor *>(dataSource))
    {
        bits = ds->getFilter().getDataBits();
    }
    else if (auto ds = qobject_cast<const analysis::ListFilterExtractor *>(dataSource))
    {
        bits = ds->getDataBits();
    }

    static const std::array<u32, 3> typewidths = {16, 32, 64};

    for (u32 ti = 0; ti < typewidths.size(); ti++)
    {
        if (bits <= typewidths[ti])
        {
            return QString("uint%1_t").arg(typewidths[ti]);
        }
    }

    return QString();
}

QJsonValue make_datasource_description(const analysis::Analysis *analysis)
{
    // NOTE: The order of data sources in an event is currently determined by
    // the a2 system as it does reorder data sources during the build process.
    // I think this is the only reason the a2 system is used at all here. This
    // is not great.

    QJsonArray eventDataSources;

    auto a2_adapter = analysis->getA2AdapterState();
    if (!a2_adapter) return QSL("Error: a2_adapter not built");
    auto a2 = a2_adapter->a2;
    if (!a2) return QSL("Error: a2 structure not present");

    for (s32 eventIndex = 0; eventIndex < a2::MaxVMEEvents; eventIndex++)
    {
        const u32 dataSourceCount = a2->dataSourceCounts[eventIndex];
        u32 eventBytes = 0;

        if (!dataSourceCount) continue;

        QJsonArray dataSourceInfos;

        for (u32 dsIndex = 0; dsIndex < dataSourceCount; dsIndex++)
        {
            auto a2_dataSource = a2->dataSources[eventIndex] + dsIndex;
            auto a1_dataSource = a2_adapter->sourceMap.value(a2_dataSource);
            s32 moduleIndex = a2_dataSource->moduleIndex;

            auto valueType = get_value_type(a1_dataSource);


            qDebug() << "DataServer" << "structure: eventIndex=" << eventIndex << "dsIndex=" << dsIndex
                << "a2_ds=" << a2_dataSource << ", a1_dataSource=" << a1_dataSource
                << "a2_ds_moduleIndex=" << moduleIndex;

            qint64 outputSize = a2_dataSource->output.size();

            QJsonObject dsInfo;
            dsInfo["name"] = a1_dataSource->objectName();
            dsInfo["moduleIndex"] = moduleIndex;
            dsInfo["format"] = "SparseArray";
            dsInfo["indextype"] = "uint16_t";
            dsInfo["valuetype"] = valueType;
            dsInfo["size"] = outputSize; // number of elements in the data array
            dsInfo["lowerLimit"] = a2_dataSource->output.lowerLimits[0];
            dsInfo["upperLimit"] = a2_dataSource->output.upperLimits[0];

            dataSourceInfos.append(dsInfo);

            qint64 output_bytes = outputSize * a2_dataSource->output.data.element_size;
            eventBytes += output_bytes;
        }

        QJsonObject eventInfo;
        eventInfo["eventIndex"] = eventIndex;
        eventInfo["dataSources"] = dataSourceInfos;
        eventDataSources.append(eventInfo);

        qDebug() << "DataServer"
            << "eventIndex=" << eventIndex
            << "outputBytes" << eventBytes;
    }

    return eventDataSources;
}

// How the data stream looks:
// eventIndex (known in endEvent)
// first data source output
// second data source output
// ...
//
// What the receiver has to know
// The data sources for each event index.
// The modules for each event index.
// The relationship of a module and its datasources

QJsonObject make_output_data_description(const VMEConfig *vmeConfig,
                                         const analysis::Analysis *analysis)
{
    QJsonObject result;
    result["vmeTree"] = make_vme_tree_description(vmeConfig);
    result["eventDataSources"] = make_datasource_description(analysis);
    return result;
}
