#include "analysis_ui.h"
#include "analysis.h"
#include "../mvme_context.h"

#include <QHBoxLayout>
#include <QTreeWidget>
#include <QScrollArea>
#include <QSplitter>

typedef QTreeWidgetItem TreeNode;

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
};

enum NodeType
{
    NodeType_Module = QTreeWidgetItem::UserType,
    NodeType_Source,
    NodeType_Operator,
    NodeType_Histo1D,
};

template<typename T>
TreeNode *makeNode(T *data, int type = QTreeWidgetItem::Type)
{
    auto ret = new TreeNode(type);
    ret->setData(0, DataRole_Pointer, QVariant::fromValue(static_cast<void *>(data)));
    return ret;
}

namespace analysis
{

inline TreeNode *makeModuleNode(ModuleConfig *mod)
{
    auto node = makeNode(mod, NodeType_Module);
    node->setText(0, mod->objectName());
    node->setIcon(0, QIcon(":/vme_module.png"));
    return node;
};

inline TreeNode *makeOperatorNode(OperatorInterface *op)
{
    auto node = makeNode(op, NodeType_Operator);
    node->setText(0, QString("%1 %2").arg(
            op->getDisplayName(),
            op->objectName()));
    node->setIcon(0, QIcon(":/data_filter.png"));
    return node;
};

inline TreeNode *makeHistoNode(Histo1DSink *op)
{
    auto node = makeNode(op, NodeType_Histo1D);
    node->setText(0, QString("%1 %2").arg(
            op->getDisplayName(),
            op->objectName()));
    node->setIcon(0, QIcon(":/hist1d.png"));
    return node;
};

struct DisplayLevelTrees
{
    QTreeWidget *operatorTree;
    QTreeWidget *displayTree;
};

struct AnalysisWidgetPrivate
{
    AnalysisWidget *q;
    MVMEContext *context;
    QVector<DisplayLevelTrees> levelTrees;

