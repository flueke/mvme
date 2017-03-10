#include "analysis_ui.h"
#include "analysis_ui_p.h"
#include "data_extraction_widget.h"

#include "../mvme_context.h"
#include "../histo1d_widget.h"
#include "../histo2d_widget.h"
#include "../treewidget_utils.h"
#include "../config_ui.h"

#include <QComboBox>
#include <QCursor>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

namespace analysis
{

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
    DataRole_ParameterIndex,
    DataRole_HistoAddress,
};

enum NodeType
{
    NodeType_Module = QTreeWidgetItem::UserType,
    NodeType_Source,
    NodeType_Operator,
    NodeType_OutputPipe,
    NodeType_OutputPipeParameter,

    NodeType_Histo1DSink,
    NodeType_Histo2DSink,
    NodeType_Sink,          // Sinks that are not handled specifically

    NodeType_Histo1D,

    NodeType_MaxNodeType
};

class TreeNode: public QTreeWidgetItem
{
    public:
        using QTreeWidgetItem::QTreeWidgetItem;

        // Custom sorting for numeric columns
        virtual bool operator<(const QTreeWidgetItem &other) const
        {
            if (type() == other.type() && treeWidget() && treeWidget()->sortColumn() == 0)
            {
                if (type() == NodeType_OutputPipeParameter)
                {
                    s32 thisAddress  = data(0, DataRole_ParameterIndex).toInt();
                    s32 otherAddress = other.data(0, DataRole_ParameterIndex).toInt();
                    return thisAddress < otherAddress;
                }
                else if (type() == NodeType_Histo1D)
                {
                    s32 thisAddress  = data(0, DataRole_HistoAddress).toInt();
                    s32 otherAddress = other.data(0, DataRole_HistoAddress).toInt();
                    return thisAddress < otherAddress;
                }
            }
            return QTreeWidgetItem::operator<(other);
        }
};

template<typename T>
TreeNode *makeNode(T *data, int type = QTreeWidgetItem::Type)
{
    auto ret = new TreeNode(type);
    ret->setData(0, DataRole_Pointer, QVariant::fromValue(static_cast<void *>(data)));
    return ret;
}

template<typename T>
T *getPointer(QTreeWidgetItem *node, s32 dataRole = DataRole_Pointer)
{
    return node ? reinterpret_cast<T *>(node->data(0, dataRole).value<void *>()) : nullptr;
}

inline QObject *getQObject(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
{
    return getPointer<QObject>(node, dataRole);
}

inline TreeNode *makeModuleNode(ModuleConfig *mod)
{
    auto node = makeNode(mod, NodeType_Module);
    node->setText(0, mod->objectName());
    node->setIcon(0, QIcon(":/vme_module.png"));
    return node;
};

inline TreeNode *makeOperatorTreeSourceNode(SourceInterface *source)
{
    auto sourceNode = makeNode(source, NodeType_Source);
    sourceNode->setText(0, source->objectName());
    sourceNode->setIcon(0, QIcon(":/data_filter.png"));

    Q_ASSERT(source->getNumberOfOutputs() == 1); // TODO: implement the case for multiple outputs

    if (source->getNumberOfOutputs() == 1)
    {
        Pipe *outputPipe = source->getOutput(0);
        s32 addressCount = outputPipe->parameters.size();

        for (s32 address = 0; address < addressCount; ++address)
        {
            auto addressNode = makeNode(outputPipe, NodeType_OutputPipeParameter);
            addressNode->setData(0, DataRole_ParameterIndex, address);
            addressNode->setText(0, QString::number(address));
            sourceNode->addChild(addressNode);
        }
    }

    return sourceNode;
}

inline TreeNode *makeDisplayTreeSourceNode(SourceInterface *source)
{
    auto sourceNode = makeNode(source, NodeType_Source);
    sourceNode->setText(0, source->objectName());
    sourceNode->setIcon(0, QIcon(":/data_filter.png"));

    return sourceNode;
}

inline TreeNode *makeHisto1DNode(Histo1DSink *sink)
{
    auto node = makeNode(sink, NodeType_Histo1DSink);
    node->setText(0, QString("%1 %2").arg(
            sink->getDisplayName(),
            sink->objectName()));
    node->setIcon(0, QIcon(":/hist1d.png"));

    if (sink->m_histos.size() > 0)
    {
        for (s32 addr = 0; addr < sink->m_histos.size(); ++addr)
        {
            auto histo = sink->m_histos[addr].get();
            auto histoNode = makeNode(histo, NodeType_Histo1D);
            histoNode->setData(0, DataRole_HistoAddress, addr);
            histoNode->setText(0, QString::number(addr));
            histoNode->setIcon(0, QIcon(":/hist1d.png"));

            node->addChild(histoNode);
        }
    }
    return node;
};

inline TreeNode *makeHisto2DNode(Histo2DSink *sink)
{
    auto node = makeNode(sink, NodeType_Histo2DSink);
    node->setText(0, QString("%1 %2").arg(
            sink->getDisplayName(),
            sink->objectName()));
    node->setIcon(0, QIcon(":/hist2d.png"));

    return node;
}

inline TreeNode *makeSinkNode(SinkInterface *sink)
{
    auto node = makeNode(sink, NodeType_Sink);
    node->setText(0, QString("%1 %2").arg(
            sink->getDisplayName(),
            sink->objectName()));
    node->setIcon(0, QIcon(":/sink.png"));

    return node;
}

inline TreeNode *makeOperatorNode(OperatorInterface *op)
{
    auto result = makeNode(op, NodeType_Operator);
    result->setText(0, QString("%1 %2").arg(
            op->getDisplayName(),
            op->objectName()));
    result->setIcon(0, QIcon(":/analysis_operator.png"));

    // outputs
    for (s32 outputIndex = 0;
         outputIndex < op->getNumberOfOutputs();
         ++outputIndex)
    {
        Pipe *outputPipe = op->getOutput(outputIndex);
        s32 outputParamSize = outputPipe->parameters.size();

        auto pipeNode = makeNode(outputPipe, NodeType_OutputPipe);
        pipeNode->setText(0, QString("#%1 \"%2\" (%3 elements)")
                          .arg(outputIndex)
                          .arg(op->getOutputName(outputIndex))
                          .arg(outputParamSize)
                         );
        result->addChild(pipeNode);

        for (s32 paramIndex = 0; paramIndex < outputParamSize; ++paramIndex)
        {
            auto paramNode = makeNode(outputPipe, NodeType_OutputPipeParameter);
            paramNode->setData(0, DataRole_ParameterIndex, paramIndex);
            paramNode->setText(0, QString("[%1]").arg(paramIndex));

            pipeNode->addChild(paramNode);
        }
    }

    return result;
};

struct Histo1DWidgetInfo
{
    QVector<std::shared_ptr<Histo1D>> histos;
    s32 histoAddress;
    std::shared_ptr<CalibrationMinMax> calib;
};

Histo1DWidgetInfo getHisto1DWidgetInfoFromNode(QTreeWidgetItem *node)
{
    QTreeWidgetItem *sinkNode = nullptr;
    Histo1DWidgetInfo result;

    switch (node->type())
    {
        case NodeType_Histo1D:
            {
                Q_ASSERT(node->parent() && node->parent()->type() == NodeType_Histo1DSink);
                sinkNode = node->parent();
                result.histoAddress = node->data(0, DataRole_HistoAddress).toInt();
            } break;

        case NodeType_Histo1DSink:
            {
                sinkNode = node;
                result.histoAddress = 0;
            } break;

        InvalidDefaultCase;
    }

    auto histoSink = getPointer<Histo1DSink>(sinkNode);
    result.histos = histoSink->m_histos;

    Q_ASSERT(histoSink->m_histos.size() && result.histoAddress < histoSink->m_histos.size());

    // Check if the histosinks input is a CalibrationMinMax
    if (Pipe *sinkInputPipe = histoSink->getSlot(0)->inputPipe)
    {
        if (auto calibRaw = qobject_cast<CalibrationMinMax *>(sinkInputPipe->getSource()))
        {
            result.calib = std::dynamic_pointer_cast<CalibrationMinMax>(calibRaw->getSharedPointer());
        }
    }

    return result;
}

struct DisplayLevelTrees
{
    QTreeWidget *operatorTree;
    QTreeWidget *displayTree;
    s32 userLevel;
};

using SetOfVoidStar = QSet<void *>;

struct EventWidgetPrivate
{
    enum Mode
    {
        Default,
        SelectInput
    };

    EventWidget *m_q;
    MVMEContext *m_context;
    QUuid m_eventId;
    // TODO: initialize eventIndex at creation time and use it instead of passing it around internally
    // TODO: or would it be better to use m_eventId instead?
    int m_eventIndex;
    AnalysisWidget *m_analysisWidget;

    QVector<DisplayLevelTrees> m_levelTrees;

    Mode m_mode;
    bool m_uniqueWidgetActive;
    Slot *m_selectInputSlot;
    s32 m_selectInputUserLevel;
    EventWidget::SelectInputCallback m_selectInputCallback;

    QSplitter *m_operatorFrameSplitter;
    QSplitter *m_displayFrameSplitter;

    enum TreeType
    {
        TreeType_Operator,
        TreeType_Display,
        TreeType_Count
    };
    // Keeps track of the expansion state of those tree nodes that are storing objects in DataRole_Pointer.
    // There's two sets, one for the operator trees and one for the display
    // trees, because objects may have nodes in both trees.
    std::array<SetOfVoidStar, TreeType_Count> m_expandedObjects;

    void createView(s32 eventIndex);
    DisplayLevelTrees createTrees(s32 eventIndex, s32 level);
    DisplayLevelTrees createSourceTrees(s32 eventIndex);
    void appendTreesToView(DisplayLevelTrees trees);
    void repopulate();

    void addUserLevel(s32 eventIndex);
    s32 getUserLevelForTree(QTreeWidget *tree);

    void doOperatorTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel);
    void doDisplayTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel);

    void modeChanged();
    void highlightValidInputNodes(QTreeWidgetItem *node);
    void highlightInputNodes(OperatorInterface *op);
    void highlightOutputNodes(PipeSourceInterface *ps);
    void clearToDefaultNodeHighlights(QTreeWidgetItem *node);
    void clearAllToDefaultNodeHighlights();
    void onNodeClicked(TreeNode *node, int column);
    void onNodeDoubleClicked(TreeNode *node, int column);
    void clearAllTreeSelections();
    void clearTreeSelectionsExcept(QTreeWidget *tree);
    void generateDefaultFilters(ModuleConfig *module);
};

