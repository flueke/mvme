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

void add_vme_properties_to_analysis(const VMEConfig *vmeConfig, analysis::Analysis *analysis)
{
    QVariantList modulePropertyList;

    for (auto module: vmeConfig->getAllModuleConfigs())
    {
        QVariantMap moduleProperties;
        moduleProperties["moduleId"] = module->getId().toString();
        moduleProperties["moduleTypeName"] = module->getModuleMeta().typeName;
        moduleProperties["moduleName"] = module->objectName();
        modulePropertyList.push_back(moduleProperties);
    }

    analysis->setProperty("ModuleProperties", modulePropertyList);
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

static QString to_string(const ModuleInfo &info)
{
    return QString("(%1, %2, %3)")
        .arg(info.id.toString())
        .arg(info.typeName)
        .arg(info.name)
        ;
}

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

    qDebug() << __PRETTY_FUNCTION__ << "vModInfos:";
    for (const auto &mi: vModInfos)
        qDebug() << __PRETTY_FUNCTION__ << "  " << mi;

    qDebug() << __PRETTY_FUNCTION__ << "aModInfos:";
    for (const auto &mi: aModInfos)
        qDebug() << __PRETTY_FUNCTION__ << "  " << mi;


    //qDebug() << __PRETTY_FUNCTION__ << "analysisModuleAndEventIds:" << analysisModuleAndEventIds;
    //qDebug() << __PRETTY_FUNCTION__ << "vmeModuleAndEventIds:" << vmeModuleAndEventIds;


    // Remove entries that are equal in both module id and event id
    analysisModuleAndEventIds.subtract(vmeModuleAndEventIds);

    // Remove entries where the module ids are equal and the analysis side event id is null.
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
        auto eventId = moduleAndEventId.second;

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

            qDebug() << __PRETTY_FUNCTION__ << "pushing rewrite: modules:"
                << to_string(modInfo) << "->" << to_string(targetModInfo)
                << ", event ids =" << eventId << "->" << info.toEventId;
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

bool run_vme_analysis_module_assignment_ui(const VMEConfig *vmeConfig, analysis::Analysis *analysis, QWidget *parent)
{
    auto vModInfos = get_module_infos(vmeConfig);
    return run_vme_analysis_module_assignment_ui(vModInfos, analysis, parent);
}

bool run_vme_analysis_module_assignment_ui(QVector<ModuleInfo> vModInfos, analysis::Analysis *analysis, QWidget *parent)
{
    auto aModInfos = get_module_infos(analysis);

    auto cmp_typeName = [](const auto &a, const auto &b)
    {
        return a.typeName < b.typeName;
    };

    std::sort(vModInfos.begin(), vModInfos.end(), cmp_typeName);
    std::sort(aModInfos.begin(), aModInfos.end(), cmp_typeName);

    auto mainTable = new QTableWidget(aModInfos.size(), vModInfos.size() + 1);

    /* Source: https://stackoverflow.com/a/38804129
     * This fixes the look of the table widget in Windows 10.
     */
    if(QSysInfo::windowsVersion() == QSysInfo::WV_WINDOWS10)
    {
        mainTable->setStyleSheet(
            "QHeaderView::section{"
            "border-top:0px solid #D8D8D8;"
            "border-left:0px solid #D8D8D8;"
            "border-right:1px solid #D8D8D8;"
            "border-bottom: 1px solid #D8D8D8;"
            "background-color:white;"
            "padding:4px;"
            "}"
            "QTableCornerButton::section{"
            "border-top:0px solid #D8D8D8;"
            "border-left:0px solid #D8D8D8;"
            "border-right:1px solid #D8D8D8;"
            "border-bottom: 1px solid #D8D8D8;"
            "background-color:white;"
            "}"
            "QHeaderView{background-color:white;}"
            );
    }
    mainTable->setSelectionMode(QAbstractItemView::NoSelection);

    // Vertical: Analysis modules
    for (s32 row = 0; row < aModInfos.size(); ++row)
    {
        auto header = QString("%1\n%2")
            .arg(aModInfos[row].name)
            .arg(aModInfos[row].typeName);
        mainTable->setVerticalHeaderItem(row, new QTableWidgetItem(header));
    }

    // Horizontal: VME modules
    for (s32 col = 0; col < vModInfos.size(); ++col)
    {
        auto header = QString("%1\n%2")
            .arg(vModInfos[col].name)
            .arg(vModInfos[col].typeName);
        mainTable->setHorizontalHeaderItem(col, new QTableWidgetItem(header));
    }
    mainTable->setHorizontalHeaderItem(vModInfos.size(), new QTableWidgetItem(QSL("discard")));
    mainTable->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);

    struct RadioContainer
    {
        QWidget *container;
        QRadioButton *button;
    };

    auto make_radio_container = []()
    {
        RadioContainer result;
        result.container = new QWidget;
        result.button    = new QRadioButton;
        auto layout = new QHBoxLayout(result.container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setAlignment(Qt::AlignCenter);
        layout->addWidget(result.button);
        return result;
    };

    // One button group for each row in the table
    QVector<QButtonGroup *> radioGroups;

    for (s32 row = 0; row < aModInfos.size(); ++row)
    {
        auto group = new QButtonGroup(mainTable);
        radioGroups.push_back(group);

        for (s32 col = 0; col < vModInfos.size() + 1; ++col)
        {
            RadioContainer radioContainer = {};

            if (col < vModInfos.size())
            {
                auto aModInfo = aModInfos[row];
                auto vModInfo = vModInfos[col];

                if (aModInfo.typeName == vModInfo.typeName)
                {
                    radioContainer = make_radio_container();
                }
            }
            else
            {
                // RadioButton for the "discard" action
                radioContainer = make_radio_container();
            }

            if (radioContainer.button)
            {
                mainTable->setCellWidget(row, col, radioContainer.container);
                group->addButton(radioContainer.button, col);
            }
            else
            {
                auto item = new QTableWidgetItem;
                item->setFlags(Qt::NoItemFlags);
                item->setBackground(mainTable->palette().mid());
                mainTable->setItem(row, col, item);
            }
        }

        // Select the first button of a group by default. There is always at
        // least one button, the "discard" button.
        group->buttons().at(0)->setChecked(true);
    }

    mainTable->resizeColumnsToContents();
    mainTable->resizeRowsToContents();
    mainTable->setMinimumHeight(175);

    QObject::connect(mainTable, &QTableWidget::cellPressed, [&radioGroups](int row, int col) {
        // Check buttons if the user clicks anywhere in the cell. Without this
        // code the click location has to be right on top of the button for it
        // to get checked.
        if (row < radioGroups.size())
        {
            if (auto button = radioGroups[row]->button(col))
            {
                button->setChecked(true);
            }
        }
    });

    static const s32 LegendFontPointSize = 20;

    auto vmeLabel = new QLabel("VME");
    {
        auto font = vmeLabel->font();
        font.setPointSize(LegendFontPointSize);
        vmeLabel->setFont(font);
    }

    auto anaLabel = new VerticalLabel("Analysis");
    {
        auto font = anaLabel->font();
        font.setPointSize(LegendFontPointSize);
        anaLabel->setFont(font);
    }

    static const auto explanation = QSL(
        "<ul>"
        "<li>Columns show possible destination modules present in the current VME configuration.</li>"
        "<li>Rows contain modules from the source analysis.</li>"
        "<li>Use the discard column to exclude a source module from the resulting analysis.</li>"
        "</ul>"
        );

    auto explanationLabel = new QLabel(explanation);

    auto mainLayout = new QGridLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(vmeLabel, 0, 1, Qt::AlignCenter);
    mainLayout->addWidget(anaLabel, 1, 0, Qt::AlignCenter);
    mainLayout->addWidget(mainTable, 1, 1);
    mainLayout->addWidget(explanationLabel, 2, 0, 1, 2);

    QDialog dialog(parent);
    dialog.setWindowTitle("Analysis to VME module assignment");
    add_widget_close_action(&dialog);

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->addLayout(mainLayout);
    dialogLayout->addWidget(buttonBox);

    dialog.resize(600, 300);

    if (dialog.exec() == QDialog::Rejected)
        return false;

    QVector<ChangeInfo> moduleChangeInfos;

    for (s32 row = 0; row < radioGroups.size(); ++row)
    {
        auto aModInfo = aModInfos[row];
        s32 checkedColumn = radioGroups[row]->checkedId();

        ChangeInfo changeInfo;
        changeInfo.fromModuleId = aModInfo.id;

        if (checkedColumn == vModInfos.size())
        {
            changeInfo.action = ChangeInfo::Discard;
        }
        else
        {
            auto vModInfo = vModInfos[checkedColumn];
            changeInfo.action = ChangeInfo::Rewrite;
            changeInfo.toModuleId = vModInfo.id;
            changeInfo.toEventId  = vModInfo.eventId;
        }

        moduleChangeInfos.push_back(changeInfo);
    }

    apply_changes(analysis, moduleChangeInfos);

    return true;
}

void remove_analysis_objects_unless_matching(analysis::Analysis *analysis,
                                             const QUuid &moduleId,
                                             const QUuid &eventId)
{
    auto sources = analysis->getSources();

    for (const auto &source: sources)
    {
        if (source->getModuleId() != moduleId || source->getEventId() != eventId)
        {
            analysis->removeSource(source);
        }
    }

    auto operators = analysis->getOperators();

    for (const auto &op: operators)
    {
        if (op->getEventId() != eventId)
        {
            analysis->removeOperator(op);
        }
    }
}

void remove_analysis_objects_unless_matching(analysis::Analysis *analysis,
                                             const ModuleInfo &moduleInfo)
{
    remove_analysis_objects_unless_matching(analysis, moduleInfo.id, moduleInfo.eventId);
}

void remove_analysis_objects_unless_matching(analysis::Analysis *analysis, const VMEConfig *vmeConfig)
{
    QSet<QUuid> vmeEventIds;
    QSet<QUuid> vmeModuleIds;

    // collect all ids contained in vmeConfig
    for (auto eventConfig: vmeConfig->getEventConfigs())
    {
        vmeEventIds.insert(eventConfig->getId());

        for (auto moduleConfig: eventConfig->getModuleConfigs())
        {
            vmeModuleIds.insert(moduleConfig->getId());
        }
    }

    auto sources = analysis->getSources();

    for (const auto &source: sources)
    {
        if (!vmeEventIds.contains(source->getEventId()))
        {
            analysis->removeSource(source);
        }
    }

    auto operators = analysis->getOperators();

    for (const auto &op: operators)
    {
        if (!vmeEventIds.contains(op->getEventId()))
        {
            analysis->removeOperator(op);
        }
    }
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
