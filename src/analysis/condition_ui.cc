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
#include "condition_ui.h"
#include "condition_ui_p.h"

#include <algorithm>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <memory>

#include "analysis/analysis.h"
#include "analysis/analysis_util.h"
#include "analysis/ui_lib.h"
#include "gui_util.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"
#include "qt_util.h"
#include "treewidget_utils.h"
#include "ui_interval_condition_dialog.h"
#include "histo_ui.h"
#include "histo1d_widget.h"

namespace analysis
{
namespace ui
{

using ConditionInterface = analysis::ConditionInterface;

namespace
{

enum NodeType
{
    NodeType_Condition = QTreeWidgetItem::UserType,
    NodeType_ConditionBit,
};

enum DataRole
{
    DataRole_AnalysisObject = Global_DataRole_AnalysisObject,
    DataRole_RawPointer,
    DataRole_BitIndex
};

class TreeNode: public BasicTreeNode
{
    public:
        using BasicTreeNode::BasicTreeNode;

        // Custom sorting for numeric columns
        virtual bool operator<(const QTreeWidgetItem &other) const override
        {
            if (type() == other.type() && treeWidget() && treeWidget()->sortColumn() == 0)
            {
                if (type() == NodeType_ConditionBit)
                {
                    s32 thisIndex  = data(0, DataRole_BitIndex).toInt();
                    s32 otherIndex = other.data(0, DataRole_BitIndex).toInt();
                    return thisIndex < otherIndex;
                }
            }
            return QTreeWidgetItem::operator<(other);
        }
};

template<typename T>
TreeNode *make_node(T *data, int type = QTreeWidgetItem::Type, int dataRole = DataRole_RawPointer)
{
    auto ret = new TreeNode(type);
    ret->setData(0, dataRole, QVariant::fromValue(static_cast<void *>(data)));
    ret->setFlags(ret->flags() & ~(Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled));
    return ret;
}

#if 0
TreeNode *make_condition_node(ConditionInterface *cond)
{
    auto ret = make_node(cond, NodeType_Condition, DataRole_AnalysisObject);

    ret->setData(0, Qt::EditRole, cond->objectName());
    ret->setData(0, Qt::DisplayRole, QString("<b>%1</b> %2").arg(
            cond->getShortName(),
            cond->objectName()));

    ret->setFlags(ret->flags() | Qt::ItemIsEditable);

    return ret;
}
#endif

} // end anon namespace

#if 0

//
// NodeModificationButtons
//
NodeModificationButtons::NodeModificationButtons(QWidget *parent)
    : QWidget(parent)
{
    pb_accept = new QPushButton(QSL("Accept"));
    pb_reject = new QPushButton(QSL("Cancel"));

    for (auto button: { pb_accept, pb_reject })
    {
        set_widget_font_pointsize_relative(button, -2);
        button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        button->setMinimumWidth(5);
    }

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);

    layout->addStretch(1);
    layout->addWidget(pb_accept);
    layout->addWidget(pb_reject);

    connect(pb_accept, &QPushButton::clicked, this, &NodeModificationButtons::accept);
    connect(pb_reject, &QPushButton::clicked, this, &NodeModificationButtons::reject);
}

//
// ConditionTreeWidget
//

struct ConditionTreeWidget::Private
{
    static const int ButtonsColumn = 1;

    explicit Private(ConditionTreeWidget *q): m_q(q) {}
    AnalysisServiceProvider *getAnalysisServiceProvider() const { return m_asp; }
    Analysis *getAnalysis() const { return getAnalysisServiceProvider()->getAnalysis(); }
    QUuid getEventId() const { return m_eventId; }
    int getEventIndex() const { return m_eventIndex; }
    void doContextMenu(const QPoint &pos);
    void removeObject(const AnalysisObjectPtr &obj);