// FIXME: the param should be eventId
void EventWidgetPrivate::createView(s32 eventIndex)
{
    auto analysis = m_context->getAnalysisNG();
    s32 maxUserLevel = 0;

    for (const auto &opEntry: analysis->getOperators(eventIndex))
    {
        maxUserLevel = std::max(maxUserLevel, opEntry.userLevel);
    }

    // +1 to make an empty display for the next level a user might want to use.
    ++maxUserLevel;

    for (s32 userLevel = 0; userLevel <= maxUserLevel; ++userLevel)
    {
        auto trees = createTrees(eventIndex, userLevel);
        m_levelTrees.push_back(trees);
    }
}

DisplayLevelTrees EventWidgetPrivate::createTrees(s32 eventIndex, s32 level)
{
    // Level 0: special case for data sources
    if (level == 0)
    {
        return createSourceTrees(eventIndex);
    }

    DisplayLevelTrees result = { new QTreeWidget, new QTreeWidget, level };
    auto headerItem = result.operatorTree->headerItem();
    headerItem->setText(0, QString(QSL("L%1 Processing")).arg(level));

    headerItem = result.displayTree->headerItem();
    headerItem->setText(0, QString(QSL("L%1 Data Display")).arg(level));

    result.operatorTree->setExpandsOnDoubleClick(false);
    result.displayTree->setExpandsOnDoubleClick(false);

    // Build a list of operators for the current level
    auto analysis = m_context->getAnalysisNG();
    QVector<Analysis::OperatorEntry> operators = analysis->getOperators(eventIndex, level);

    // Populate the OperatorTree
    for (auto entry: operators)
    {
        if(!qobject_cast<SinkInterface *>(entry.op.get()))
        {
            auto opNode = makeOperatorNode(entry.op.get());
            result.operatorTree->addTopLevelItem(opNode);
        }
    }
    result.operatorTree->sortItems(0, Qt::AscendingOrder);

    // Populate the DisplayTree
    {
        auto histo1DRoot = new TreeNode({QSL("1D")});
        auto histo2DRoot = new TreeNode({QSL("2D")});
        result.displayTree->addTopLevelItem(histo1DRoot);
        result.displayTree->addTopLevelItem(histo2DRoot);
        histo1DRoot->setExpanded(true);
        histo2DRoot->setExpanded(true);

        for (const auto &entry: operators)
        {
            if (auto histoSink = qobject_cast<Histo1DSink *>(entry.op.get()))
            {
                auto histoNode = makeHisto1DNode(histoSink);
                histo1DRoot->addChild(histoNode);
            }
            else if (auto histoSink = qobject_cast<Histo2DSink *>(entry.op.get()))
            {
                auto histoNode = makeHisto2DNode(histoSink);
                histo2DRoot->addChild(histoNode);
            }
            else if (auto sink = qobject_cast<SinkInterface *>(entry.op.get()))
            {
                auto sinkNode = makeSinkNode(sink);
                result.displayTree->addTopLevelItem(sinkNode);
            }
        }
    }
    result.displayTree->sortItems(0, Qt::AscendingOrder);

    return result;
}

DisplayLevelTrees EventWidgetPrivate::createSourceTrees(s32 eventIndex)
{
    auto analysis = m_context->getAnalysisNG();
    auto vmeConfig = m_context->getDAQConfig();

    auto eventConfig = vmeConfig->getEventConfig(eventIndex);
    auto modules = eventConfig->getModuleConfigs();

    DisplayLevelTrees result = { new QTreeWidget, new QTreeWidget, 0 };

    auto headerItem = result.operatorTree->headerItem();
    headerItem->setText(0, QSL("L0 Parameter Extraction"));

    headerItem = result.displayTree->headerItem();
    headerItem->setText(0, QSL("L0 Data Display"));

    result.operatorTree->setExpandsOnDoubleClick(false);
    result.displayTree->setExpandsOnDoubleClick(false);

    // Populate the OperatorTree
    int moduleIndex = 0;
    for (auto mod: modules)
    {
        auto moduleNode = makeModuleNode(mod);
        result.operatorTree->addTopLevelItem(moduleNode);
        moduleNode->setExpanded(true);

        for (auto sourceEntry: analysis->getSources(eventIndex, moduleIndex))
        {
            auto sourceNode = makeOperatorTreeSourceNode(sourceEntry.source.get());
            moduleNode->addChild(sourceNode);
        }
        ++moduleIndex;
    }
    result.operatorTree->sortItems(0, Qt::AscendingOrder);

    // Populate the DisplayTree
    // Create module nodes and nodes for the raw histograms for each data source for the module.
    QSet<QObject *> sinksAddedBelowModules;
    moduleIndex = 0;
    auto opEntries = analysis->getOperators(eventIndex, 0);

    for (auto mod: modules)
    {
        auto moduleNode = makeModuleNode(mod);
        result.displayTree->addTopLevelItem(moduleNode);
        moduleNode->setExpanded(true);

        for (auto sourceEntry: analysis->getSources(eventIndex, moduleIndex))
        {
            for (const auto &entry: opEntries)
            {
                auto histoSink = qobject_cast<Histo1DSink *>(entry.op.get());
                if (histoSink && (histoSink->getSlot(0)->inputPipe == sourceEntry.source->getOutput(0)))
                {
                    auto histoNode = makeHisto1DNode(histoSink);
                    moduleNode->addChild(histoNode);
                    sinksAddedBelowModules.insert(histoSink);
                }
            }
        }
        ++moduleIndex;
    }

    // This handles any "lost" display elements. E.g. raw histograms whose data
    // source has been deleted.
    for (auto &entry: opEntries)
    {
        if (auto histoSink = qobject_cast<Histo1DSink *>(entry.op.get()))
        {
            if (!sinksAddedBelowModules.contains(histoSink))
            {
                auto histoNode = makeHisto1DNode(histoSink);
                result.displayTree->addTopLevelItem(histoNode);
            }
        }
        else if (auto histoSink = qobject_cast<Histo2DSink *>(entry.op.get()))
        {
            if (!sinksAddedBelowModules.contains(histoSink))
            {
                auto histoNode = makeHisto2DNode(histoSink);
                result.displayTree->addTopLevelItem(histoNode);
            }
        }
        else if (auto sink = qobject_cast<SinkInterface *>(entry.op.get()))
        {
            if (!sinksAddedBelowModules.contains(sink))
            {
                auto sinkNode = makeSinkNode(sink);
                result.displayTree->addTopLevelItem(sinkNode);
            }
        }
    }

    result.displayTree->sortItems(0, Qt::AscendingOrder);

    return result;
}

