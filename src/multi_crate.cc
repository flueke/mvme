#include "multi_crate.h"

#include <cassert>
#include <stdexcept>
#include <fmt/format.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include "vme_config_scripts.h"

namespace multi_crate
{

MulticrateVMEConfig::MulticrateVMEConfig(QObject *parent)
    : ConfigObject(parent)
    , m_mergedConfig(new VMEConfig(this))
{
}

MulticrateVMEConfig::~MulticrateVMEConfig()
{
}

void MulticrateVMEConfig::addCrateConfig(VMEConfig *cfg)
{
    cfg->setParent(this);
    m_crateConfigs.push_back(cfg);
    emit crateConfigAdded(cfg);
    setModified();
}

void MulticrateVMEConfig::removeCrateConfig(VMEConfig *cfg)
{
    if (containsCrateConfig(cfg))
    {
        emit crateConfigAboutToBeRemoved(cfg);

        m_crateConfigs.erase(
            std::remove_if(std::begin(m_crateConfigs), std::end(m_crateConfigs),
                           [cfg] (const VMEConfig *c) { return cfg == c; }),
            std::end(m_crateConfigs));

        setModified();
    }
}

bool MulticrateVMEConfig::containsCrateConfig(const VMEConfig *cfg) const
{
    return (std::find_if(std::begin(m_crateConfigs), std::end(m_crateConfigs),
                         [cfg] (const VMEConfig *c) { return cfg == c; })
            != std::end(m_crateConfigs));
}

void MulticrateVMEConfig::setIsCrossCrateEvent(int eventIndex, bool isCrossCrate)
{
    if (isCrossCrate)
    {
        m_crossCrateEventIndexes.insert(eventIndex);
    }
    else
    {
        auto pos = m_crossCrateEventIndexes.find(eventIndex);
        if (pos != std::end(m_crossCrateEventIndexes))
            m_crossCrateEventIndexes.erase(pos);
    }
}

bool MulticrateVMEConfig::isCrossCrateEvent(int eventIndex) const
{
    return m_crossCrateEventIndexes.find(eventIndex) != std::end(m_crossCrateEventIndexes);
}

void MulticrateVMEConfig::setCrossCrateEventMainModuleId(int eventIndex, const QUuid &moduleId)
{
    m_crossCrateEventMainModules[eventIndex] = moduleId;
}

QUuid MulticrateVMEConfig::getCrossCrateEventMainModuleId(int eventIndex) const
{
    try
    {
        return m_crossCrateEventMainModules.at(eventIndex);
    }
    catch (const std::out_of_range &)
    {
        return {};
    }
}

std::error_code MulticrateVMEConfig::write_impl(QJsonObject &json) const
{
    // Serialize crate configs and hold them in an array.
    QJsonArray cratesArray;
    for (auto crateConfig: getCrateConfigs())
    {
        QJsonObject dst;
        crateConfig->write(dst);
        cratesArray.append(dst);
    }

    json["crateConfigs"] = cratesArray;

    // cross crate event indexes
    QJsonArray crossEventsArray;
    for (auto it=std::begin(m_crossCrateEventIndexes); it!=std::end(m_crossCrateEventIndexes); ++it)
        crossEventsArray.append(static_cast<qint64>(*it));

    json["crossCrateEvents"] = crossEventsArray;

    // per event main/reference modules
    QJsonArray mainModulesArray;
    for (auto it=std::begin(m_crossCrateEventMainModules); it!=std::end(m_crossCrateEventMainModules); ++it)
    {
        QJsonObject dst;
        dst["eventIndex"] = static_cast<qint64>(it->first);
        dst["moduleId"] = it->second.toString();
        mainModulesArray.append(dst);
    }

    json["mainModules"] = mainModulesArray;

    // write out the merged vme config
    if (m_mergedConfig)
    {
        QJsonObject dst;
        m_mergedConfig->write(dst);
        json["mergedVMEConfig"] = dst;
    }

    return {};
}

std::error_code MulticrateVMEConfig::read_impl(const QJsonObject &json)
{
    auto cratesArray = json["crateConfigs"].toArray();

    for (int ci=0; ci<cratesArray.size(); ++ci)
    {
        auto jobj = cratesArray[ci].toObject();
        auto crateConfig = new VMEConfig(this);
        crateConfig->read(jobj);
        m_crateConfigs.push_back(crateConfig);
    }

    auto crossEventsArray = json["crossCrateEvents"].toArray();

    for (int i=0; i<crossEventsArray.size(); ++i)
        m_crossCrateEventIndexes.insert(crossEventsArray[i].toInt());

    auto mainModulesArray = json["mainModules"].toArray();

    for (int i=0; i<mainModulesArray.size(); ++i)
    {
        auto jobj = mainModulesArray[i].toObject();
        int eventIndex = jobj["eventIndex"].toInt();
        auto moduleId = QUuid::fromString(jobj["moduleId"].toString());
        m_crossCrateEventMainModules[eventIndex] = moduleId;
    }

    m_mergedConfig->read(json["mergedVMEConfig"].toObject());

    return {};
}

namespace
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

std::unique_ptr<ModuleConfig> copy_module_config(const ModuleConfig *src)
{
    auto moduleCopy = copy_config_object(src);

    // Symbol tables from innermost scope to outermost scope. Traverse them in
    // reverse order setting symbols on the moduleCopy object. This way the
    // symbols that had precendence due to scoping will overwrite symbols
    // defined in an outer scope.
    auto symtabs = mesytec::mvme::collect_symbol_tables(src);

    for (auto tabIter=symtabs.rbegin(); tabIter!=symtabs.rend(); ++tabIter)
    {
        auto &symbols = tabIter->symbols;

        for (auto varIter=symbols.begin(); varIter!=symbols.end(); ++varIter)
        {
            moduleCopy->setVariable(varIter.key(), varIter.value());
        }
    }

    return moduleCopy;
}

} // end anon namespace