    ConditionTreeWidget *m_q;
    AnalysisServiceProvider *m_asp;
    QUuid m_eventId;
    int m_eventIndex;
    QSet<void *> m_expandedObjects;
    ObjectToNode m_objectMap;
};

ConditionTreeWidget::ConditionTreeWidget(AnalysisServiceProvider *asp_, const QUuid &eventId, int eventIndex,
                                         QWidget *parent)
    : QTreeWidget(parent)
    , m_d(std::make_unique<Private>(this))
{
    //qDebug() << __PRETTY_FUNCTION__ << this;

    // Private setup
    m_d->m_asp = asp_;
    m_d->m_eventId = eventId;
    m_d->m_eventIndex = eventIndex;

    // QTreeWidget settings
    setExpandsOnDoubleClick(false);
    setItemDelegate(new HtmlDelegate(this));
    setContextMenuPolicy(Qt::CustomContextMenu);

    // columns: 0 -> name,  1 -> accept/reject buttons
    setColumnCount(2);
    headerItem()->setText(0, QSL("Name"));
    headerItem()->setText(1, QSL(""));

    // interactions

    QObject::connect(this, &QWidget::customContextMenuRequested,
                     this, [this] (QPoint pos) { m_d->doContextMenu(pos); });

    // FIXME: restoring expanded state_ doesn't work because the trees are
    // recreated in ConditionWidget::repopulate()
#if 0
    connect(this, &QTreeWidget::itemExpanded,
            this, [this] (QTreeWidgetItem *node) {

        if (auto obj = get_pointer<void>(node))
        {
            m_d->m_expandedObjects.insert(obj);
        }
    });

    connect(this, &QTreeWidget::itemCollapsed,
            this, [this] (QTreeWidgetItem *node) {

        if (auto obj = get_pointer<void>(node))
        {
            m_d->m_expandedObjects.remove(obj);
        }
    });
#endif

    // init
    repopulate();
}

ConditionTreeWidget::~ConditionTreeWidget()
{
    //qDebug() << __PRETTY_FUNCTION__ << this;
}

void ConditionTreeWidget::repopulate()
{
    clear();
    m_d->m_objectMap.clear();

    auto analysis = m_d->getAnalysis();
    auto conditions = analysis->getConditions(m_d->getEventId());

    std::sort(conditions.begin(), conditions.end(), [](auto c1, auto c2) {
        return c1->objectName() < c2->objectName();
    });

    auto make_mod_buttons = [this](QTreeWidgetItem *node) -> NodeModificationButtons *
    {
        auto modButtonsWidget = new NodeModificationButtons;

        this->setItemWidget(node, Private::ButtonsColumn, modButtonsWidget);

        modButtonsWidget->setButtonsVisible(false);

        connect(modButtonsWidget, &NodeModificationButtons::accept,
                this, &ConditionTreeWidget::applyConditionAccept);

        connect(modButtonsWidget, &NodeModificationButtons::reject,
                this, &ConditionTreeWidget::applyConditionReject);

        return modButtonsWidget;
    };

    for (const auto &cond: conditions)
    {
        auto node = make_condition_node(cond.get());
        addTopLevelItem(node);
        m_d->m_objectMap[cond] = node;
        make_mod_buttons(node);
    }

    resizeColumnToContents(0);

    expand_tree_nodes(invisibleRootItem(), m_d->m_expandedObjects, 0,
                      { DataRole_AnalysisObject, DataRole_RawPointer});
}

void ConditionTreeWidget::doPeriodicUpdate()
{
}

static const QColor InputNodeOfColor            = QColor(0x90, 0xEE, 0x90, 255.0/3); // lightgreen but with some alpha
static const QColor ChildIsInputNodeOfColor     = QColor(0x90, 0xEE, 0x90, 255.0/6);

void ConditionTreeWidget::highlightConditionLink(const ConditionLink &cl)
{
    //qDebug() << __PRETTY_FUNCTION__ << cl.condition << cl.subIndex;

    clearHighlights();

    if (auto condNode = m_d->m_objectMap[cl.condition])
    {
        condNode->setBackground(0, InputNodeOfColor);
    }
    else
    {
        InvalidCodePath;
    }
}

void ConditionTreeWidget::clearHighlights()
{
    walk_treewidget_nodes(invisibleRootItem(), [] (QTreeWidgetItem *node) {
        node->setBackground(0, QColor(0, 0, 0, 0));
    });
}

void ConditionTreeWidget::setModificationButtonsVisible(const ConditionLink &cl, bool visible)
{
    //qDebug() << __PRETTY_FUNCTION__ << cl.condition << visible;

    QTreeWidgetItem *node = cl ? m_d->m_objectMap[cl.condition] : nullptr;

    if (node)
    {
        if (auto modButtons = qobject_cast<NodeModificationButtons *>(itemWidget(
                    node, Private::ButtonsColumn)))
        {
            modButtons->setButtonsVisible(visible);
        }

        auto conditionNode = node;

        /* Unset ItemIsEnabled on all other nodes. */
        auto walker = [conditionNode, visible](QTreeWidgetItem *walkedNode)
        {
            if (walkedNode != conditionNode)
            {
                Qt::ItemFlags flags = walkedNode->flags();

                if (visible)
                    flags &= ~Qt::ItemIsEnabled;
                else
                    flags |= Qt::ItemIsEnabled;

                walkedNode->setFlags(flags);
            }
        };

        walk_treewidget_nodes(invisibleRootItem(), walker);
    }
}

namespace
{

AnalysisObjectPtr get_analysis_object(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
{
    auto qo = get_qobject(node, dataRole);

    if (auto ao = qobject_cast<AnalysisObject *>(qo))
        return ao->shared_from_this();

    return AnalysisObjectPtr();
}

template<typename T>
std::shared_ptr<T> get_shared_analysis_object(QTreeWidgetItem *node,
                                              s32 dataRole = Qt::UserRole)
{
    auto objPtr = get_analysis_object(node, dataRole);
    return std::dynamic_pointer_cast<T>(objPtr);
}

} // end anon ns

void ConditionTreeWidget::Private::doContextMenu(const QPoint &pos)
{
    QMenu menu;

    auto activeNode = m_q->itemAt(pos);
    ConditionLink activeCl;

    if (activeNode && activeNode->type() == NodeType_Condition)
    {
        activeCl.condition = get_shared_analysis_object<ConditionInterface>(
            activeNode, DataRole_AnalysisObject);
        activeCl.subIndex = 0;

        assert(activeCl);
    }
    else if (activeNode && activeNode->type() == NodeType_ConditionBit)
    {
        if (auto condNode = activeNode->parent())
        {
            activeCl.condition = get_shared_analysis_object<ConditionInterface>(
                condNode, DataRole_AnalysisObject);

            activeCl.subIndex = activeNode->data(0, DataRole_BitIndex).toInt();

            assert(activeCl);
        }
    }

    if (activeCl)
    {
        // edit
        menu.addAction(QIcon(QSL(":/pencil.png")), QSL("Edit in plot"),
                       [this, activeCl] { emit m_q->editConditionGraphically(activeCl); });

        menu.addAction(QIcon(QSL(":/pencil.png")), QSL("Edit in table"),
                       [this, activeCl] { emit m_q->editConditionInEditor(activeCl); });

        if (activeNode && activeNode->type() == NodeType_Condition)
        {
            // rename
            menu.addAction(QIcon(QSL(":/document-rename.png")),
                           QSL("Rename"), [activeNode] () {

                if (auto tw = activeNode->treeWidget())
                {
                    tw->editItem(activeNode);
                }
            });

            // remove
            menu.addSeparator();

            menu.addAction(QIcon::fromTheme("edit-delete"),
                           QSL("Remove selected"), [this, activeCl] {
                removeObject(activeCl.condition);
            });
        }
    }

    if (!menu.isEmpty())
    {
        menu.exec(m_q->mapToGlobal(pos));
    }
}

void ConditionTreeWidget::Private::removeObject(const AnalysisObjectPtr &obj)
{
    if (obj)
    {
        AnalysisPauser pauser(getAnalysisServiceProvider());
        getAnalysis()->removeObjectsRecursively({ obj });
    }
}

//
// ConditionWidget
//

struct ConditionWidget::Private
{
    ConditionWidget *m_q;