static const s32 minTreeWidth = 200;
static const s32 minTreeHeight = 150;

void EventWidgetPrivate::appendTreesToView(DisplayLevelTrees trees)
{
    auto opTree   = trees.operatorTree;
    auto dispTree = trees.displayTree;
    s32 levelIndex = trees.userLevel;

    opTree->setMinimumWidth(minTreeWidth);
    opTree->setMinimumHeight(minTreeHeight);
    opTree->setContextMenuPolicy(Qt::CustomContextMenu);

    dispTree->setMinimumWidth(minTreeWidth);
    dispTree->setMinimumHeight(minTreeHeight);
    dispTree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_operatorFrameSplitter->addWidget(opTree);
    m_displayFrameSplitter->addWidget(dispTree);

    QObject::connect(opTree, &QWidget::customContextMenuRequested, m_q, [this, opTree, levelIndex] (QPoint pos) {
        doOperatorTreeContextMenu(opTree, pos, levelIndex);
    });

    QObject::connect(dispTree, &QWidget::customContextMenuRequested, m_q, [this, dispTree, levelIndex] (QPoint pos) {
        doDisplayTreeContextMenu(dispTree, pos, levelIndex);
    });

    for (auto tree: {opTree, dispTree})
    {
        tree->installEventFilter(m_q);


        QObject::connect(tree, &QTreeWidget::itemClicked, m_q, [this] (QTreeWidgetItem *node, int column) {
            onNodeClicked(reinterpret_cast<TreeNode *>(node), column);
        });

        QObject::connect(tree, &QTreeWidget::itemDoubleClicked, m_q, [this] (QTreeWidgetItem *node, int column) {
            onNodeDoubleClicked(reinterpret_cast<TreeNode *>(node), column);
        });


        QObject::connect(tree, &QTreeWidget::currentItemChanged, m_q,
                         [this, tree](QTreeWidgetItem *current, QTreeWidgetItem *previous) {
            if (current)
            {
                clearTreeSelectionsExcept(tree);
            }
        });

        TreeType treeType = (tree == opTree ? TreeType_Operator : TreeType_Display);

        QObject::connect(tree, &QTreeWidget::itemExpanded, m_q, [this, treeType] (QTreeWidgetItem *node) {
            if (void *voidObj = getPointer<void>(node))
            {
                qDebug() << voidObj << "was expanded";
                m_expandedObjects[treeType].insert(voidObj);
            }
        });

        QObject::connect(tree, &QTreeWidget::itemCollapsed, m_q, [this, treeType] (QTreeWidgetItem *node) {
            if (void *voidObj = getPointer<void>(node))
            {
                qDebug() << voidObj << "was collapsed";
                m_expandedObjects[treeType].remove(voidObj);
            }
        });
    }
}

static void expandObjectNodes(QTreeWidgetItem *node, const SetOfVoidStar &objectsToExpand)
{
    s32 childCount = node->childCount();

    for (s32 childIndex = 0;
         childIndex < childCount;
         ++childIndex)
    {
        auto childNode = node->child(childIndex);
        expandObjectNodes(childNode, objectsToExpand);
    }

    void *voidObj = getPointer<void>(node);

    if (voidObj && objectsToExpand.contains(voidObj))
    {
        node->setExpanded(true);
    }
}

template<typename T>
static void expandObjectNodes(const QVector<DisplayLevelTrees> &treeVector, const T &objectsToExpand)
{
    for (auto trees: treeVector)
    {
        expandObjectNodes(trees.operatorTree->invisibleRootItem(), objectsToExpand[EventWidgetPrivate::TreeType_Operator]);
        expandObjectNodes(trees.displayTree->invisibleRootItem(), objectsToExpand[EventWidgetPrivate::TreeType_Display]);
    }
}

void EventWidgetPrivate::repopulate()
{
    auto splitterSizes = m_operatorFrameSplitter->sizes();
    // clear
#if 0
    for (auto trees: m_levelTrees)
    {
        // FIXME: this is done because setParent(nullptr) below will cause a
        // focus in event on one of the other trees and that will call
        // EventWidget::eventFilter() which will call setCurrentItem() on
        // whatever tree gained focus which will emit currentItemChanged()
        // which will invoke a lambda which will call onNodeClicked() which
        // will call clearAllToDefaultNodeHighlights() which will try to figure
        // out if any operator is missing any inputs which will dereference the
        // operator which might have just been deleted via the context menu.
        // This is complicated and not great. Is it generally dangerous to have
        // currentItemChanged() call onNodeClicked()? Is there some other way
        // to stop the call chain earlier?
        trees.operatorTree->removeEventFilter(m_q);
        trees.displayTree->removeEventFilter(m_q);
    }
#endif

    for (auto trees: m_levelTrees)
    {
        trees.operatorTree->setParent(nullptr);
        trees.operatorTree->deleteLater();

        trees.displayTree->setParent(nullptr);
        trees.displayTree->deleteLater();
    }
    m_levelTrees.clear();
    Q_ASSERT(m_operatorFrameSplitter->count() == 0);
    Q_ASSERT(m_displayFrameSplitter->count() == 0);

    // populate
    if (m_eventIndex >= 0)
    {
        // This populates m_d->m_levelTrees
        createView(m_eventIndex);
    }

    for (auto trees: m_levelTrees)
    {
        // This populates the operator and display splitters
        appendTreesToView(trees);
    }

    if (splitterSizes.size() == m_operatorFrameSplitter->count())
    {
        // Restore the splitter sizes. As the splitters are synced via
        // splitterMoved() they both had the same sizes before.
        m_operatorFrameSplitter->setSizes(splitterSizes);
        m_displayFrameSplitter->setSizes(splitterSizes);
    }

    expandObjectNodes(m_levelTrees, m_expandedObjects);
    clearAllToDefaultNodeHighlights();
}

void EventWidgetPrivate::addUserLevel(s32 eventIndex)
{
    s32 levelIndex = m_levelTrees.size();
    auto trees = createTrees(eventIndex, levelIndex);
    m_levelTrees.push_back(trees);
    appendTreesToView(trees);
}

s32 EventWidgetPrivate::getUserLevelForTree(QTreeWidget *tree)
{
    for (s32 userLevel = 0;
         userLevel < m_levelTrees.size();
         ++userLevel)
    {
        auto trees = m_levelTrees[userLevel];
        if (tree == trees.operatorTree || tree == trees.displayTree)
        {
            return userLevel;
        }
    }

    return -1;
}

