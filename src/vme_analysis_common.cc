/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "vme_analysis_common.h"

#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

#include "analysis/analysis.h"
#include "analysis/analysis_util.h"
#include "qt_util.h"

using namespace analysis;

namespace vme_analysis_common
{

void update_analysis_vme_properties(const VMEConfig *vmeConfig, analysis::Analysis *analysis)
{
    const auto allModules = vmeConfig->getAllModuleConfigs();
    QSet<QUuid> moduleIdsInVMEConfig;

    for (auto module: allModules)
        moduleIdsInVMEConfig.insert(module->getId());

    QVariantList modulePropertyList;

    // Keep properties for modules that do not exist in the vme config.
    for (const auto &var: analysis->getModulePropertyList())
    {
        auto props = var.toMap();
        auto moduleId = props["moduleId"].toUuid();

        if (!moduleIdsInVMEConfig.contains(moduleId))
            modulePropertyList.push_back(props);
    }

    // Add properties for modules in the vme config
    for (auto module: allModules)
    {
        QVariantMap moduleProperties;
        moduleProperties["moduleId"] = module->getId().toString();
        moduleProperties["moduleTypeName"] = module->getModuleMeta().typeName;
        moduleProperties["moduleName"] = module->objectName();
        modulePropertyList.push_back(moduleProperties);
    }

    analysis->setModulePropertyList(modulePropertyList);
}

void remove_analysis_module_properties(const QUuid &moduleId, analysis::Analysis *analysis)
{
    auto propList = analysis->property("ModuleProperties").toList();

    auto pos = std::find_if(std::begin(propList), std::end(propList), [&moduleId] (const QVariant &vp) {
        return vp.toMap().value("moduleId").toUuid() == moduleId;
    });

    if (pos != std::end(propList))
        propList.erase(pos);

    analysis->setProperty("ModuleProperties", propList);
}

QDebug &operator<<(QDebug &out, const ModuleInfo &mi)
{
    QDebugStateSaver qdss(out);
    out.nospace().noquote();
    out << "ModuleInfo(id=" << mi.id << ", eventId=" << mi.eventId
        << ", typeName=" <<mi.typeName << ", name=" << mi.name;
    return out;
}

QVector<ModuleInfo> get_module_infos(const VMEConfig *vmeConfig)
{
    QVector<ModuleInfo> result;
    for (auto module: vmeConfig->getAllModuleConfigs())
    {
        ModuleInfo info;
        info.id = module->getId();
        info.typeName = module->getModuleMeta().typeName;
        info.name = module->objectName();
        info.eventId = qobject_cast<EventConfig *>(module->parent())->getId();
        result.push_back(info);
    }
    return result;
}

QVector<ModuleInfo> get_module_infos(Analysis *analysis)
{
    QVector<ModuleInfo> result;
    for (auto propsVariant: analysis->property("ModuleProperties").toList())
    {
        auto props = propsVariant.toMap();
        ModuleInfo info;
        info.id = QUuid(props.value("moduleId").toString());
        info.typeName = props.value("moduleTypeName").toString();
        info.name = props.value("moduleName").toString();

        // Hack to fix the issues that the module type name change from mdpp16
        // to mdpp16_scp brought with it :(
        if (info.typeName == QSL("mdpp16")
            || (info.typeName.isEmpty() && info.name.contains("mdpp16")))
        {
            info.typeName = QSL("mdpp16_scp");
        }

        result.push_back(info);
    }
    return result;

}

struct ChangeInfo
{
    enum ChangeAction
    {
        Discard,    // Delete objects referencing fromModuleId
        Rewrite     // Rewrite objects referencing fromModuleId
    };