    AnalysisServiceProvider *m_asp;
    QToolBar *m_toolbar;
    QStackedWidget *m_treeStack;
    QHash<QUuid, ConditionTreeWidget *> m_treesByEventId;
    ConditionLink m_conditionLinkWithVisibleButtons;

    explicit Private(ConditionWidget *q): m_q(q) {}
    AnalysisServiceProvider *getAnalysisServiceProvider() const { return m_asp; }
    Analysis *getAnalysis() const { return getAnalysisServiceProvider()->getAnalysis(); }

    void onCurrentNodeChanged(QTreeWidgetItem *node);
    void onNodeChanged(QTreeWidgetItem *node, int column);
    void onModificationsAccepted();
    void onModificationsRejected();
    void editConditionInEditor(const ConditionLink &cl);
};

ConditionWidget::ConditionWidget(AnalysisServiceProvider *asp_, QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<Private>(this))
{
    m_d->m_asp = asp_;
    m_d->m_toolbar = make_toolbar();
    m_d->m_treeStack = new QStackedWidget;

    // populate the toolbar
    {
        //auto tb = m_d->m_toolbar;
    }

    // layout
    auto layout = new QVBoxLayout(this);
    layout->addWidget(m_d->m_toolbar);
    layout->addWidget(m_d->m_treeStack);
    layout->setStretch(2, 1);
    layout->setContentsMargins(0, 0, 0, 0);

    setWindowTitle(QSL("Conditions/Cuts"));

    repopulate();
}

ConditionWidget::~ConditionWidget()
{
    //qDebug() << __PRETTY_FUNCTION__ << this;
}


void ConditionWidget::repopulate()
{
    //qDebug() << __PRETTY_FUNCTION__ << this;

    clear_stacked_widget(m_d->m_treeStack);
    m_d->m_treesByEventId.clear();
    m_d->m_conditionLinkWithVisibleButtons = {};

    auto eventConfigs = m_d->getAnalysisServiceProvider()->getVMEConfig()->getEventConfigs();

    for (s32 eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto eventId = eventConfig->getId();

        auto conditionTree = new ConditionTreeWidget(
            m_d->getAnalysisServiceProvider(), eventId, eventIndex);

        m_d->m_treeStack->addWidget(conditionTree);
        m_d->m_treesByEventId[eventId] = conditionTree;

        // interactions
        connect(conditionTree, &QTreeWidget::itemClicked,
                this, [this] (QTreeWidgetItem *node) { m_d->onCurrentNodeChanged(node); });

        connect(conditionTree, &QTreeWidget::currentItemChanged,
                this, [this] (QTreeWidgetItem *node) { m_d->onCurrentNodeChanged(node); });

        connect(conditionTree, &QTreeWidget::itemChanged,
                this, [this] (QTreeWidgetItem *node, int col) { m_d->onNodeChanged(node, col); });

        connect(conditionTree, &ConditionTreeWidget::applyConditionAccept,
                this, [this] { m_d->onModificationsAccepted(); });

        connect(conditionTree, &ConditionTreeWidget::applyConditionReject,
                this, [this] { m_d->onModificationsRejected(); });

        // pass the graphical edit request on to the outside
        connect(conditionTree, &ConditionTreeWidget::editConditionGraphically,
                this, &ConditionWidget::editCondition);

        // handle the "edit in editor" request internally
        connect(conditionTree, &ConditionTreeWidget::editConditionInEditor,
                this, [this] (const ConditionLink &cond) { m_d->editConditionInEditor(cond); });
    }

    assert(m_d->m_treeStack->count() == eventConfigs.size());
}

void ConditionWidget::repopulate(int eventIndex)
{
    //qDebug() << __PRETTY_FUNCTION__ << this << eventIndex;

    if (auto tree = qobject_cast<ConditionTreeWidget *>(m_d->m_treeStack->widget(eventIndex)))
    {
        tree->repopulate();
    }
}

void ConditionWidget::repopulate(const QUuid &eventId)
{
    auto widget = m_d->m_treesByEventId.value(eventId);

    if (auto tree = qobject_cast<ConditionTreeWidget *>(widget))
    {
        tree->repopulate();
    }
}

void ConditionWidget::doPeriodicUpdate()
{
}

void ConditionWidget::selectEvent(int eventIndex)
{
    if (0 <= eventIndex && eventIndex < m_d->m_treeStack->count())
    {
        m_d->m_treeStack->setCurrentIndex(eventIndex);
    }
}

void ConditionWidget::selectEventById(const QUuid &eventId)
{
    if (auto tree = m_d->m_treesByEventId.value(eventId, nullptr))
    {
        m_d->m_treeStack->setCurrentWidget(tree);
    }
}

void ConditionWidget::clearTreeSelections()
{
    for (auto tree: m_d->m_treesByEventId.values())
    {
        tree->clearSelection();
    }
}

void ConditionWidget::Private::onCurrentNodeChanged(QTreeWidgetItem *node)
{
    if (!node) return;

    if (m_conditionLinkWithVisibleButtons)
    {
        /* The accept/reject buttons are shown for some node in the tree. This
         * means the user has made changes to the set of operators linking to
         * the condition being edited and that all other nodes in the current
         * tree have their Qt::ItemIsEnabled flags cleared
         * (setModificationButtonsVisible()).
         *
         * We want to stay in this state_ until the user either accepts or
         * rejects the changes so none of the signals below should be emitted.
         */
        qDebug() << __PRETTY_FUNCTION__ << this << "early return";
        return;
    }

    switch (static_cast<NodeType>(node->type()))
    {
        case NodeType_Condition:
            if (auto cond = qobject_cast<ConditionInterface *>(get_qobject(node,
                                                                           DataRole_AnalysisObject)))
            {
                emit m_q->objectSelected(cond->shared_from_this());

                auto condPtr = std::dynamic_pointer_cast<ConditionInterface>(
                    cond->shared_from_this());

                emit m_q->conditionLinkSelected({ condPtr, 0 });
            }
            else
            {
                InvalidCodePath;
            }

            break;

        case NodeType_ConditionBit:

            if (auto cond = qobject_cast<ConditionInterface *>(get_qobject(node->parent(),
                                                                           DataRole_AnalysisObject)))
            {
                emit m_q->objectSelected(cond->shared_from_this());

                auto condPtr = std::dynamic_pointer_cast<ConditionInterface>(
                    cond->shared_from_this());

                int subIndex = node->data(0, DataRole_BitIndex).toInt();

                emit m_q->conditionLinkSelected({ condPtr, subIndex });
            }
            break;

        default:
            break;
    }
}

void ConditionWidget::Private::onNodeChanged(QTreeWidgetItem *node, int column)
{
    if (column != 0) return;

    if (node->type() == NodeType_Condition)
    {
        if (auto cond = get_pointer<ConditionInterface>(node, DataRole_AnalysisObject))
        {
            auto value    = node->data(0, Qt::EditRole).toString();
            bool modified = value != cond->objectName();

            if (modified)
            {
                cond->setObjectName(value);
                getAnalysis()->setModified(true);
                node->setData(0, Qt::DisplayRole, QString("<b>%1</b> %2").arg(
                        cond->getShortName(),
                        cond->objectName()));

                qDebug() << __PRETTY_FUNCTION__ << cond << cond->objectName();

                if (auto tree = node->treeWidget())
                {
                    tree->resizeColumnToContents(0);
                }

            }
        }
    }

}

void ConditionWidget::clearTreeHighlights()
{
    for (auto tree: m_d->m_treesByEventId.values())
    {
        tree->clearHighlights();
    }
}

void ConditionWidget::highlightConditionLink(const ConditionLink &cl)
{
    clearTreeHighlights();

    if (cl)
    {
        auto eventId = cl.condition->getEventId();

        if (auto tree = m_d->m_treesByEventId[eventId])
        {
            tree->highlightConditionLink(cl);
        }
    }
}

void ConditionWidget::setModificationButtonsVisible(const ConditionLink &cl, bool visible)
{
    assert(cl);
    if (!cl) return;

    auto eventId = cl.condition->getEventId();

    if (auto tree = m_d->m_treesByEventId[eventId])
    {
        tree->setModificationButtonsVisible(cl, visible);

        m_d->m_conditionLinkWithVisibleButtons = (visible ? cl : ConditionLink{});
    }
}

void ConditionWidget::Private::onModificationsAccepted()
{
    auto &cl = m_conditionLinkWithVisibleButtons;
    m_q->setModificationButtonsVisible(cl, false);

    emit m_q->applyConditionAccept();
}

void ConditionWidget::Private::onModificationsRejected()
{
    auto &cl = m_conditionLinkWithVisibleButtons;
    m_q->setModificationButtonsVisible(cl, false);

    emit m_q->applyConditionReject();
}

void ConditionWidget::Private::editConditionInEditor(const ConditionLink &cl)
{
    qDebug() << __PRETTY_FUNCTION__;
    if (auto cond = dynamic_cast<IntervalCondition *>(cl.condition.get()))
    {
        IntervalConditionEditor editor(cond, m_asp, m_q);
        if (editor.exec() == QDialog::Accepted)
        {
            m_q->repopulate();
        }
    }
}
#endif

using namespace histo_ui;

struct IntervalConditionDialog::Private
{
    std::unique_ptr<Ui::IntervalConditionDialog> ui;
};

IntervalConditionDialog::IntervalConditionDialog(QWidget *parent)
    : QDialog(parent)
    , d(std::make_unique<Private>())
{
    d->ui = std::make_unique<Ui::IntervalConditionDialog>();
    d->ui->setupUi(this);
    d->ui->buttonBox->button(QDialogButtonBox::Ok)->setDefault(false);
    d->ui->buttonBox->button(QDialogButtonBox::Ok)->setAutoDefault(false);
    d->ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
    d->ui->tw_intervals->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    connect(d->ui->buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &IntervalConditionDialog::applied);

    connect(d->ui->pb_new, &QPushButton::clicked,
            this, &IntervalConditionDialog::newConditionButtonClicked);

    connect(d->ui->combo_cond, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] (int /*index*/) {
                emit conditionSelected(d->ui->combo_cond->currentData().toUuid());
            });