void EventWidgetPrivate::doOperatorTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel)
{
    auto node = tree->itemAt(pos);
    auto obj  = getQObject(node);

    QMenu menu;
    auto menuNew = new QMenu;
    bool actionNewIsFirst = false;

    if (node)
    {
        if (userLevel == 0 && node->type() == NodeType_Module)
        {
            if (!m_uniqueWidgetActive)
            {
                auto moduleConfig = getPointer<ModuleConfig>(node);

                // new sources
                auto add_action = [this, &menu, menuNew, moduleConfig](const QString &title, auto srcPtr)
                {
                    menuNew->addAction(title, &menu, [this, moduleConfig, srcPtr]() {
                        auto widget = new AddEditSourceWidget(srcPtr, moduleConfig, m_q);
                        widget->move(QCursor::pos());
                        widget->setAttribute(Qt::WA_DeleteOnClose);
                        widget->show();
                        m_uniqueWidgetActive = true;
                        clearAllTreeSelections();
                        clearAllToDefaultNodeHighlights();
                    });
                };

                auto analysis = m_context->getAnalysisNG();
                auto &registry(analysis->getRegistry());

                for (auto sourceName: registry.getSourceNames())
                {
                    SourcePtr src(registry.makeSource(sourceName));
                    add_action(src->getDisplayName(), src);
                }

                // default data filters and "raw display" creation
                if (moduleConfig && (defaultDataFilters.contains(moduleConfig->type)
                                     || defaultDualWordFilters.contains(moduleConfig->type)))
                {
                    menu.addAction(QSL("Generate default filters"), [this, moduleConfig] () {
                        generateDefaultFilters(moduleConfig);
                    });
                }
                actionNewIsFirst = true;
            }
        }

        if (userLevel == 0 && node->type() == NodeType_Source)
        {
            auto sourceInterface = getPointer<SourceInterface>(node);

            if (sourceInterface)
            {
                Q_ASSERT(sourceInterface->getNumberOfOutputs() == 1); // TODO: implement the case for multiple outputs
                auto pipe = sourceInterface->getOutput(0);

                menu.addAction(QSL("Show Parameters"), [this, pipe]() {
                    auto widget = new PipeDisplay(pipe, m_q);
                    widget->move(QCursor::pos());
                    widget->setAttribute(Qt::WA_DeleteOnClose);
                    widget->show();
                });

                auto moduleNode = node->parent();
                Q_ASSERT(moduleNode && moduleNode->type() == NodeType_Module);

                auto moduleConfig = getPointer<ModuleConfig>(moduleNode);

                if (!m_uniqueWidgetActive)
                {
                    if (moduleConfig)
                    {
                        menu.addAction(QSL("Edit"), [this, sourceInterface, moduleConfig]() {
                            auto widget = new AddEditSourceWidget(sourceInterface, moduleConfig, m_q);
                            widget->move(QCursor::pos());
                            widget->setAttribute(Qt::WA_DeleteOnClose);
                            widget->show();
                            m_uniqueWidgetActive = true;
                            clearAllTreeSelections();
                            clearAllToDefaultNodeHighlights();
                        });
                    }

                    menu.addAction(QSL("Remove"), [this, sourceInterface]() {
                        // TODO: QMessageBox::question or similar
                        m_q->removeSource(sourceInterface);
                    });
                }
            }
        }

        if (userLevel > 0 && node->type() == NodeType_OutputPipe)
        {
            auto pipe = getPointer<Pipe>(node);

            menu.addAction(QSL("Show Parameters"), [this, pipe]() {
                auto widget = new PipeDisplay(pipe, m_q);
                widget->move(QCursor::pos());
                widget->setAttribute(Qt::WA_DeleteOnClose);
                widget->show();
            });
        }

        if (userLevel > 0 && node->type() == NodeType_Operator)
        {
            if (!m_uniqueWidgetActive)
            {
                auto op = getPointer<OperatorInterface>(node);
                Q_ASSERT(op);
                menu.addAction(QSL("Edit"), [this, userLevel, op]() {
                    auto widget = new AddEditOperatorWidget(op, userLevel, m_q);
                    widget->move(QCursor::pos());
                    widget->setAttribute(Qt::WA_DeleteOnClose);
                    widget->show();
                    m_uniqueWidgetActive = true;
                    clearAllTreeSelections();
                    clearAllToDefaultNodeHighlights();
                });

                menu.addAction(QSL("Remove"), [this, op]() {
                    // TODO: QMessageBox::question or similar
                    m_q->removeOperator(op);
                });
            }
        }
    }
    else // No node selected
    {
        if (m_mode == EventWidgetPrivate::Default && !m_uniqueWidgetActive)
        {
            if (userLevel > 0)
            {
                auto add_action = [this, &menu, menuNew, userLevel](const QString &title, auto opPtr)
                {
                    menuNew->addAction(title, &menu, [this, userLevel, opPtr]() {
                        auto widget = new AddEditOperatorWidget(opPtr, userLevel, m_q);
                        widget->move(QCursor::pos());
                        widget->setAttribute(Qt::WA_DeleteOnClose);
                        widget->show();
                        m_uniqueWidgetActive = true;
                        clearAllTreeSelections();
                        clearAllToDefaultNodeHighlights();
                    });
                };

                auto analysis = m_context->getAnalysisNG();
                auto &registry(analysis->getRegistry());

                for (auto operatorName: registry.getOperatorNames())
                {
                    OperatorPtr op(registry.makeOperator(operatorName));
                    add_action(op->getDisplayName(), op);
                }
            }
        }
    }

    if (menuNew->isEmpty())
    {
        delete menuNew;
    }
    else
    {
        auto actionNew = menu.addAction(QSL("New"));
        actionNew->setMenu(menuNew);
        QAction *before = nullptr;
        if (actionNewIsFirst)
        {
            before = menu.actions().value(0);
        }
        menu.insertAction(before, actionNew);
    }

    if (!menu.isEmpty())
    {
        menu.exec(tree->mapToGlobal(pos));
    }
}

void EventWidgetPrivate::doDisplayTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel)
{
    Q_ASSERT(userLevel >= 0 && userLevel < m_levelTrees.size());

    auto node = tree->itemAt(pos);
    auto obj  = getQObject(node);

    QMenu menu;
    auto menuNew = new QMenu;

    auto add_action = [this, &menu, menuNew, userLevel](const QString &title, auto op)
    {
        menuNew->addAction(title, &menu, [this, userLevel, op]() {
            auto widget = new AddEditOperatorWidget(op, userLevel, m_q);
            widget->move(QCursor::pos());
            widget->setAttribute(Qt::WA_DeleteOnClose);
            widget->show();
            m_uniqueWidgetActive = true;
            clearAllTreeSelections();
            clearAllToDefaultNodeHighlights();
        });
    };

    if (node)
    {
        switch (node->type())
        {
            case NodeType_Histo1D:
                {
                    Histo1DWidgetInfo widgetInfo = getHisto1DWidgetInfoFromNode(node);

                    menu.addAction(QSL("Open"), m_q, [this, widgetInfo]() {
                        auto widget = new Histo1DWidget(widgetInfo.histos[widgetInfo.histoAddress]);
                        if (widgetInfo.calib)
                        {
                            widget->setCalibrationInfo(widgetInfo.calib, widgetInfo.histoAddress, m_context);
                        }
                        m_context->addWidgetWindow(widget);
                    });
                } break;

            case NodeType_Histo1DSink:
                {
                    Histo1DWidgetInfo widgetInfo = getHisto1DWidgetInfoFromNode(node);

                    menu.addAction(QSL("Open"), m_q, [this, widgetInfo]() {
                        auto listWidget = new Histo1DListWidget(widgetInfo.histos);
                        if (widgetInfo.calib)
                        {
                            listWidget->setCalibration(widgetInfo.calib, m_context);
                        }
                        m_context->addWidgetWindow(listWidget);
                    });
                } break;

            case NodeType_Histo2DSink:
                {
                    if (auto histoSink = qobject_cast<Histo2DSink *>(obj))
                    {
                        auto histo = histoSink->m_histo.get();
                        if (histo)
                        {
                            menu.addAction(QSL("Open"), m_q, [this, histo]() {
                                m_context->openInNewWindow(histo);
                            });
                        }
                    }
                } break;

            case NodeType_Module:
                {
                    auto sink = std::make_shared<Histo1DSink>();
                    add_action(sink->getDisplayName(), sink);
                } break;
        }

        if (auto op = qobject_cast<OperatorInterface *>(obj))
        {
            if (!m_uniqueWidgetActive)
            {
                menu.addAction(QSL("Edit"), [this, userLevel, op]() {
                    auto widget = new AddEditOperatorWidget(op, userLevel, m_q);
                    widget->move(QCursor::pos());
                    widget->setAttribute(Qt::WA_DeleteOnClose);
                    widget->show();
                    m_uniqueWidgetActive = true;
                    clearAllTreeSelections();
                    clearAllToDefaultNodeHighlights();
                });

                menu.addAction(QSL("Remove"), [this, op]() {
                    // TODO: QMessageBox::question or similar
                    m_q->removeOperator(op);
                });
            }
        }

        if (userLevel > 0 && !m_uniqueWidgetActive)
        {
            auto displayTree = m_levelTrees[userLevel].displayTree;
            Q_ASSERT(displayTree->topLevelItemCount() >= 2);
            auto histo1DRoot = displayTree->topLevelItem(0);
            auto histo2DRoot = displayTree->topLevelItem(1);

            if (node == histo1DRoot)
            {
                auto sink = std::make_shared<Histo1DSink>();
                add_action(sink->getDisplayName(), sink);
            }

            if (node == histo2DRoot)
            {
                auto sink = std::make_shared<Histo2DSink>();
                add_action(sink->getDisplayName(), sink);
            }

        }
    }
    else
    {
        if (m_mode == EventWidgetPrivate::Default && !m_uniqueWidgetActive)
        {
            if (userLevel == 0)
            {
                auto sink = std::make_shared<Histo1DSink>();
                add_action(sink->getDisplayName(), sink);
            }
            else
            {
                auto analysis = m_context->getAnalysisNG();
                auto &registry(analysis->getRegistry());

                for (auto sinkName: registry.getSinkNames())
                {
                    OperatorPtr sink(registry.makeSink(sinkName));
                    add_action(sink->getDisplayName(), sink);
                }
            }
        }
    }

    if (menuNew->isEmpty())
    {
        delete menuNew;
    }
    else
    {
        auto actionNew = menu.addAction(QSL("New"));
        actionNew->setMenu(menuNew);
        menu.addAction(actionNew);
    }

    if (!menu.isEmpty())
    {
        menu.exec(tree->mapToGlobal(pos));
    }
}

