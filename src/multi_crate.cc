#include "multi_crate.h"

#include <cassert>
#include <stdexcept>
#include <fmt/format.h>
#include <mesytec-mvlc/mesytec-mvlc.h>

namespace multi_crate
{

// Generates new QUuids for a hierarchy of ConfigObjects
void generate_new_ids(ConfigObject *parentObject)
{
    parentObject->generateNewId();

    for (auto child: parentObject->children())
        if (auto childObject = qobject_cast<ConfigObject *>(child))
            generate_new_ids(childObject);
}

// Copies a ConfigObject by first serializing to json, then creating the copy
// via deserialization. The copied object and its children are assigned new ids.
template<typename T>
std::unique_ptr<T> copy_config_object(const T *obj)
{
    assert(obj);

    QJsonObject json;
    obj->write(json);

    auto ret = std::make_unique<T>();
    ret->read(json);

    generate_new_ids(ret.get());

    return ret;
}

std::pair<std::unique_ptr<VMEConfig>, MultiCrateModuleMappings> make_merged_vme_config(
    const std::vector<VMEConfig *> &crateConfigs,
    const std::set<int> &crossCrateEvents
    )
{
    MultiCrateModuleMappings mappings;
    size_t mergedEventCount = crossCrateEvents.size();

    std::vector<std::unique_ptr<EventConfig>> mergedEvents;

    for (auto outEi=0u; outEi<mergedEventCount; ++outEi)
    {
        auto outEv = std::make_unique<EventConfig>();
        outEv->setObjectName(QSL("event%1").arg(outEi));

        for (auto crateConf: crateConfigs)
        {
            auto crateEvents = crateConf->getEventConfigs();

            for (int ei=0; ei<crateEvents.size(); ++ei)
            {
                if (crossCrateEvents.count(ei))
                {
                    auto moduleConfigs = crateEvents[ei]->getModuleConfigs();

                    for (auto moduleConf: moduleConfigs)
                    {
                        auto moduleCopy = copy_config_object(moduleConf);
                        mappings.insertMapping(moduleConf, moduleCopy.get());
                        outEv->addModuleConfig(moduleCopy.release());
                    }
                }
            }
        }

        mergedEvents.emplace_back(std::move(outEv));
    }

    std::vector<std::unique_ptr<EventConfig>> singleCrateEvents;

    for (size_t ci=0; ci<crateConfigs.size(); ++ci)
    {
        auto crateConf = crateConfigs[ci];
        auto crateEvents = crateConf->getEventConfigs();

        for (int ei=0; ei<crateEvents.size(); ++ei)
        {
            auto eventConf = crateEvents[ei];

            if (!crossCrateEvents.count(ei))
            {
                auto outEv = std::make_unique<EventConfig>();
                outEv->setObjectName(QSL("crate%1_%2")
                                     .arg(ci)
                                     .arg(eventConf->objectName())
                                     );

                auto moduleConfigs = crateEvents[ei]->getModuleConfigs();

                for (auto moduleConf: moduleConfigs)
                {
                    auto moduleCopy = copy_config_object(moduleConf);
                    mappings.insertMapping(moduleConf, moduleCopy.get());
                    outEv->addModuleConfig(moduleCopy.release());
                }

                singleCrateEvents.emplace_back(std::move(outEv));
            }
        }
    }

    auto merged = std::make_unique<VMEConfig>();

    for (auto &eventConf: mergedEvents)
        merged->addEventConfig(eventConf.release());

    for (auto &eventConf: singleCrateEvents)
        merged->addEventConfig(eventConf.release());

    return std::make_pair(std::move(merged), mappings);
}

QJsonObject to_json_object(const MultiCrateConfig &mcfg)
{
    QJsonObject j;
    j["mainConfig"] = mcfg.mainConfig;
    j["secondaryConfigs"] = QJsonArray::fromStringList(mcfg.secondaryConfigs);

    QJsonArray ids;

    std::transform(
        std::begin(mcfg.crossCrateEventIds), std::end(mcfg.crossCrateEventIds),
        std::back_inserter(ids), [] (const QUuid &id)
        {
            return id.toString();
        });

    j["crossCrateEventIds"] = ids;

    return j;
}

QJsonDocument to_json_document(const MultiCrateConfig &mcfg)
{
    QJsonObject outer;
    outer["MvmeMultiCrateConfig"] = to_json_object(mcfg);
    return QJsonDocument(outer);
}

MultiCrateConfig load_multi_crate_config(const QJsonObject &json)
{
    MultiCrateConfig mcfg = {};
    mcfg.mainConfig = json["mainConfig"].toString();

    for (const auto &jval: json["secondaryConfigs"].toArray())
        mcfg.secondaryConfigs.push_back(jval.toString());

    for (const auto &jval: json["crossCrateEventIds"].toArray())
        mcfg.crossCrateEventIds.insert(QUuid::fromString(jval.toString()));

    return mcfg;
}

MultiCrateConfig load_multi_crate_config(const QJsonDocument &doc)
{
    return load_multi_crate_config(doc.object()["MvmeMultiCrateConfig"].toObject());
}

MultiCrateConfig load_multi_crate_config(const QString &filename)
{
    QFile inFile(filename);

    if (!inFile.open(QIODevice::ReadOnly))
        throw std::runtime_error(inFile.errorString().toStdString());

    auto data = inFile.readAll();

    QJsonParseError parseError;
    QJsonDocument doc(QJsonDocument::fromJson(data, &parseError));

    if (parseError.error != QJsonParseError::NoError)
    {
        throw std::runtime_error(
            fmt::format("JSON parse error in file {}: {} at offset {}",
                        filename.toStdString(),
                        parseError.errorString().toStdString(),
                        std::to_string(parseError.offset)));
    }

    if (doc.isNull())
        return {};

    return load_multi_crate_config(doc);
}


// run_readout_parser()
// run_event_builder()

void multi_crate_playground()
{
    CrateReadout crateReadout;

    CrateReadout crateReadout2(std::move(crateReadout));
}

}