    connect(d->ui->combo_cond, &QComboBox::editTextChanged,
            this, [this] (const QString &text) {
                emit conditionNameChanged(
                    d->ui->combo_cond->currentData().toUuid(),
                    text);
            });
}

IntervalConditionDialog::~IntervalConditionDialog()
{
    qDebug() << __PRETTY_FUNCTION__;
}

void IntervalConditionDialog::setConditionList(const QVector<ConditionInfo> &condInfos)
{
    d->ui->combo_cond->clear();
    for (const auto &info: condInfos)
    {
        d->ui->combo_cond->addItem(info.second, info.first);
    }

    d->ui->combo_cond->setEditable(d->ui->combo_cond->count() > 0);
}

void IntervalConditionDialog::setIntervals(const QVector<QwtInterval> &intervals)
{
    d->ui->tw_intervals->clearContents();
    d->ui->tw_intervals->setRowCount(intervals.size());
    int row = 0;
    for (const auto &interval: intervals)
    {
        auto x1Item = new QTableWidgetItem(QString::number(interval.minValue()));
        auto x2Item = new QTableWidgetItem(QString::number(interval.maxValue()));
        d->ui->tw_intervals->setItem(row, 0, x1Item);
        d->ui->tw_intervals->setItem(row, 1, x2Item);

        auto headerItem = new QTableWidgetItem(QString::number(row));
        d->ui->tw_intervals->setVerticalHeaderItem(row, headerItem);

        ++row;
    }
    d->ui->tw_intervals->resizeRowsToContents();
    d->ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!intervals.isEmpty());
    d->ui->buttonBox->button(QDialogButtonBox::Apply)->setEnabled(!intervals.isEmpty());
}


