#ifndef __MVME_MULTI_CRATE_H__
#define __MVME_MULTI_CRATE_H__

#include <memory>
#include <set>

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "vme_config.h"

namespace multi_crate
{

//
// VME config merging
//

struct MultiCrateModuleMappings
{
    QMap<QUuid, QUuid> cratesToMerged;
    QMap<QUuid, QUuid> mergedToCrates;

    void insertMapping(const ModuleConfig *crateModule, const ModuleConfig *mergedModule)
    {
        insertMapping(crateModule->getId(), mergedModule->getId());
    }

    void insertMapping(const QUuid &crateModuleId, const QUuid &mergedModuleId)
    {
        cratesToMerged.insert(crateModuleId, mergedModuleId);
        mergedToCrates.insert(mergedModuleId, crateModuleId);
    }
};

// inputs:
// * list of crate vme configs with the first being the main crate
// * list of event indexes which are part of a cross-crate event
//
// outputs:
// * a new merged vme config containing both merged cross-crate events and
//   non-merged single-crate events. The latter events are in linear (crate,
//   event) order.
// * bi-directional module mappings
std::pair<std::unique_ptr<VMEConfig>, MultiCrateModuleMappings> make_merged_vme_config(
    const std::vector<VMEConfig *> &crateConfigs,
    const std::set<int> &crossCrateEvents
    );

//
// MultiCrateConfig
//

struct MultiCrateConfig
{
    // Filename of the VMEConfig for the main crate.
    QString mainConfig;

    // Filenames of VMEconfigs of the secondary crates.
    QStringList secondaryConfigs;

    // Event ids from mainConfig which form cross crate events.
    std::set<QUuid> crossCrateEventIds;

    // Ids of the "main" module for each cross crate event. The moduleId may
    // come from any of the individual VMEConfigs. The representation of this
    // module in the merged VMEConfig will be used as the EventBuilders main
    // module.
    std::set<QUuid> mainModuleIds;
};

inline bool operator==(const MultiCrateConfig &a, const MultiCrateConfig &b)
{
    return (a.mainConfig == b.mainConfig
            && a.secondaryConfigs == b.secondaryConfigs
            && a.crossCrateEventIds == b.crossCrateEventIds
           );
}

inline bool operator!=(const MultiCrateConfig &a, const MultiCrateConfig &b)
{
    return !(a == b);
}

MultiCrateConfig load_multi_crate_config(const QJsonDocument &doc);
MultiCrateConfig load_multi_crate_config(const QJsonObject &json);
MultiCrateConfig load_multi_crate_config(const QString &filename);

QJsonObject to_json_object(const MultiCrateConfig &mcfg);
QJsonDocument to_json_document(const MultiCrateConfig &mcfg);

}

#endif /* __MVME_MULTI_CRATE_H__ */
