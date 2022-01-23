#ifndef __MVME_MULTI_CRATE_H__
#define __MVME_MULTI_CRATE_H__

#include <memory>
#include <set>

#include "vme_config.h"

namespace multi_crate
{

// input:
// * list of crate vme configs with the first being the main crate
// * list of event indexes which are part of a cross-crate event
//
// output:
// a new merged vme config containing both merged cross-crate events and
// non-merged single-crate events. The latter events are in linear (crate,
// event) order.
//
// TODO: also output mapping information to be able to map objects between the
// input configs and the merged output config.

struct MultiCrateModuleMappings
{
    QMap<QUuid, QUuid> cratesToMerged;
    QMap<QUuid, QUuid> mergedToCrates;
};

inline void insert_module_mapping(
    MultiCrateModuleMappings &mappings,
    ModuleConfig *crateModule,
    ModuleConfig *mergedModule)
{
    mappings.cratesToMerged.insert(crateModule->getId(), mergedModule->getId());
    mappings.mergedToCrates.insert(mergedModule->getId(), crateModule->getId());
}

std::pair<std::unique_ptr<VMEConfig>, MultiCrateModuleMappings> make_merged_vme_config(
    const std::vector<VMEConfig *> &crateConfigs,
    const std::set<int> &crossCrateEvents
    );

}

#endif /* __MVME_MULTI_CRATE_H__ */