QVector<QwtInterval> IntervalConditionDialog::getIntervals() const
{
    QVector<QwtInterval> ret;

    for (int row=0; row<d->ui->tw_intervals->rowCount(); ++row)
    {
        auto x1Item = d->ui->tw_intervals->item(row, 0);
        auto x2Item = d->ui->tw_intervals->item(row, 1);

        double x1 = x1Item->data(Qt::EditRole).toDouble();
        double x2 = x2Item->data(Qt::EditRole).toDouble();

        ret.push_back({x1, x2});
    }

    return ret;
}

QString IntervalConditionDialog::getConditionName() const
{
    return d->ui->combo_cond->currentText();
}

void IntervalConditionDialog::setInfoText(const QString &txt)
{
    d->ui->label_info->setText(txt);
}

void IntervalConditionDialog::selectCondition(const QUuid &objectId)
{
    auto idx = d->ui->combo_cond->findData(objectId);

    if (idx >= 0)
        d->ui->combo_cond->setCurrentIndex(idx);
}

void IntervalConditionDialog::selectInterval(int index)
{
    if (auto item = d->ui->tw_intervals->item(index, 0))
    {
        d->ui->tw_intervals->setCurrentItem(item, QItemSelectionModel::Rows | QItemSelectionModel::ClearAndSelect);
    }
}

