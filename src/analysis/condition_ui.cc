#include "condition_ui.h"

#include <QStackedWidget>

#include "gui_util.h"
#include "mvme_context.h"
#include "qt_util.h"
#include "treewidget_utils.h"


namespace analysis
{
namespace
{

enum NodeType
{
    NodeType_Condition,
    NodeType_ConditionBit,

    NodeType_MaxNodeType
};

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
    DataRole_BitIndex
};

class Node: public BasicTreeNode
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
Node *make_node(T *data, int type = QTreeWidgetItem::Type)
{
    auto ret = new Node(type);
    ret->setData(0, DataRole_Pointer, QVariant::fromValue(static_cast<void *>(data)));
    ret->setFlags(ret->flags() & ~(Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled));
    return ret;
}

Node *make_condition_node(ConditionInterface *cond)
{
    auto ret = make_node(cond, NodeType_Condition);

    ret->setData(0, Qt::EditRole, cond->objectName());
    ret->setData(0, Qt::DisplayRole, QString("<b>%1</b> %2").arg(
            cond->getShortName(),
            cond->objectName()));

    ret->setFlags(ret->flags() | Qt::ItemIsEditable);

    if (auto condi = qobject_cast<ConditionInterval *>(cond))
    {
        for (u32 bi = 0; bi < condi->getNumberOfConditionBits(); bi++)
        {
            auto child = make_node(condi, NodeType_ConditionBit);
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
    setColumnCount(2);
    headerItem()->setText(0, QSL("Name"));
    headerItem()->setText(1, QSL("Bit"));

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

    auto analysis = m_d->getAnalysis();
    auto conditions = analysis->getConditions(m_d->getEventId());

    qSort(conditions.begin(), conditions.end(), [](auto c1, auto c2) {
        return c1->objectName() < c2->objectName();
    });

    for (auto cond: conditions)
    {
        auto node = make_condition_node(cond.get());

        this->addTopLevelItem(node);
    }
}

void ConditionTreeWidget::doPeriodicUpdate()
{
}

//
// ConditionWidget
//

struct ConditionWidget::Private
{
    MVMEContext *getContext() const { return m_context; }
    Analysis *getAnalysis() const { return getContext()->getAnalysis(); }

    MVMEContext *m_context;
    QToolBar *m_toolbar;
    QStackedWidget *m_treeStack;
    QHash<QUuid, ConditionTreeWidget *> m_treesByEventId;
};

ConditionWidget::ConditionWidget(MVMEContext *ctx, QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<Private>())
{
    m_d->m_context = ctx;
    m_d->m_toolbar = make_toolbar();
    m_d->m_treeStack = new QStackedWidget;

    // populate the toolbar
    {
        auto tb = m_d->m_toolbar;
        auto actionApplyCondition = tb->addAction(QSL("Apply Condition"));
        //actionApplyCondition->setEnabled(false);
    }

    // layout
    auto layout = new QVBoxLayout(this);
    layout->addWidget(m_d->m_toolbar);
    layout->addWidget(m_d->m_treeStack);
    layout->setStretch(1, 1);

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

}
