#include "condition_ui.h"

#include <QPushButton>
#include <QStackedWidget>

#include "analysis/analysis.h"
#include "analysis/analysis_util.h"
#include "analysis/ui_lib.h"
#include "gui_util.h"
#include "mvme_context.h"
#include "qt_util.h"
#include "treewidget_utils.h"

namespace analysis
{
namespace ui
{

using ConditionInterface = analysis::ConditionInterface;

namespace
{

enum NodeType
{
    NodeType_Condition,
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

TreeNode *make_condition_node(ConditionInterface *cond)
{
    auto ret = make_node(cond, NodeType_Condition, DataRole_AnalysisObject);

    ret->setData(0, Qt::EditRole, cond->objectName());
    ret->setData(0, Qt::DisplayRole, QString("<b>%1</b> %2").arg(
            cond->getShortName(),
            cond->objectName()));

    ret->setFlags(ret->flags() | Qt::ItemIsEditable);

    if (auto condi = qobject_cast<ConditionInterval *>(cond))
    {
        for (s32 bi = 0; bi < condi->getNumberOfBits(); bi++)
        {
            auto child = make_node(condi, NodeType_ConditionBit, DataRole_AnalysisObject);
            child->setData(0, DataRole_BitIndex, bi);
            child->setText(0, QString::number(bi));

            ret->addChild(child);
        }
    }

    return ret;
}

} // end anon namespace

//
// ConditionTreeWidget
//

struct ConditionTreeWidget::Private
{
    Analysis *getAnalysis() const { return m_context->getAnalysis(); }
    QUuid getEventId() const { return m_eventId; }
    int getEventIndex() const { return m_eventIndex; }