void IntervalConditionDialog::reject()
{
    d->ui->tw_intervals->clearContents();
    d->ui->combo_cond->clear();
    d->ui->combo_cond->setEditable(false);
    QDialog::reject();
}

struct IntervalConditionEditorController::Private
{
    enum class State
    {
        Inactive,
        NewInterval,
        EditInterval
    };

    Histo1DSinkPtr sink_;
    IPlotWidget *histoWidget_;
    IntervalConditionDialog *dialog_;
    AnalysisServiceProvider *asp_;

    // Currently selected condition id. Updated when the dialoge emits conditionSelected()
    QUuid currentConditionId_;

    // Set if a new condition is to be created. Cleared once the condition has
    // been added to the analysis via 'apply'.
    std::shared_ptr<IntervalCondition> newCond_;

    // Intervals currently being edited.
    QVector<QwtInterval> intervals_;

    // Interval edit state
    State state_ = State::Inactive;

    NewIntervalPicker *newPicker_ = nullptr;
    IntervalEditorPicker *editPicker_ = nullptr;

    void repopulateDialogFromAnalysis()
    {
        auto conditions = getEditableConditions();
        auto condInfos = getConditionInfos(conditions);
        dialog_->setConditionList(condInfos);
    }

    void transitionState(State newState)
    {
        switch (newState)
        {
            case State::Inactive:
                {
                    newPicker_->reset();
                    newPicker_->setEnabled(false);
                    editPicker_->reset();
                    editPicker_->setEnabled(false);

                    if (auto zoomAction = histoWidget_->findChild<QAction *>("zoomAction"))
                        zoomAction->setChecked(true);

                    dialog_->setInfoText("Click the \"New\" button to create a new interval condition working on the histograms input.");
                } break;

            case State::NewInterval:
                {
                    newPicker_->reset();
                    newPicker_->setEnabled(true);
                    editPicker_->reset();
                    editPicker_->setEnabled(false);

                    if (auto zoomAction = histoWidget_->findChild<QAction *>("zoomAction"))
                        zoomAction->setChecked(false);

                    dialog_->setInfoText("Select two points in the histogram to create the initial condition intervals.");
                } break;

            case State::EditInterval:
                {
                    newPicker_->reset();
                    newPicker_->setEnabled(false);
                    editPicker_->reset();
                    editPicker_->setEnabled(true);

                    if (auto zoomAction = histoWidget_->findChild<QAction *>("zoomAction"))
                        zoomAction->setChecked(false);

                    dialog_->setInfoText("Drag interval borders to modify. Use the \"Histogram #\" box to cycle through histograms.");
                } break;
        }

        state_ = newState;
    }