std::pair<std::unique_ptr<VMEConfig>, MultiCrateObjectMappings> make_merged_vme_config(
    const std::vector<VMEConfig *> &crateConfigs,
    const std::set<int> &crossCrateEvents,
    const MultiCrateObjectMappings &prevMappings
    )
{
    assert(!crateConfigs.empty());

    //const auto &mainCrateConf = crateConfigs[0];
    size_t mergedEventCount = crossCrateEvents.size();
    MultiCrateObjectMappings mappings;
    std::vector<std::unique_ptr<EventConfig>> mergedEvents;

    // Create the cross crate merged events.

    for (auto outEi=0u; outEi<mergedEventCount; ++outEi)
    {
        // TODO: Mapping from crate events to merged events should use the main
        // crates event id as the source of the mapping.

        auto outEv = std::make_unique<EventConfig>();
        outEv->setObjectName(QSL("event%1").arg(outEi));
        outEv->triggerCondition = TriggerCondition::TriggerIO;

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
                        auto moduleCopy = copy_module_config(moduleConf);

                        // Reuse the previously mapped module id.
                        if (prevMappings.cratesToMerged.contains(moduleConf->getId()))
                            moduleCopy->setId(prevMappings.cratesToMerged[moduleConf->getId()]);

                        mappings.insertMapping(moduleConf, moduleCopy.get());
                        outEv->addModuleConfig(moduleCopy.release());
                    }
                }
            }
        }

        mergedEvents.emplace_back(std::move(outEv));
    }

    // Copy over the non-merged events from each of the crate configs.

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
#if 1
                // Create a recursive copy of the event, then update the mappings table by
                // iterating over the modules.
                auto outEv = copy_config_object(eventConf);
                outEv->setObjectName(QSL("crate%1_%2")
                                     .arg(ci)
                                     .arg(eventConf->objectName())
                                     );

                assert(eventConf->getModuleConfigs().size() == outEv->getModuleConfigs().size());

                for (int mi=0; mi<eventConf->moduleCount(); ++mi)
                {
                    auto inMod = eventConf->getModuleConfigs().at(mi);
                    auto outMod = outEv->getModuleConfigs().at(mi);
                    mappings.insertMapping(inMod, outMod);
                }
#else
                // Alternative version: create a new EventConfig, then copy each module
                // individually while updating the mappings.

                //auto outEv = std::make_unique<EventConfig>();
                //outEv->triggerCondition = TriggerCondition::TriggerIO;
                auto moduleConfigs = crateEvents[ei]->getModuleConfigs();

                for (auto moduleConf: moduleConfigs)
                {
                    auto moduleCopy = copy_config_object(moduleConf);
                    mappings.insertMapping(moduleConf, moduleCopy.get());
                    outEv->addModuleConfig(moduleCopy.release());
                }
#endif

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

void multi_crate_playground()
{
    CrateReadout crateReadout;

    CrateReadout crateReadout2(std::move(crateReadout));
}

}