    MVMEContext *m_context;
    QUuid m_eventId;
    int m_eventIndex;
    QSet<void *> m_expandedObjects;
    ObjectToNode m_objectMap;
};

ConditionTreeWidget::ConditionTreeWidget(MVMEContext *ctx, const QUuid &eventId, int eventIndex,
                                         QWidget *parent)
    : QTreeWidget(parent)
    , m_d(std::make_unique<Private>())
{
    qDebug() << __PRETTY_FUNCTION__ << this;

    // Private setup
    m_d->m_context = ctx;
    m_d->m_eventId = eventId;
    m_d->m_eventIndex = eventIndex;

    // QTreeWidget settings
    setExpandsOnDoubleClick(false);
    setItemDelegate(new HtmlDelegate(this));
    //setDragEnabled(true);
    //viewport()->setAcceptDrops(true);
    //setDropIndicatorShown(true);
    //setDragDropMode(QAbstractItemView::DragDrop);

    // columns: name, condition bit value
    setColumnCount(1);
    headerItem()->setText(0, QSL("Name"));
    //headerItem()->setText(1, QSL("Bit"));



    // interactions

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


    // init
    repopulate();
}

ConditionTreeWidget::~ConditionTreeWidget()
{
    qDebug() << __PRETTY_FUNCTION__ << this;
}

void ConditionTreeWidget::repopulate()
{
    clear();
    m_d->m_objectMap.clear();

    auto analysis = m_d->getAnalysis();
    auto conditions = analysis->getConditions(m_d->getEventId());

    qSort(conditions.begin(), conditions.end(), [](auto c1, auto c2) {
        return c1->objectName() < c2->objectName();
    });

    for (auto cond: conditions)
    {
        auto node = make_condition_node(cond.get());
        addTopLevelItem(node);
        m_d->m_objectMap[cond] = node;
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
    qDebug() << __PRETTY_FUNCTION__ << cl.condition << cl.subIndex;

    clearHighlights();

    if (auto condNode = m_d->m_objectMap[cl.condition])
    {
        if (cl.condition->getNumberOfBits() == 1)
        {
            condNode->setBackground(0, InputNodeOfColor);
        }
        else if (0 <= cl.subIndex && cl.subIndex < condNode->childCount())
        {
            if (auto bitNode = condNode->child(cl.subIndex))
            {
                condNode->setBackground(0, ChildIsInputNodeOfColor);
                bitNode->setBackground(0, InputNodeOfColor);
            }
            else
            {
                InvalidCodePath;
            }
        }
    }
    else
    {
        InvalidCodePath;
    }
}

static void clear_highlights(QTreeWidgetItem *root)
{
    root->setBackground(0, QColor(0, 0, 0, 0));

    for (auto ci = 0; ci < root->childCount(); ci++)
    {
        clear_highlights(root->child(ci));
    }
}

void ConditionTreeWidget::clearHighlights()
{
    clear_highlights(invisibleRootItem());
}

//
// ConditionWidget
//

struct ConditionWidget::Private
{
    ConditionWidget *m_q;

    MVMEContext *m_context;
    QToolBar *m_toolbar;
    QStackedWidget *m_treeStack;
    QHash<QUuid, ConditionTreeWidget *> m_treesByEventId;

    QAction *m_actionApplyCondition;
    QPushButton *pb_applyConditionAccept;
    QPushButton *pb_applyConditionReject;

    MVMEContext *getContext() const { return m_context; }
    Analysis *getAnalysis() const { return getContext()->getAnalysis(); }

    explicit Private(ConditionWidget *q): m_q(q) {}
    void onNodeClicked(QTreeWidgetItem *node);
    void onActionApplyConditionClicked();
};

ConditionWidget::ConditionWidget(MVMEContext *ctx, QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<Private>(this))
{
    m_d->m_context = ctx;
    m_d->m_toolbar = make_toolbar();
    m_d->m_treeStack = new QStackedWidget;

    // populate the toolbar
    {
        auto tb = m_d->m_toolbar;

        m_d->m_actionApplyCondition = tb->addAction(
            QIcon(QSL(":/scissors_arrow.png")), QSL("Apply Condition"),
            this, [this] { m_d->onActionApplyConditionClicked(); });

        m_d->m_actionApplyCondition->setEnabled(false);
    }

    m_d->pb_applyConditionAccept = new QPushButton(QSL("Accept"));
    m_d->pb_applyConditionReject = new QPushButton(QSL("Cancel"));

    m_d->pb_applyConditionAccept->setVisible(false);
    m_d->pb_applyConditionReject->setVisible(false);

    connect(m_d->pb_applyConditionAccept, &QPushButton::clicked,
            this, [this] () {
                m_d->pb_applyConditionAccept->setVisible(false);
                m_d->pb_applyConditionReject->setVisible(false);
                emit applyConditionAccept();
            });

    connect(m_d->pb_applyConditionReject, &QPushButton::clicked,
            this, [this] () {
                m_d->pb_applyConditionAccept->setVisible(false);
                m_d->pb_applyConditionReject->setVisible(false);
                emit applyConditionReject();
            });

    auto buttonsLayout = new QHBoxLayout;
    buttonsLayout->addWidget(m_d->pb_applyConditionAccept);
    buttonsLayout->addWidget(m_d->pb_applyConditionReject);

    // layout
    auto layout = new QVBoxLayout(this);
    layout->addWidget(m_d->m_toolbar);
    layout->addLayout(buttonsLayout);
    layout->addWidget(m_d->m_treeStack);
    layout->setStretch(2, 1);
    layout->setContentsMargins(0, 0, 0, 0);

    setWindowTitle(QSL("Conditions/Cuts"));

    repopulate();
}

ConditionWidget::~ConditionWidget()
{
    qDebug() << __PRETTY_FUNCTION__ << this;
}


void ConditionWidget::repopulate()
{
    clear_stacked_widget(m_d->m_treeStack);
    m_d->m_treesByEventId.clear();

    auto eventConfigs = m_d->getContext()->getEventConfigs();

    for (s32 eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto eventId = eventConfig->getId();

        auto conditionTree = new ConditionTreeWidget(
            m_d->getContext(), eventId, eventIndex);

        m_d->m_treeStack->addWidget(conditionTree);
        m_d->m_treesByEventId[eventId] = conditionTree;

        // interactions
        connect(conditionTree, &QTreeWidget::itemClicked,
                this, [this] (QTreeWidgetItem *node) {
                    m_d->onNodeClicked(node);
                });
    }

    assert(m_d->m_treeStack->count() == eventConfigs.size());
}

void ConditionWidget::repopulate(int eventIndex)
{
    qDebug() << __PRETTY_FUNCTION__ << this << eventIndex;

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
    qDebug() << __PRETTY_FUNCTION__ << this << eventIndex << m_d->m_treeStack->count();

    if (0 <= eventIndex && eventIndex < m_d->m_treeStack->count())
    {
        m_d->m_treeStack->setCurrentIndex(eventIndex);
    }
}

void ConditionWidget::selectEventById(const QUuid &eventId)
{
    qDebug() << __PRETTY_FUNCTION__ << this << eventId << m_d->m_treeStack->count();

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

void ConditionWidget::Private::onNodeClicked(QTreeWidgetItem *node)
{
    switch (static_cast<NodeType>(node->type()))
    {
        case NodeType_Condition:
            if (auto cond = qobject_cast<ConditionInterface *>(get_qobject(node,
                                                                           DataRole_AnalysisObject)))
            {
                m_actionApplyCondition->setEnabled(cond->getNumberOfBits() == 1);
                emit m_q->objectSelected(cond->shared_from_this());

                if (cond->getNumberOfBits() == 1)
                {
                    auto condPtr = std::dynamic_pointer_cast<ConditionInterface>(
                        cond->shared_from_this());

                    emit m_q->conditionLinkSelected({ condPtr, 0 });
                }
                else
                {
                    emit m_q->applyConditionReject();
                }
            }
            else
            {
                InvalidCodePath;
            }

            break;

        case NodeType_ConditionBit:
            m_actionApplyCondition->setEnabled(true);

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
            m_actionApplyCondition->setEnabled(false);
            break;
    }

}

void ConditionWidget::Private::onActionApplyConditionClicked()
{
    ConditionInterface *cond = nullptr;
    int bitIndex = -1;

    if (auto tree = qobject_cast<QTreeWidget *>(m_treeStack->currentWidget()))
    {
        auto selectedNode = tree->currentItem();

        if (!selectedNode) return;

        switch (selectedNode->type())
        {
            case NodeType_Condition:
                if ((cond = qobject_cast<ConditionInterface *>(get_qobject(selectedNode,
                                                                           DataRole_AnalysisObject))))
                {
                    assert(cond->getNumberOfBits() == 1);
                    bitIndex = 0;
                }
                break;

            case NodeType_ConditionBit:
                {
                    auto parentNode = selectedNode->parent();

                    if (parentNode && parentNode->type() == NodeType_Condition)
                    {
                        if ((cond = qobject_cast<ConditionInterface *>(
                                    get_qobject(parentNode, DataRole_AnalysisObject))))
                        {
                            bitIndex = selectedNode->data(0, DataRole_BitIndex).toInt();
                        }
                    }
                }
                break;
        }
    }

    if (cond && bitIndex >= 0)
    {
        pb_applyConditionAccept->setVisible(true);
        pb_applyConditionReject->setVisible(true);

        auto condPtr = std::dynamic_pointer_cast<ConditionInterface>(cond->shared_from_this());

        assert(condPtr);

        emit m_q->applyConditionBegin({ condPtr, bitIndex });
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

} // ns ui
} // ns analysis