void EventWidgetPrivate::modeChanged()
{
    switch (m_mode)
    {
        case Default:
            {
                /* The previous mode was SelectInput so m_selectInputUserLevel
                 * must still be valid */
                Q_ASSERT(m_selectInputUserLevel < m_levelTrees.size());

                for (s32 userLevel = 0; userLevel <= m_selectInputUserLevel; ++userLevel)
                {
                    auto opTree = m_levelTrees[userLevel].operatorTree;
                    clearToDefaultNodeHighlights(opTree->invisibleRootItem());
                }
            } break;

        case SelectInput:
            // highlight valid sources
            {
                clearAllTreeSelections();


                Q_ASSERT(m_selectInputUserLevel < m_levelTrees.size());

                for (s32 userLevel = 0; userLevel <= m_selectInputUserLevel; ++userLevel)
                {
                    auto opTree = m_levelTrees[userLevel].operatorTree;
                    highlightValidInputNodes(opTree->invisibleRootItem());
                }
            } break;
    }
}

static bool isValidInputNode(QTreeWidgetItem *node, Slot *slot)
{
    PipeSourceInterface *dstObject = slot->parentOperator;
    Q_ASSERT(dstObject);

    PipeSourceInterface *srcObject = nullptr;

    switch (node->type())
    {
        case NodeType_Operator:
            {
                srcObject = getPointer<PipeSourceInterface>(node);
                Q_ASSERT(srcObject);
            } break;
        case NodeType_OutputPipe:
        case NodeType_OutputPipeParameter:
            {
                auto pipe = getPointer<Pipe>(node);
                srcObject = pipe->source;
                Q_ASSERT(srcObject);
            } break;
    }

    bool result = false;

    if (srcObject == dstObject)
    {
        // do not allow self-connections! :)
        result = false;
    }
    else if ((slot->acceptedInputTypes & InputType::Array)
        && (node->type() == NodeType_Operator || node->type() == NodeType_Source))
    {
        // Highlight operator and source nodes only if they have exactly a
        // single output.
        PipeSourceInterface *pipeSource = getPointer<PipeSourceInterface>(node);
        if (pipeSource->getNumberOfOutputs() == 1)
        {
            result = true;
        }
    }
    else if ((slot->acceptedInputTypes & InputType::Array)
             && node->type() == NodeType_OutputPipe)
    {
        result = true;
    }
    else if ((slot->acceptedInputTypes & InputType::Value)
             && node->type() == NodeType_OutputPipeParameter)
    {
        result = true;
    }

    return result;
}

static const QColor ValidInputNodeColor         = QColor("lightgreen");
static const QColor InputNodeOfColor            = QColor(0x90, 0xEE, 0x90, 255.0/3); // lightgreen but with some alpha
static const QColor ChildIsInputNodeOfColor     = QColor(0x90, 0xEE, 0x90, 255.0/6);

static const QColor OutputNodeOfColor           = QColor(0x00, 0x00, 0xCD, 255.0/3); // mediumblue with some alpha
static const QColor ChildIsOutputNodeOfColor    = QColor(0x00, 0x00, 0xCD, 255.0/6);

static const QColor MissingInputColor           = QColor(0xB2, 0x22, 0x22, 255.0/3); // firebrick with some alpha

void EventWidgetPrivate::highlightValidInputNodes(QTreeWidgetItem *node)
{
    if (isValidInputNode(node, m_selectInputSlot))
    {
        node->setBackground(0, ValidInputNodeColor);
    }

    for (s32 childIndex = 0; childIndex < node->childCount(); ++childIndex)
    {
        // recurse
        auto child = node->child(childIndex);
        highlightValidInputNodes(child);
    }
}

static bool isSourceNodeOf(QTreeWidgetItem *node, Slot *slot)
{
    PipeSourceInterface *srcObject = nullptr;

    switch (node->type())
    {
        case NodeType_Source:
        case NodeType_Operator:
            {
                srcObject = getPointer<PipeSourceInterface>(node);
                Q_ASSERT(srcObject);
            } break;

        case NodeType_OutputPipe:
        case NodeType_OutputPipeParameter:
            {
                auto pipe = getPointer<Pipe>(node);
                srcObject = pipe->source;
                Q_ASSERT(srcObject);
            } break;
    }

    bool result = false;

    if (slot->inputPipe->source == srcObject)
    {
        if (slot->paramIndex == Slot::NoParamIndex && node->type() != NodeType_OutputPipeParameter)
        {
            result = true;
        }
        else if (slot->paramIndex != Slot::NoParamIndex && node->type() == NodeType_OutputPipeParameter)
        {
            s32 nodeParamAddress = node->data(0, DataRole_ParameterIndex).toInt();
            result = (nodeParamAddress == slot->paramIndex);
        }
    }

    return result;
}

static bool isOutputNodeOf(QTreeWidgetItem *node, PipeSourceInterface *ps)
{
    OperatorInterface *dstObject = nullptr;

    switch (node->type())
    {
        case NodeType_Operator:
        case NodeType_Histo1DSink:
        case NodeType_Histo2DSink:
        case NodeType_Sink:
            {
                dstObject = getPointer<OperatorInterface>(node);
                Q_ASSERT(dstObject);
            } break;
    }

    bool result = false;

    if (dstObject)
    {
        for (s32 slotIndex = 0; slotIndex < dstObject->getNumberOfSlots(); ++slotIndex)
        {
            Slot *slot = dstObject->getSlot(slotIndex);

            if (slot->inputPipe)
            {
                for (s32 outputIndex = 0; outputIndex < ps->getNumberOfOutputs(); ++outputIndex)
                {
                    Pipe *pipe = ps->getOutput(outputIndex);
                    if (slot->inputPipe == pipe)
                    {
                        result = true;
                        break;
                    }
                }
            }
        }
    }

    return result;
}

// Returns true if this node or any of its children represent an input of the
// given operator.
static bool highlightInputNodes(OperatorInterface *op, QTreeWidgetItem *node)
{
    bool result = false;

    for (s32 childIndex = 0; childIndex < node->childCount(); ++childIndex)
    {
        // recurse
        auto child = node->child(childIndex);
        result = highlightInputNodes(op, child) || result;
    }

    if (result)
    {
        node->setBackground(0, ChildIsInputNodeOfColor);
    }

    for (s32 slotIndex = 0; slotIndex < op->getNumberOfSlots(); ++slotIndex)
    {
        Slot *slot = op->getSlot(slotIndex);
        if (slot->inputPipe && isSourceNodeOf(node, slot))
        {
            node->setBackground(0, InputNodeOfColor);
            result = true;
        }
    }

    return result;
}

// Returns true if this node or any of its children are connected to an output of the
// given pipe source.
static bool highlightOutputNodes(PipeSourceInterface *ps, QTreeWidgetItem *node)
{
    bool result = false;

    for (s32 childIndex = 0; childIndex < node->childCount(); ++childIndex)
    {
        // recurse
        auto child = node->child(childIndex);
        result = highlightOutputNodes(ps, child) || result;
    }

    if (result)
    {
        node->setBackground(0, ChildIsOutputNodeOfColor);
    }

    if (isOutputNodeOf(node, ps))
    {
        node->setBackground(0, OutputNodeOfColor);
        result = true;
    }

    return result;
}

void EventWidgetPrivate::highlightInputNodes(OperatorInterface *op)
{
    for (auto trees: m_levelTrees)
    {
        // Without the namespace prefix the compiler can't find
        // highlightInputNodes(). C++ surprises me again and again...
        analysis::highlightInputNodes(op, trees.operatorTree->invisibleRootItem());
    }
}

void EventWidgetPrivate::highlightOutputNodes(PipeSourceInterface *ps)
{
    for (auto trees: m_levelTrees)
    {
        analysis::highlightOutputNodes(ps, trees.operatorTree->invisibleRootItem());
        analysis::highlightOutputNodes(ps, trees.displayTree->invisibleRootItem());
    }
}