    void onDialogApplied()
    {
        std::shared_ptr<IntervalCondition> cond;

        if (newCond_ && newCond_->getId() == currentConditionId_)
            cond = newCond_;
        else
            cond = getCondition(currentConditionId_);

        if (!cond)
            return;

        AnalysisPauser pauser(asp_);

        cond->setObjectName(dialog_->getConditionName());
        cond->setIntervals(intervals_);

        if (!cond->getAnalysis())
        {
            // It's a new condition. Connect its input and add it to the analysis.
            cond->connectArrayToInputSlot(0, sink_->getSlot(0)->inputPipe);
            asp_->getAnalysis()->addOperator(sink_->getEventId(), sink_->getUserLevel(), cond);
        }
        else
        {
            assert(asp_->getAnalysis() == cond->getAnalysis().get());
            asp_->setAnalysisOperatorEdited(cond);
        }

        newCond_ = {};
        repopulateDialogFromAnalysis();
        dialog_->selectCondition(cond->getId());
    }

    void onDialogAccepted()
    {
        qDebug() << __PRETTY_FUNCTION__ << this;
        onDialogApplied();
    }

    void onDialogRejected()
    {
        qDebug() << __PRETTY_FUNCTION__ << this;
        transitionState(State::Inactive);
        newCond_ = {};
        intervals_ = {};
        currentConditionId_ = QUuid();
    }

    // Keeps the dialog_ at the top right of the histo widget.
    void updateDialogPosition()
    {
        int x = histoWidget_->x() + histoWidget_->frameGeometry().width() + 2;
        int y = histoWidget_->y();
        dialog_->move({x, y});
        dialog_->resize(dialog_->width(), histoWidget_->height());
    }

    void onNewConditionRequested()
    {
        newCond_ = std::make_shared<IntervalCondition>();
        newCond_->setObjectName("new condition");
        intervals_ = {};

        auto conditions = getEditableConditions();
        conditions.push_back(newCond_);
        auto condInfos = getConditionInfos(conditions);

        dialog_->setConditionList(condInfos);
        dialog_->selectCondition(newCond_->getId());
        dialog_->setIntervals(intervals_);

        transitionState(State::NewInterval);
    }

    void onNewIntervalSelected(const QwtInterval &interval)
    {
        if (state_ == State::NewInterval)
        {
            // Set all intervals to the same, newly selected value.
            intervals_.resize(sink_->getNumberOfHistos());
            intervals_.fill(interval);

            dialog_->setIntervals(intervals_);

            if (auto w = qobject_cast<Histo1DWidget *>(histoWidget_))
                dialog_->selectInterval(w->currentHistoIndex());

            // Transition to EditInterval state
            transitionState(State::EditInterval);
            editPicker_->setInterval(interval);
        }
        else
            assert(false);
    }

    void onIntervalModified(const QwtInterval &interval)
    {
        if (state_ == State::EditInterval)
        {
            int intervalIndex = 0;

            if (auto w = qobject_cast<Histo1DWidget *>(histoWidget_))
                intervalIndex = w->currentHistoIndex();

            if (intervalIndex < intervals_.size())
                intervals_[intervalIndex] = interval;

            dialog_->setIntervals(intervals_);
        }
        else
            assert(false);
    }

    void onHistogramSelected(int histoIndex)
    {
        if (state_ == State::EditInterval && histoIndex < intervals_.size())
        {
            editPicker_->setInterval(intervals_[histoIndex]);
            dialog_->selectInterval(histoIndex);
        }
    }

    ConditionVector getEditableConditions()
    {
        if (auto analysis = sink_->getAnalysis())
            return find_conditions_for_sink(sink_, analysis->getConditions());
        return {};
    };

    std::shared_ptr<IntervalCondition> getCondition(const QUuid &objectId)
    {
        for (const auto &condPtr: getEditableConditions())
            if (condPtr->getId() == objectId)
                return std::dynamic_pointer_cast<IntervalCondition>(condPtr);
        return {};
    }

    QVector<IntervalConditionDialog::ConditionInfo> getConditionInfos(
        const ConditionVector &conditions)
    {
        QVector<IntervalConditionDialog::ConditionInfo> condInfos;

        for (const auto &cond: conditions)
            condInfos.push_back(std::make_pair(cond->getId(), cond->objectName()));

        return condInfos;
    }

    QVector<QwtInterval> getIntervals(const QUuid &id)
    {
        if (auto intervalCond = getCondition(id))
            return intervalCond->getIntervals();
        return {};
    }

