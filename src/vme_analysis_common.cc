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

#include "qt_util.h"

using namespace analysis;

namespace vme_analysis_common
{

void add_vme_properties_to_analysis(VMEConfig *vmeConfig, analysis::Analysis *analysis)
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

QVector<ModuleInfo> get_module_infos(VMEConfig *vmeConfig)
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

static void rewrite_module(Analysis *analysis, const QUuid &fromModuleId, const QUuid &toModuleId, const QUuid &toEventId)
{
    QUuid fromEventId;

    auto &sources(analysis->getSources());
    for (Analysis::SourceEntry &source: sources)
    {
        if (source.moduleId == fromModuleId)
        {
            // Save the eventId and use it when changing the operator entries
            // below. No matter how many sources there are, there should only
            // ever be a single eventId as a the module identified by
            // fromModuleId cannot be a member of different events.
            fromEventId = source.eventId;

            // Rewrite the source entry
            source.moduleId = toModuleId;
            source.eventId = toEventId;
            analysis->setModified(true);
        }
    }

    auto &operators(analysis->getOperators());
    for (Analysis::OperatorEntry &op: operators)
    {
        if (op.eventId == fromEventId)
        {
            op.eventId = toEventId;
            analysis->setModified(true);
        }
    }
}

static void collect_dependent_operators(QSet<OperatorInterface *> &operatorSet, PipeSourceInterface *pipeSource)
{
    for (s32 outputIndex = 0;
         outputIndex < pipeSource->getNumberOfOutputs();
         ++outputIndex)
    {
        auto outputPipe = pipeSource->getOutput(outputIndex);

        for (Slot *destSlot: outputPipe->getDestinations())
        {
            if (destSlot->parentOperator)
            {
                operatorSet.insert(destSlot->parentOperator);
                collect_dependent_operators(operatorSet, destSlot->parentOperator);
            }
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

    for (auto sourceEntry: analysis->getSources())
    {
        if (sourceEntry.moduleId == moduleId)
        {
            sourcesToRemove.push_back(sourceEntry.source.get());
        }
    }

    QSet<OperatorInterface *> operatorsToMaybeRemove;

    for (auto &source: sourcesToRemove)
    {
        collect_dependent_operators(operatorsToMaybeRemove, source);
    }

    for (auto &source: sourcesToRemove)
    {
        analysis->removeSource(source);
    }

    while (!operatorsToMaybeRemove.isEmpty())
    {
        bool didRemove = false;
        auto it = std::find_if(operatorsToMaybeRemove.begin(), operatorsToMaybeRemove.end(), has_no_connected_slots);

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

static void apply_changes(Analysis *analysis, const QVector<ChangeInfo> &changes)
{
    for (auto ci: changes)
    {
        switch (ci.action)
        {
            case ChangeInfo::Rewrite:
                rewrite_module(analysis, ci.fromModuleId, ci.toModuleId, ci.toEventId);
                break;

            case ChangeInfo::Discard:
                discard_module(analysis, ci.fromModuleId);
                break;
        }
    }
}

bool auto_assign_vme_modules(VMEConfig *vmeConfig, analysis::Analysis *analysis)
{
    auto vModInfos = get_module_infos(vmeConfig);
    return auto_assign_vme_modules(vModInfos, analysis);
}

bool auto_assign_vme_modules(QVector<ModuleInfo> vModInfos, analysis::Analysis *analysis)
{
    auto aModInfos = get_module_infos(analysis);

    QSet<QUuid> vModIds;
    for (auto modInfo: vModInfos)
    {
        vModIds.insert(modInfo.id);
    }

    QSet<QUuid> aModIds;
    for (auto modInfo: aModInfos)
    {
        aModIds.insert(modInfo.id);
    }

    aModIds.subtract(vModIds);

    if (aModIds.isEmpty()) // True if all analysis modules exist in the vme config
        return true;

    QVector<ChangeInfo> moduleChangeInfos;

    for (auto moduleId: aModIds)
    {
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

        }
    }

    // Not all modules can be auto assigned
    if (moduleChangeInfos.size() != aModIds.size())
        return false;

    apply_changes(analysis, moduleChangeInfos);

    return true;
}

bool run_vme_analysis_module_assignment_ui(VMEConfig *vmeConfig, analysis::Analysis *analysis, QWidget *parent)
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

    auto mainLayout = new QGridLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(vmeLabel, 0, 1, Qt::AlignCenter);
    mainLayout->addWidget(anaLabel, 1, 0, Qt::AlignCenter);
    mainLayout->addWidget(mainTable, 1, 1);

    QDialog dialog;
    dialog.setWindowTitle("Analysis to VME Module assignment");
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

void remove_analysis_objects_unless_matching(analysis::Analysis *analysis, const ModuleInfo &moduleInfo)
{
    QVector<Analysis::SourceEntry> sources = analysis->getSources();

    for (const auto &source: sources)
    {
        if (source.moduleId != moduleInfo.id || source.eventId != moduleInfo.eventId)
        {
            analysis->removeSource(source.source);
        }
    }

    QVector<Analysis::OperatorEntry> operators = analysis->getOperators();

    for (const auto &op: operators)
    {
        if (op.eventId != moduleInfo.eventId)
        {
            analysis->removeOperator(op.op);
        }
    }
}

}