void EventWidgetPrivate::clearToDefaultNodeHighlights(QTreeWidgetItem *node)
{
    node->setBackground(0, QBrush());

    for (s32 childIndex = 0; childIndex < node->childCount(); ++childIndex)
    {
        // recurse
        auto child = node->child(childIndex);
        clearToDefaultNodeHighlights(child);
    }

    switch (node->type())
    {
        case NodeType_Operator:
        case NodeType_Histo1DSink:
        case NodeType_Histo2DSink:
            {
                auto op = getPointer<OperatorInterface>(node);
                for (auto slotIndex = 0; slotIndex < op->getNumberOfSlots(); ++slotIndex)
                {
                    Slot *slot = op->getSlot(slotIndex);
                    if (!slot->inputPipe)
                    {
                        node->setBackground(0, MissingInputColor);
                        break;
                    }
                }
            } break;
    }
}

void EventWidgetPrivate::clearAllToDefaultNodeHighlights()
{
    for (auto trees: m_levelTrees)
    {
        clearToDefaultNodeHighlights(trees.operatorTree->invisibleRootItem());
        clearToDefaultNodeHighlights(trees.displayTree->invisibleRootItem());
    }
}

void EventWidgetPrivate::onNodeClicked(TreeNode *node, int column)
{
    switch (m_mode)
    {
        case Default:
            {
                clearAllToDefaultNodeHighlights();

                switch (node->type())
                {
                    case NodeType_Operator:
                    case NodeType_Histo1DSink:
                    case NodeType_Histo2DSink:
                    case NodeType_Sink:
                        {
                            auto op = getPointer<OperatorInterface>(node);
                            highlightInputNodes(op);
                        } break;
                }

                switch (node->type())
                {
                    case NodeType_Source:
                    case NodeType_Operator:
                        {
                            auto ps = getPointer<PipeSourceInterface>(node);
                            highlightOutputNodes(ps);
                        } break;
                }
            } break;

        case SelectInput:
            {
                if (isValidInputNode(node, m_selectInputSlot)
                    && getUserLevelForTree(node->treeWidget()) <= m_selectInputUserLevel)
                {
                    Slot *slot = m_selectInputSlot;
                    Q_ASSERT(slot);
                    AnalysisPauser pauser(m_context);
                    // connect the slot with the selected input source
                    switch (node->type())
                    {
                        case NodeType_Source:
                        case NodeType_Operator:
                            {
                                Q_ASSERT(slot->acceptedInputTypes & InputType::Array);

                                PipeSourceInterface *source = getPointer<PipeSourceInterface>(node);
                                slot->connectPipe(source->getOutput(0), Slot::NoParamIndex);
                            } break;

                        case NodeType_OutputPipe:
                            {
                                Q_ASSERT(slot->acceptedInputTypes & InputType::Array);
                                Q_ASSERT(slot->parentOperator);

                                Pipe *pipe = getPointer<Pipe>(node);
                                slot->connectPipe(pipe, Slot::NoParamIndex);
                            } break;

                        case NodeType_OutputPipeParameter:
                            {
                                Q_ASSERT(slot->acceptedInputTypes & InputType::Value);

                                Pipe *pipe = getPointer<Pipe>(node);
                                s32 paramIndex = node->data(0, DataRole_ParameterIndex).toInt();
                                slot->connectPipe(pipe, paramIndex);
                            } break;

                        InvalidDefaultCase;
                    }

                    // tell the widget that initiated the select that we're done
                    if (m_selectInputCallback)
                    {
                        m_selectInputCallback();
                    }

                    // leave SelectInput mode
                    m_mode = Default;
                    m_selectInputCallback = nullptr;
                    modeChanged();
                }
            } break;
    }
}

void EventWidgetPrivate::onNodeDoubleClicked(TreeNode *node, int column)
{
    if (m_mode == Default)
    {
        switch (node->type())
        {
            // TODO: do not create duplicate widgets here. instead raise existing ones
            case NodeType_Histo1D:
                {
                    Histo1DWidgetInfo widgetInfo = getHisto1DWidgetInfoFromNode(node);
                    auto widget = new Histo1DWidget(widgetInfo.histos[widgetInfo.histoAddress]);
                    if (widgetInfo.calib)
                    {
                        widget->setCalibrationInfo(widgetInfo.calib, widgetInfo.histoAddress, m_context);
                    }
                    m_context->addWidgetWindow(widget);
                } break;

            case NodeType_Histo1DSink:
                {
                    Histo1DWidgetInfo widgetInfo = getHisto1DWidgetInfoFromNode(node);
                    auto listWidget = new Histo1DListWidget(widgetInfo.histos);
                    if (widgetInfo.calib)
                    {
                        listWidget->setCalibration(widgetInfo.calib, m_context);
                    }
                    m_context->addWidgetWindow(listWidget);
                } break;

            case NodeType_Histo2DSink:
                {
                    auto histoSink = getPointer<Histo2DSink>(node);
                    auto histoPtr = histoSink->m_histo;
                    auto widget = new Histo2DWidget(histoPtr);
                    m_context->addWidgetWindow(widget);
                } break;
        }
    }
}

void EventWidgetPrivate::clearAllTreeSelections()
{
    for (DisplayLevelTrees trees: m_levelTrees)
    {
        for (auto tree: {trees.operatorTree, trees.displayTree})
        {
            tree->setCurrentItem(nullptr);
        }
    }
}

void EventWidgetPrivate::clearTreeSelectionsExcept(QTreeWidget *treeNotToClear)
{
    for (DisplayLevelTrees trees: m_levelTrees)
    {
        for (auto tree: {trees.operatorTree, trees.displayTree})
        {
            if (tree != treeNotToClear)
            {
                tree->setCurrentItem(nullptr);
            }
        }
    }
}

void EventWidgetPrivate::generateDefaultFilters(ModuleConfig *module)
{
    auto indices = m_context->getDAQConfig()->getEventAndModuleIndices(module);
    s32 eventIndex = indices.first;
    s32 moduleIndex = indices.second;

    if (eventIndex < 0 || moduleIndex < 0)
        return;

    AnalysisPauser pauser(m_context);

    // "single word" filters
    {
        const auto filterDefinitions = defaultDataFilters.value(module->type);

        for (const auto &filterDef: filterDefinitions)
        {
            DataFilter dataFilter(filterDef.filter);
            MultiWordDataFilter multiWordFilter({dataFilter});
            double unitMin = 0.0;
            double unitMax = (1 << multiWordFilter.getDataBits());

            RawDataDisplay rawDataDisplay = make_raw_data_display(multiWordFilter, unitMin, unitMax,
                                                                  filterDef.name,
                                                                  filterDef.title,
                                                                  QString());

            add_raw_data_display(m_context->getAnalysisNG(), eventIndex, moduleIndex, rawDataDisplay);
        }
    }

    // "dual word" filters
    {
        const auto filterDefinitions = defaultDualWordFilters.value(module->type);
        for (const auto &filterDef: filterDefinitions)
        {
            DataFilter loWordFilter(filterDef.lowFilter);
            DataFilter hiWordFilter(filterDef.highFilter);
            MultiWordDataFilter multiWordFilter({loWordFilter, hiWordFilter});

            double unitMin = 0.0;
            double unitMax = (1 << multiWordFilter.getDataBits());

            RawDataDisplay rawDataDisplay = make_raw_data_display(multiWordFilter, unitMin, unitMax,
                                                                  filterDef.name,
                                                                  filterDef.title,
                                                                  QString());

            add_raw_data_display(m_context->getAnalysisNG(), eventIndex, moduleIndex, rawDataDisplay);
        }
    }

    //m_context->getAnalysisNG()->beginRun(); // FIXME: needed?
    repopulate();
}