    void onConditionSelected(const QUuid &id)
    {
        currentConditionId_ = id;
        intervals_ = getIntervals(id);
        dialog_->setIntervals(intervals_);

        if (newCond_ && newCond_->getId() == id)
        {
            transitionState(State::NewInterval);
        }
        else
        {
            dialog_->setIntervals(intervals_);
            transitionState(State::EditInterval);

            if (auto w = qobject_cast<Histo1DWidget *>(histoWidget_))
            {
                int intervalIndex = w->currentHistoIndex();

                if (intervalIndex < intervals_.size())
                {
                    editPicker_->setInterval(intervals_[intervalIndex]);
                    dialog_->selectInterval(intervalIndex);
                }
            }
        }
    }
};

IntervalConditionEditorController::IntervalConditionEditorController(
            const Histo1DSinkPtr &sinkPtr,
            IPlotWidget *histoWidget,
            AnalysisServiceProvider *asp,
            QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->sink_ = sinkPtr;
    d->histoWidget_ = histoWidget;
    d->dialog_ = new IntervalConditionDialog(histoWidget);
    d->asp_ = asp;
    d->newPicker_ = new NewIntervalPicker(histoWidget->getPlot());
    d->newPicker_->setEnabled(false);
    d->editPicker_ = new IntervalEditorPicker(histoWidget->getPlot());
    d->editPicker_->setEnabled(false);

    connect(d->dialog_, &IntervalConditionDialog::applied,
            this, [this] () { d->onDialogApplied(); });

    connect(d->dialog_, &QDialog::accepted,
            this, [this] () { d->onDialogAccepted(); });

    connect(d->dialog_, &QDialog::rejected,
            this, [this] () { d->onDialogRejected(); });

    connect(d->dialog_, &IntervalConditionDialog::newConditionButtonClicked,
            this, [this] () { d->onNewConditionRequested(); });

    connect(d->dialog_, &IntervalConditionDialog::conditionSelected,
           this, [this] (const QUuid &id) { d->onConditionSelected(id); });

    connect(d->newPicker_, &NewIntervalPicker::intervalSelected,
            this, [this] (const QwtInterval &interval) { d->onNewIntervalSelected(interval); });

    connect(d->editPicker_, &IntervalEditorPicker::intervalModified,
            this, [this] (const QwtInterval &interval) { d->onIntervalModified(interval); });

    if (auto histo1DWidget = qobject_cast<Histo1DWidget *>(histoWidget))
    {
        connect(histo1DWidget, &Histo1DWidget::histogramSelected,
               this, [this] (int histoIndex) { d->onHistogramSelected(histoIndex); });
    }

    d->histoWidget_->installEventFilter(this);
    d->dialog_->installEventFilter(this);
    d->dialog_->setInfoText("Use the \"Condition Name\" controls to edit an existing or create a new condition.");

    d->updateDialogPosition();
    d->dialog_->show();
}

IntervalConditionEditorController::~IntervalConditionEditorController()
{
    qDebug() << __PRETTY_FUNCTION__;
    delete d->dialog_;

    if (auto zoomAction = d->histoWidget_->findChild<QAction *>("zoomAction"))
        zoomAction->setChecked(true);
}

bool IntervalConditionEditorController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == d->histoWidget_
        && (event->type() == QEvent::Move
            || event->type() == QEvent::Resize))
    {
        d->updateDialogPosition();
    }

    return QObject::eventFilter(watched, event);
}

void IntervalConditionEditorController::setEnabled(bool on)
{
    if (on)
    {
        d->repopulateDialogFromAnalysis();
    }
    else
    {
        d->dialog_->reject();
        if (auto zoomAction = d->histoWidget_->findChild<QAction *>("zoomAction"))
            zoomAction->setChecked(true);
    }

    d->dialog_->setVisible(on);
}

IntervalConditionDialog *IntervalConditionEditorController::getDialog() const
{
    return d->dialog_;
}

//
// PolygonConditionEditorController
//

struct PolygonConditionEditorController::Private
{
};

PolygonConditionEditorController::PolygonConditionEditorController(
    const Histo2DSinkPtr,
    histo_ui::IPlotWidget *histoWidget,
    AnalysisServiceProvider *asp,
    QObject *parent)
{
}

PolygonConditionEditorController::~PolygonConditionEditorController()
{
}

bool PolygonConditionEditorController::eventFilter(QObject *watched, QEvent *event)
{
}

void PolygonConditionEditorController::setEnabled(bool on)
{
}

QDialog *PolygonConditionEditorController::getDialog() const
{
}

} // ns ui
} // ns analysis