    void createView();
    DisplayLevelTrees createTrees(int eventIndex, int level);
    DisplayLevelTrees createSourceTrees(int eventIndex);
};

void AnalysisWidgetPrivate::createView()
{
    levelTrees.push_back(createTrees(0, 0));
    levelTrees.push_back(createTrees(0, 1));
    levelTrees.push_back(createTrees(0, 2));
    levelTrees.push_back(createTrees(0, 3));
}

DisplayLevelTrees AnalysisWidgetPrivate::createTrees(int eventIndex, int level)
{
    // Level 0: special case for data sources
    if (level == 0)
    {
        return createSourceTrees(eventIndex);
    }

    DisplayLevelTrees result = { new QTreeWidget, new QTreeWidget };

    auto headerItem = result.operatorTree->headerItem();
    headerItem->setText(0, QString(QSL("L%1 Processing")).arg(level));

    headerItem = result.displayTree->headerItem();
    headerItem->setText(0, QString(QSL("L%1 Data Display")).arg(level));

    // Build a list of operators for the current level

    auto analysis = context->getAnalysisNG();
    QVector<OperatorPtr> operators;

    for (auto opEntry: analysis->getOperators())
    {
        if (opEntry.eventIndex == eventIndex && opEntry.op->getMaximumInputRank() == level - 1)
        {
            operators.push_back(opEntry.op);
        }
    }

    // populate the OperatorTree
    for (auto op: operators)
    {
        auto histoSink = qobject_cast<Histo1DSink *>(op.get());
        if (!histoSink)
        {
            auto opNode = makeOperatorNode(op.get());
            result.operatorTree->addTopLevelItem(opNode);
        }
    }

    // populate the DisplayTree
    for (auto op: operators)
    {
        auto histoSink = qobject_cast<Histo1DSink *>(op.get());
        if (histoSink)
        {
            auto histoNode = makeHistoNode(histoSink);
            result.displayTree->addTopLevelItem(histoNode);
        }
    }

    return result;
}

DisplayLevelTrees AnalysisWidgetPrivate::createSourceTrees(int eventIndex)
{
    auto analysis = context->getAnalysisNG();
    auto vmeConfig = context->getDAQConfig();

    auto eventConfig = vmeConfig->getEventConfig(eventIndex);
    auto modules = eventConfig->getModuleConfigs();

    DisplayLevelTrees result = { new QTreeWidget, new QTreeWidget };

    auto headerItem = result.operatorTree->headerItem();
    headerItem->setText(0, QSL("Parameter Extraction"));

    headerItem = result.displayTree->headerItem();
    headerItem->setText(0, QSL("L0 Data Display"));

    // populate the OperatorTree
    int moduleIndex = 0;
    for (auto mod: modules)
    {
        auto moduleNode = makeModuleNode(mod);
        result.operatorTree->addTopLevelItem(moduleNode);

        for (auto sourceEntry: analysis->getSources(eventIndex, moduleIndex))
        {
            auto source = sourceEntry.source.get();
            auto sourceNode = makeNode(source, NodeType_Source);
            sourceNode->setIcon(0, QIcon(":/data_filter.png"));
            sourceNode->setText(0, source->objectName());
            moduleNode->addChild(sourceNode);
        }

        ++moduleIndex;
    }

    // populate the DisplayTree
    moduleIndex = 0;
    for (auto mod: modules)
    {
        auto moduleNode = makeModuleNode(mod);
        result.displayTree->addTopLevelItem(moduleNode);

        // TODO: find and add raw histos here (once they exist)!
    }

    return result;
}


AnalysisWidget::AnalysisWidget(MVMEContext *ctx, QWidget *parent)
    : QWidget(parent)
    , m_d(new AnalysisWidgetPrivate)
{
    setMinimumSize(1000, 600); // FIXME: find another way to make the window be sanely sized at startup
    m_d->q = this;
    m_d->context = ctx;

    auto outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    auto scrollArea = new QScrollArea;
    outerLayout->addWidget(scrollArea);

    auto scrollLayout = new QHBoxLayout(scrollArea);
    scrollLayout->setContentsMargins(0, 0, 0, 0);

    // row frames and splitter
    auto rowSplitter = new QSplitter(Qt::Vertical);
    scrollLayout->addWidget(rowSplitter);

    auto operatorFrame = new QFrame;
    auto operatorFrameLayout = new QHBoxLayout(operatorFrame);
    operatorFrameLayout->setContentsMargins(2, 2, 2, 2);
    rowSplitter->addWidget(operatorFrame);

    auto displayFrame = new QFrame;
    auto displayFrameLayout = new QHBoxLayout(displayFrame);
    displayFrameLayout->setContentsMargins(2, 2, 2, 2);
    rowSplitter->addWidget(displayFrame);

    // column frames and splitters
    auto operatorFrameColumnSplitter = new QSplitter;
    operatorFrameLayout->addWidget(operatorFrameColumnSplitter);

    auto displayFrameColumnSplitter = new QSplitter;
    displayFrameLayout->addWidget(displayFrameColumnSplitter);

    // column splitter sync
    connect(operatorFrameColumnSplitter, &QSplitter::splitterMoved,
            displayFrameColumnSplitter, [operatorFrameColumnSplitter, displayFrameColumnSplitter] (int pos, int index) {
                auto sizes = operatorFrameColumnSplitter->sizes();
                displayFrameColumnSplitter->setSizes(sizes);
    });

    connect(displayFrameColumnSplitter, &QSplitter::splitterMoved,
            operatorFrameColumnSplitter, [operatorFrameColumnSplitter, displayFrameColumnSplitter] (int pos, int index) {
                auto sizes = displayFrameColumnSplitter->sizes();
                operatorFrameColumnSplitter->setSizes(sizes);
    });

    m_d->createView();

    for (int levelIndex = 0;
         levelIndex < m_d->levelTrees.size();
         ++levelIndex)
    {
        operatorFrameColumnSplitter->addWidget(m_d->levelTrees[levelIndex].operatorTree);
        displayFrameColumnSplitter->addWidget(m_d->levelTrees[levelIndex].displayTree);
    }
}

AnalysisWidget::~AnalysisWidget()
{
    delete m_d;
}

}
