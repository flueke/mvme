#include "multi_crate.h"

#include <cassert>

namespace multi_crate
{

template<typename T>
std::unique_ptr<T> copy_config_object(const T *obj)
{
    assert(obj);

    QJsonObject json;
    obj->write(json);

    auto ret = std::make_unique<T>();
    ret->read(json);
    ret->generateNewId();

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
                        insert_module_mapping(mappings, moduleConf, moduleCopy.get());
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
                    insert_module_mapping(mappings, moduleConf, moduleCopy.get());
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

}