    ChangeAction action;
    QUuid fromModuleId;
    QUuid toModuleId;
    QUuid toEventId;
};

static void rewrite_module(Analysis *analysis,
                           const QUuid &fromModuleId,
                           const QUuid &toModuleId,
                           const QUuid &toEventId)
{
    qDebug() << __PRETTY_FUNCTION__ << "rewrite_module: fromModuleId =" << fromModuleId
        << ", toModuleId =" << toModuleId
        << ", toEventId =" << toEventId;

    QUuid fromEventId;

    auto &sources(analysis->getSources());

    for (auto &source: sources)
    {
        qDebug() << __PRETTY_FUNCTION__ << "checking dataSource" << source <<
            ", moduleId=" << source->getModuleId() << ", looking for fromModuleId =" << fromModuleId;

        if (source->getModuleId() == fromModuleId)
        {
            // Save the eventId and use it when changing the operator entries
            // below. No matter how many sources there are, there should only
            // ever be a single eventId as a the module identified by
            // fromModuleId cannot be a member of different events.
            fromEventId = source->getEventId();

            qDebug() << __PRETTY_FUNCTION__ << "setting fromEventId to" << fromEventId << ", found in dataSource" << source;

            // Rewrite the source entry
            source->setModuleId(toModuleId);
            source->setEventId(toEventId);
        }
    }

    auto &operators(analysis->getOperators());
    for (auto &op: operators)
    {
        qDebug() << __PRETTY_FUNCTION__ << "op =" << op << ", eventId =" << op->getEventId();

        if (op->getEventId() == fromEventId)
        {

            qDebug() << __PRETTY_FUNCTION__ << "rewrite op eventId, old =" <<
                op->getEventId() << ", new =" << toEventId;

            op->setEventId(toEventId);
        }
    }

    auto &directories(analysis->getDirectories());
    for (auto &dir: directories)
    {
        qDebug() << __PRETTY_FUNCTION__ << "checking dir =" << dir << ", eventId =" << dir->getEventId();

        if (dir->getEventId() == fromEventId)
        {
            qDebug() << __PRETTY_FUNCTION__ << "rewrite dir eventId, old =" <<
                dir->getEventId() << ", new =" << toEventId;

            dir->setEventId(toEventId);
        }
    }
}

static bool has_no_connected_slots(OperatorInterface *op)
{
    for (s32 slotIndex = 0;
         slotIndex < op->getNumberOfSlots();
         ++slotIndex)
    {
        if (op->getSlot(slotIndex)->isConnected())
            return false;
    }

    return true;
}


static void discard_module(Analysis *analysis, const QUuid &moduleId)
{
    QVector<SourceInterface *> sourcesToRemove;

    for (auto source: analysis->getSources())
    {
        if (source->getModuleId() == moduleId)
        {
            sourcesToRemove.push_back(source.get());
        }
    }

    QSet<OperatorInterface *> operatorsToMaybeRemove;

    for (auto &source: sourcesToRemove)
    {
        collect_dependent_operators(source, operatorsToMaybeRemove);
    }

    for (auto &source: sourcesToRemove)
    {
        analysis->removeSource(source);
    }

    while (!operatorsToMaybeRemove.isEmpty())
    {
        bool didRemove = false;
        auto it = std::find_if(operatorsToMaybeRemove.begin(), operatorsToMaybeRemove.end(),
                               has_no_connected_slots);

        if (it != operatorsToMaybeRemove.end())
        {
            analysis->removeOperator(*it);
            operatorsToMaybeRemove.remove(*it);
            didRemove = true;
        }

        if (!didRemove)
            break;
    }
}

#ifndef QT_NO_DEBUG
static QString to_string(const ModuleInfo &info)
{
    return QString("(%1, %2, %3)")
        .arg(info.id.toString())
        .arg(info.typeName)
        .arg(info.name)
        ;
}
#endif

static void apply_changes(Analysis *analysis, const QVector<ChangeInfo> &changes)
{
    for (auto ci: changes)
    {
        switch (ci.action)
        {
            case ChangeInfo::Rewrite:
                {
                    rewrite_module(analysis, ci.fromModuleId, ci.toModuleId, ci.toEventId);
                } break;

            case ChangeInfo::Discard:
                {
                    discard_module(analysis, ci.fromModuleId);
                } break;
        }
    }
}

bool auto_assign_vme_modules(const VMEConfig *vmeConfig, analysis::Analysis *analysis, LoggerFun logger)
{
    auto vModInfos = get_module_infos(vmeConfig);
    return auto_assign_vme_modules(vModInfos, analysis, logger);
}

bool auto_assign_vme_modules(QVector<ModuleInfo> vModInfos, analysis::Analysis *analysis, LoggerFun logger)
{
    // TODO: implement priority matching based on module names (the types have to match of course)

    auto do_log = [logger] (const QString &msg) { if (logger) logger(msg); };


    QSet<std::pair<QUuid, QUuid>> vmeModuleAndEventIds;

    for (auto modInfo: vModInfos)
    {
        vmeModuleAndEventIds.insert(std::make_pair(modInfo.id, modInfo.eventId));
    }

    auto aModInfos = get_module_infos(analysis);
    QSet<std::pair<QUuid, QUuid>> analysisModuleAndEventIds;
    for (auto modInfo: aModInfos)
    {
        analysisModuleAndEventIds.insert(std::make_pair(modInfo.id, modInfo.eventId));
    }

#if 0
    qDebug() << __PRETTY_FUNCTION__ << "vModInfos:";
    for (const auto &mi: vModInfos)
        qDebug() << __PRETTY_FUNCTION__ << "  " << mi;

    qDebug() << __PRETTY_FUNCTION__ << "aModInfos:";
    for (const auto &mi: aModInfos)
        qDebug() << __PRETTY_FUNCTION__ << "  " << mi;
#endif


    //qDebug() << __PRETTY_FUNCTION__ << "analysisModuleAndEventIds:" << analysisModuleAndEventIds;
    //qDebug() << __PRETTY_FUNCTION__ << "vmeModuleAndEventIds:" << vmeModuleAndEventIds;


    // Remove entries that are equal in both module id and event id
    analysisModuleAndEventIds.subtract(vmeModuleAndEventIds);

    // Remove entries where the module ids are equal and the analysis side event id is null.
    // FIXME: operators connected to this module do not get their event id updated.
    for (const auto &vModIds: vmeModuleAndEventIds)
    {
        auto vmeModuleIdAndNullEventId = std::make_pair(vModIds.first, QUuid());

        if (analysisModuleAndEventIds.contains(vmeModuleIdAndNullEventId))
            analysisModuleAndEventIds.remove(vmeModuleIdAndNullEventId);
    }

    if (analysisModuleAndEventIds.isEmpty()) // True if all analysis modules exist in the vme config
    {
        qDebug() << __PRETTY_FUNCTION__ << "auto_assign: all modules match";
        return true;
    }

    QVector<ChangeInfo> moduleChangeInfos;

    for (auto moduleAndEventId: analysisModuleAndEventIds)
    {
        auto moduleId = moduleAndEventId.first;

        ModuleInfo modInfo = *std::find_if(aModInfos.begin(), aModInfos.end(),
                                           [moduleId](const auto &modInfo) { return modInfo.id == moduleId; });

        QString typeName = modInfo.typeName;
        auto numACandidates = std::count_if(aModInfos.begin(), aModInfos.end(),
                                            [typeName](const auto &modInfo) { return modInfo.typeName == typeName; });

        auto numVCandidates = std::count_if(vModInfos.begin(), vModInfos.end(),
                                            [typeName](const auto &modInfo) { return modInfo.typeName == typeName; });

        if (numACandidates == 1 && numVCandidates == 1)
        {
            // One to one assignment is possible. Extract the required
            // information and store it for later use.

            auto targetModInfo = *std::find_if(vModInfos.begin(), vModInfos.end(),
                                               [typeName](const auto &modInfo) { return modInfo.typeName == typeName; });

            ChangeInfo info;
            info.action         = ChangeInfo::Rewrite;
            info.fromModuleId   = moduleId;
            info.toModuleId     = targetModInfo.id;
            info.toEventId      = targetModInfo.eventId;
            moduleChangeInfos.push_back(info);

#ifndef QT_NO_DEBUG
            auto eventId = moduleAndEventId.second;
            qDebug() << __PRETTY_FUNCTION__ << "pushing rewrite: modules:"
                << to_string(modInfo) << "->" << to_string(targetModInfo)
                << ", event ids =" << eventId << "->" << info.toEventId;
#endif
        }
    }

    // Not all modules can be auto assigned
    if (moduleChangeInfos.size() != analysisModuleAndEventIds.size())
    {
        qDebug() << "auto_assign: could not auto-assign all modules";
        return false;
    }

    apply_changes(analysis, moduleChangeInfos);

    return true;
}

VMEIdToIndex build_id_to_index_mapping(const VMEConfig *vmeConfig)
{
    VMEIdToIndex result;

    auto eventConfigs = vmeConfig->getEventConfigs();

    for (s32 eventIndex = 0;
         eventIndex < eventConfigs.size();
         eventIndex++)
    {
        auto eventConfig = eventConfigs.at(eventIndex);
        auto moduleConfigs = eventConfig->getModuleConfigs();

        result.insert(eventConfig->getId(), { eventIndex, -1 });

        for (s32 moduleIndex = 0;
             moduleIndex < moduleConfigs.size();
             ++moduleIndex)
        {
            auto moduleConfig = moduleConfigs.at(moduleIndex);

            result.insert(moduleConfig->getId(), { eventIndex, moduleIndex });
        }
    }

    return result;
}

}