EventWidget::EventWidget(MVMEContext *ctx, const QUuid &eventId, AnalysisWidget *analysisWidget, QWidget *parent)
    : QWidget(parent)
    , m_d(new EventWidgetPrivate)
{
    *m_d = {};
    m_d->m_q = this;
    m_d->m_context = ctx;
    m_d->m_eventId = eventId;
    m_d->m_analysisWidget = analysisWidget;

    int eventIndex = -1;
    auto eventConfigs = m_d->m_context->getEventConfigs();
    for (int idx = 0; idx < eventConfigs.size(); ++idx)
    {
        if (eventConfigs[idx]->getId() == eventId)
        {
            eventIndex = idx;
            break;
        }
    }

    m_d->m_eventIndex = eventIndex;

    auto outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // Row frames and splitter:
    // Two rows, the top one containing Modules and Operators, the bottom one
    // containing histograms.
    auto rowSplitter = new QSplitter(Qt::Vertical);
    outerLayout->addWidget(rowSplitter);

    auto operatorFrame = new QFrame;
    auto operatorFrameLayout = new QHBoxLayout(operatorFrame);
    operatorFrameLayout->setContentsMargins(2, 2, 2, 2);
    rowSplitter->addWidget(operatorFrame);

    auto displayFrame = new QFrame;
    auto displayFrameLayout = new QHBoxLayout(displayFrame);
    displayFrameLayout->setContentsMargins(2, 2, 2, 2);
    rowSplitter->addWidget(displayFrame);

    // Column frames and splitters:
    // One column for each user level
    m_d->m_operatorFrameSplitter = new QSplitter;
    operatorFrameLayout->addWidget(m_d->m_operatorFrameSplitter);

    m_d->m_displayFrameSplitter = new QSplitter;
    displayFrameLayout->addWidget(m_d->m_displayFrameSplitter);

    auto sync_splitters = [](QSplitter *sa, QSplitter *sb)
    {
        auto sync_one_way = [](QSplitter *src, QSplitter *dst)
        {
            connect(src, &QSplitter::splitterMoved, dst, [src, dst](int, int) {
                dst->setSizes(src->sizes());
            });
        };

        sync_one_way(sa, sb);
        sync_one_way(sb, sa);
    };

    sync_splitters(m_d->m_operatorFrameSplitter, m_d->m_displayFrameSplitter);

    m_d->repopulate();
}

void EventWidget::selectInputFor(Slot *slot, s32 userLevel, SelectInputCallback callback)
{
    m_d->m_mode = EventWidgetPrivate::SelectInput;
    m_d->m_selectInputSlot = slot;
    m_d->m_selectInputUserLevel = userLevel;
    m_d->m_selectInputCallback = callback;
    m_d->modeChanged();
    // The actual input selection is handled in onNodeClicked()
}

void EventWidget::endSelectInput()
{
    if (m_d->m_mode == EventWidgetPrivate::SelectInput)
    {
        m_d->m_mode = EventWidgetPrivate::Default;
        m_d->m_selectInputCallback = nullptr;
        m_d->modeChanged();
    }
}

void EventWidget::addOperator(OperatorPtr op, s32 userLevel)
{
    if (!op) return;

    AnalysisPauser pauser(m_d->m_context);
    m_d->m_context->getAnalysisNG()->addOperator(m_d->m_eventIndex, op, userLevel);
    m_d->repopulate();
}

void EventWidget::operatorEdited(OperatorInterface *op)
{
    // Updates the edited SourceInterface and recursively all the operators
    // depending on it.
    AnalysisPauser pauser(m_d->m_context);
    do_beginRun_forward(op);
    m_d->repopulate();
}

void EventWidget::removeOperator(OperatorInterface *op)
{
    AnalysisPauser pauser(m_d->m_context);
    m_d->m_context->getAnalysisNG()->removeOperator(op);
    m_d->repopulate();
}

void EventWidget::addSource(SourcePtr src, ModuleConfig *module, bool addHistogramsAndCalibration,
                            const QString &unitLabel, double unitMin, double unitMax)
{
    if (!src) return;

    auto indices = m_d->m_context->getDAQConfig()->getEventAndModuleIndices(module);
    s32 eventIndex = indices.first;
    s32 moduleIndex = indices.second;
    auto analysis = m_d->m_context->getAnalysisNG();


    AnalysisPauser pauser(m_d->m_context);

    if (addHistogramsAndCalibration)
    {
        // Only implemented for Extractor type sources
        auto extractor = std::dynamic_pointer_cast<Extractor>(src);
        Q_ASSERT(extractor);
        auto rawDisplay = make_raw_data_display(extractor, unitMin, unitMax, "MISSING TITLE", // INCOMPLETE
                                                unitLabel);
        add_raw_data_display(analysis, eventIndex, moduleIndex, rawDisplay);
    }
    else
    {
        analysis->addSource(eventIndex, moduleIndex, src);
    }

    m_d->repopulate();
}

void EventWidget::sourceEdited(SourceInterface *src)
{
    // Updates the edited SourceInterface and recursively all the operators
    // depending on it.
    AnalysisPauser pauser(m_d->m_context);
    do_beginRun_forward(src);
    m_d->repopulate();
}

void EventWidget::removeSource(SourceInterface *src)
{
    AnalysisPauser pauser(m_d->m_context);
    m_d->m_context->getAnalysisNG()->removeSource(src);
    m_d->repopulate();
}

void EventWidget::uniqueWidgetCloses()
{
    m_d->m_uniqueWidgetActive = false;
}

void EventWidget::addUserLevel(s32 eventIndex)
{
    m_d->addUserLevel(eventIndex);
}

MVMEContext *EventWidget::getContext() const
{
    return m_d->m_context;
}

bool EventWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
    {
        for (auto trees: m_d->m_levelTrees)
        {
            for (auto tree: {trees.operatorTree, trees.displayTree})
            {
                if (tree == watched)
                {
                    if (!tree->currentItem())
                    {
                        auto node = tree->topLevelItem(0);
                        if (node)
                        {
                            tree->setCurrentItem(node);
                        }
                    }
                    break;
                }
            }
        }
    }
}

EventWidget::~EventWidget()
{
    delete m_d;
}

struct AnalysisWidgetPrivate
{
    AnalysisWidget *m_q;
    MVMEContext *m_context;
    QHash<QUuid, EventWidget *> m_eventWidgetsByEventId;
    QList<EventConfig *> m_eventConfigs;


    QToolBar *m_toolbar;
    QComboBox *m_eventSelectCombo;
    QStackedWidget *m_eventWidgetStack;

    void repopulate();

    void actionNew();
    void actionOpen();
    void actionSave();
    void actionSaveAs();
    void actionClearHistograms();

    void updateWindowTitle();
};

void AnalysisWidgetPrivate::repopulate()
{
    int lastEventSelectIndex = m_eventSelectCombo->currentIndex();

    // Clear combobox and stacked widget. This deletes all existing EventWidgets.
    m_eventSelectCombo->clear();
    
    while (auto widget = m_eventWidgetStack->currentWidget())
    {
        m_eventWidgetStack->removeWidget(widget);
        widget->deleteLater();
    }
    Q_ASSERT(m_eventWidgetStack->count() == 0);
    m_eventWidgetsByEventId.clear();

    // Repopulate combobox and stacked widget
    m_eventConfigs = m_context->getEventConfigs();

    // FIXME: use ids here
    // FIXME: event creation is still entirely based on the DAQ config. events
    //        that do exist in the analysis but in the DAQ won't show up at all
    for (s32 eventIndex = 0;
         eventIndex < m_eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = m_eventConfigs[eventIndex];
        auto eventId = eventConfig->getId();
        auto eventWidget = new EventWidget(m_context, eventId, m_q);
        m_eventSelectCombo->addItem(QString("%1 (idx=%2)").arg(eventConfig->objectName()).arg(eventIndex));

        auto scrollArea = new QScrollArea;
        scrollArea->setWidget(eventWidget);
        scrollArea->setWidgetResizable(true);

        m_eventWidgetStack->addWidget(scrollArea);
        m_eventWidgetsByEventId[eventId] = eventWidget;
    }

    if (lastEventSelectIndex >= 0 && lastEventSelectIndex < m_eventSelectCombo->count())
    {
        m_eventSelectCombo->setCurrentIndex(lastEventSelectIndex);
    }

    updateWindowTitle();
}

static const QString AnalysisFileFilter = QSL("MVME Analysis Files (*.analysis);; All Files (*.*)");

void AnalysisWidgetPrivate::actionNew()
{
    AnalysisPauser pauser(m_context);
    m_context->getAnalysisNG()->clear();
    m_context->setAnalysisConfigFileName(QString());
    repopulate();
}

void AnalysisWidgetPrivate::actionOpen()
{
    auto path = m_context->getWorkspaceDirectory();
    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    QString fileName = QFileDialog::getOpenFileName(m_q, QSL("Load analysis config"), path, AnalysisFileFilter);
    if (fileName.isEmpty())
        return;
    m_context->loadAnalysisConfig(fileName);
}

void AnalysisWidgetPrivate::actionSave()
{
    QString fileName = m_context->getAnalysisConfigFileName();

    if (fileName.isEmpty())
    {
        actionSaveAs();
    }
    else
    {
        auto result = saveAnalysisConfig(nullptr, m_context->getAnalysisNG(), fileName,
                                         m_context->getWorkspaceDirectory(), AnalysisFileFilter);
        if (result.first)
        {
            m_context->setAnalysisConfigFileName(result.second);
        }
    }
}

