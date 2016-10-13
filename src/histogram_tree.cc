#include "histogram_tree.h"
#include "histogram.h"
#include "hist2d.h"
#include "mvme_context.h"
#include "mvme_config.h"


#include <QTreeWidget>
#include <QHBoxLayout>
#include <QDebug>
#include <QMenu>
#include <QStyledItemDelegate>

enum NodeType
{
    NodeType_HistoCollection = QTreeWidgetItem::UserType,
    NodeType_Histo2D,
};

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
};

class TreeNode: public QTreeWidgetItem
{
    public:
        using QTreeWidgetItem::QTreeWidgetItem;
};

class NoEditDelegate: public QStyledItemDelegate
{
    public:
        NoEditDelegate(QObject* parent=0): QStyledItemDelegate(parent) {}
        virtual QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
            return 0;
        }
};

HistogramTreeWidget::HistogramTreeWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
    , m_tree(new QTreeWidget)
    , m_node1D(new TreeNode)
    , m_node2D(new TreeNode)
{
    m_tree->setColumnCount(2);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIndentation(10);
    m_tree->setItemDelegateForColumn(1, new NoEditDelegate(this));
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);

    auto headerItem = m_tree->headerItem();
    headerItem->setText(0, QSL("Object"));
    headerItem->setText(1, QSL("Info"));

    m_node1D->setText(0, QSL("1D"));
    m_node2D->setText(0, QSL("2D"));

    m_tree->addTopLevelItem(m_node1D);
    m_tree->addTopLevelItem(m_node2D);

    m_node1D->setExpanded(true);
    m_node2D->setExpanded(true);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, &HistogramTreeWidget::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &HistogramTreeWidget::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged, this, &HistogramTreeWidget::onItemChanged);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &HistogramTreeWidget::onItemExpanded);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &HistogramTreeWidget::treeContextMenu);

    connect(m_context, &MVMEContext::objectAdded, this, &HistogramTreeWidget::onObjectAdded);
    connect(m_context, &MVMEContext::objectAboutToBeRemoved, this, &HistogramTreeWidget::onObjectAboutToBeRemoved);
}

template<typename T>
TreeNode *makeNode(T *data, int type = QTreeWidgetItem::Type)
{
    auto ret = new TreeNode(type);
    ret->setData(0, DataRole_Pointer, Ptr2Var(data));
    return ret;
}

void HistogramTreeWidget::onObjectAdded(QObject *object)
{
    TreeNode *node = nullptr;
    TreeNode *parent = nullptr;

    if(qobject_cast<HistogramCollection *>(object))
    {
        node = makeNode(object, NodeType_HistoCollection);
        parent = m_node1D;
    }
    else if(qobject_cast<Hist2D *>(object))
    {
        node = makeNode(object, NodeType_HistoCollection);
        parent = m_node2D;
    }

    if (node && parent)
    {
        node->setText(0, object->objectName());
        m_treeMap[object] = node;
        parent->addChild(node);
    }
}

void HistogramTreeWidget::onObjectAboutToBeRemoved(QObject *object)
{
    delete m_treeMap.take(object);
}

void HistogramTreeWidget::onItemClicked(QTreeWidgetItem *item, int column)
{
    auto obj = Var2Ptr<QObject>(item->data(0, DataRole_Pointer));

    if (obj)
        emit objectClicked(obj);
}

void HistogramTreeWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    auto obj = Var2Ptr<QObject>(item->data(0, DataRole_Pointer));

    if (obj)
        emit objectDoubleClicked(obj);
}

void HistogramTreeWidget::onItemChanged(QTreeWidgetItem *item, int column)
{
}

void HistogramTreeWidget::onItemExpanded(QTreeWidgetItem *item)
{
    m_tree->resizeColumnToContents(0);
}

void HistogramTreeWidget::treeContextMenu(const QPoint &pos)
{
    auto node = m_tree->itemAt(pos);
    auto parent = node->parent();
    auto obj = Var2Ptr<QObject>(node->data(0, DataRole_Pointer));

    QMenu menu;

    if (node->type() == NodeType_HistoCollection
        || node->type() == NodeType_Histo2D)
    {
        menu.addAction(QSL("Open in new window"), this, [obj, this]() { emit openInNewWindow(obj); });
        menu.addAction(QSL("Clear"), this, &HistogramTreeWidget::clearHistogram);

        if (node->type() == NodeType_Histo2D)
            menu.addAction(QSL("Remove Histogram"), this, &HistogramTreeWidget::removeHistogram);
    }

    if (node == m_node2D && m_context->getConfig()->getAllModuleConfigs().size())
    {
        menu.addAction(QSL("Add 2D Histogram"), this, &HistogramTreeWidget::add2DHistogram);
    }

    if (!menu.isEmpty())
    {
        menu.exec(m_tree->mapToGlobal(pos));
    }
}

void HistogramTreeWidget::clearHistogram()
{
    auto node = m_tree->currentItem();
    {
        auto histo = Var2Ptr<HistogramCollection>(node->data(0, DataRole_Pointer));
        if (histo)
            histo->clearHistogram();
    }

    {
        auto histo = Var2Ptr<Hist2D>(node->data(0, DataRole_Pointer));
        if (histo)
            histo->clear();
    }
}

// XXX: leftoff

void HistogramTreeWidget::removeHistogram()
{
    auto node = m_tree->currentItem();
}

void HistogramTreeWidget::add2DHistogram()
{
    auto node = m_tree->currentItem();
}