void AnalysisWidgetPrivate::actionSaveAs()
{
    auto result = saveAnalysisConfigAs(nullptr, m_context->getAnalysisNG(),
                                       m_context->getWorkspaceDirectory(), AnalysisFileFilter);

    if (result.first)
    {
        m_context->setAnalysisConfigFileName(result.second);
    }
}

void AnalysisWidgetPrivate::actionClearHistograms()
{
    AnalysisPauser pauser(m_context);

    for (auto &opEntry: m_context->getAnalysisNG()->getOperators())
    {
        if (auto histoSink = qobject_cast<Histo1DSink *>(opEntry.op.get()))
        {
            for (auto &histo: histoSink->m_histos)
            {
                histo->clear();
            }
        }
        else if (auto histoSink = qobject_cast<Histo2DSink *>(opEntry.op.get()))
        {
            histoSink->m_histo->clear();
        }
    }
}

void AnalysisWidgetPrivate::updateWindowTitle()
{
    QString fileName = m_context->getAnalysisConfigFileName();

    if (fileName.isEmpty())
        fileName = QSL("<not saved>");

    auto wsDir = m_context->getWorkspaceDirectory() + '/';

    if (fileName.startsWith(wsDir))
        fileName.remove(wsDir);

    m_q->setWindowTitle(QString(QSL("%1 - [Analysis UI]")).arg(fileName));
}

AnalysisWidget::AnalysisWidget(MVMEContext *ctx, QWidget *parent)
    : QWidget(parent)
    , m_d(new AnalysisWidgetPrivate)
{
    m_d->m_q = this;
    m_d->m_context = ctx;

    auto do_repopulate_lambda = [this]() { m_d->repopulate(); };

    // DAQ config changes
    connect(m_d->m_context, &MVMEContext::daqConfigChanged, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::eventAdded, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::eventAboutToBeRemoved, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::moduleAdded, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::moduleAboutToBeRemoved, this, do_repopulate_lambda);

    // Analysis changes
    connect(m_d->m_context, &MVMEContext::analysisNGChanged, this, [this]() {
        m_d->repopulate();
    });

    connect(m_d->m_context, &MVMEContext::analysisConfigFileNameChanged, this, [this](const QString &) {
        m_d->updateWindowTitle();
    });

    // toolbar
    {
        m_d->m_toolbar = new QToolBar;
        m_d->m_toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        m_d->m_toolbar->setIconSize(QSize(16, 16));
        auto font = m_d->m_toolbar->font();
        font.setPointSize(font.pointSize() - 1);
        m_d->m_toolbar->setFont(font);

        m_d->m_toolbar->addAction(QIcon(":/document-new.png"), QSL("New"), this, [this]() { m_d->actionNew(); });
        m_d->m_toolbar->addAction(QIcon(":/document-open.png"), QSL("Open"), this, [this]() { m_d->actionOpen(); });
        m_d->m_toolbar->addAction(QIcon(":/document-save.png"), QSL("Save"), this, [this]() { m_d->actionSave(); });
        m_d->m_toolbar->addAction(QIcon(":/document-save-as.png"), QSL("Save As"), this, [this]() { m_d->actionSaveAs(); });
        m_d->m_toolbar->addSeparator();
        m_d->m_toolbar->addAction(QIcon(":/clear_histos.png"), QSL("Clear Histograms"), this, [this]() { m_d->actionClearHistograms(); });
    }

    auto toolbarFrame = new QFrame;
    toolbarFrame->setFrameStyle(
        //QFrame::NoFrame);
        QFrame::StyledPanel | QFrame::Sunken);
    auto toolbarFrameLayout = new QHBoxLayout(toolbarFrame);
    toolbarFrameLayout->setContentsMargins(0, 0, 0, 0);
    toolbarFrameLayout->setSpacing(0);
    toolbarFrameLayout->addWidget(m_d->m_toolbar);

    m_d->m_eventSelectCombo = new QComboBox;
    m_d->m_eventWidgetStack = new QStackedWidget;

    connect(m_d->m_eventSelectCombo, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
            m_d->m_eventWidgetStack, &QStackedWidget::setCurrentIndex);

    // TODO: implement removeUserLevel functionality. Disable button if highest
    // user level is not empty. Enable if it becomes empty.
    auto removeUserLevelButton = new QToolButton();
    removeUserLevelButton->setIcon(QIcon(QSL(":/list_remove.png")));
    connect(removeUserLevelButton, &QPushButton::clicked, this, [this]() {
    });
    removeUserLevelButton->setEnabled(false);
    

    auto addUserLevelButton = new QToolButton();
    addUserLevelButton->setIcon(QIcon(QSL(":/list_add.png")));
    connect(addUserLevelButton, &QPushButton::clicked, this, [this]() {
        s32 eventIndex = m_d->m_eventSelectCombo->currentIndex();
        EventConfig *eventConfig = m_d->m_eventConfigs[eventIndex];
        EventWidget *eventWidget = m_d->m_eventWidgetsByEventId.value(eventConfig->getId());
        if (eventWidget)
        {
            eventWidget->addUserLevel(eventIndex);
        }
    });

    auto eventSelectLayout = new QHBoxLayout;
    eventSelectLayout->addWidget(new QLabel(QSL("Event:")));
    eventSelectLayout->addWidget(m_d->m_eventSelectCombo);
    eventSelectLayout->addStretch();
    eventSelectLayout->addWidget(removeUserLevelButton);
    eventSelectLayout->addWidget(addUserLevelButton);

    auto toolbarSeparator = new QFrame;
    toolbarSeparator->setFrameShape(QFrame::HLine);
    toolbarSeparator->setFrameShadow(QFrame::Sunken);

    auto layout = new QGridLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    //layout->setVerticalSpacing(2);
    s32 row = 0;
    layout->addWidget(toolbarFrame, row++, 0);
    //layout->addWidget(m_d->m_toolbar, row++, 0);
    //layout->addWidget(toolbarSeparator, row++, 0);
    layout->addLayout(eventSelectLayout, row++, 0);
    layout->addWidget(m_d->m_eventWidgetStack, row++, 0);

    auto analysis = ctx->getAnalysisNG();
    analysis->updateRanks();
    analysis->beginRun();

    m_d->repopulate();
}

AnalysisWidget::~AnalysisWidget()
{
    delete m_d;
}

} // end namespace analysis

#if 0
QAction *add_toolbar_action(QToolBar *toolbar, QIcon icon, const QString &title, const QString &statusTip = QString())
{
    auto result = toolbar->addAction(icon, title);
    if (!statusTip.isEmpty())
        result->setStatusTip(statusTip);
    else
        result->setStatusTip(title);

    return result;
}
#endif

#if 0
    {
        // DataExtractionEditor test
        analysis::MultiWordDataFilter testFilter;
        testFilter.addSubFilter(analysis::DataFilter("0001XXXXPO00AAAADDDDDDDDDDDDDDDD", 3));
        testFilter.addSubFilter(analysis::DataFilter("0101XXXXXXXXXXXXDDDDDDDDDDDDDDDD", 4));

        auto testWidget = new DataExtractionEditor(this);
        testWidget->setWindowFlags(Qt::Tool);
        testWidget->show();

        testWidget->m_defaultFilter = "0001XXXXPO00AAAADDDDDDDDDDDDDDDD";
        testWidget->m_subFilters = testFilter.getSubFilters();
        testWidget->m_requiredCompletionCount = 42;
        testWidget->updateDisplay();
    }
#endif

#if 0
// Was playing around with storing shared_ptr<T> in QVariants. Getting the
// value out of the variant involves having to know the exact type T with which
// it was added. I prefer just storing raw pointers and qobject_cast()'ing or
// reinterpret_cast()'ing those.

template<typename T, typename U>
std::shared_ptr<T> qobject_pointer_cast(const std::shared_ptr<U> &src)
{
    if (auto rawT = qobject_cast<T *>(src.get()))
    {
        return std::shared_ptr<T>(src, rawT);
    }

    return std::shared_ptr<T>();
}

template<typename T>
TreeNode *makeNode(const std::shared_ptr<T> &data, int type = QTreeWidgetItem::Type)
{
    auto ret = new TreeNode(type);
    ret->setData(0, DataRole_SharedPointer, QVariant::fromValue(data));
    return ret;
}

template<typename T>
std::shared_ptr<T> getSharedPointer(QTreeWidgetItem *node, s32 dataRole = DataRole_SharedPointer)
{
    return node ? node->data(0, dataRole).value<std::shared_ptr<T>>() : std::shared_ptr<T>();
}
#endif

