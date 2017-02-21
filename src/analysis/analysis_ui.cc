#include "analysis_ui.h"
#include "analysis_ui_p.h"

#include "../mvme_context.h"

#include <QComboBox>
#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
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

//typedef QTreeWidgetItem TreeNode;
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
T *getPointer(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
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

    if (sink->histos.size() > 0)
    {
        for (s32 addr = 0; addr < sink->histos.size(); ++addr)
        {
            auto histo = sink->histos[addr].get();
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

struct DisplayLevelTrees
{
    QTreeWidget *operatorTree;
    QTreeWidget *displayTree;
    s32 userLevel;
};

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
    int eventIndex;

    QVector<DisplayLevelTrees> m_levelTrees;

    Mode m_mode;
    bool m_addAnalysisElementWidgetActive;
    Slot *m_selectInputSlot;
    s32 m_selectInputUserLevel;
    EventWidget::SelectInputCallback m_selectInputCallback;

    QSplitter *m_operatorFrameSplitter;
    QSplitter *m_displayFrameSplitter;

    void createView(s32 eventIndex);
    DisplayLevelTrees createTrees(s32 eventIndex, s32 level);
    DisplayLevelTrees createSourceTrees(s32 eventIndex);
    void appendTreesToView(DisplayLevelTrees trees);

    void addUserLevel(s32 eventIndex);

    void doOperatorTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel);
    void doDisplayTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel);

    void modeChanged();
    void highlightValidInputNodes(QTreeWidgetItem *node);
    void clearNodeHighlights(QTreeWidgetItem *node);
    void onNodeClicked(TreeNode *node, int column);
    void onNodeDoubleClicked(TreeNode *node, int column);
    void clearAllTreeSelections();
    void clearTreeSelectionsExcept(QTreeWidget *tree);
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

    // Build a list of operators for the current level

    auto analysis = m_context->getAnalysisNG();
    QVector<Analysis::OperatorEntry> operators = analysis->getOperators(eventIndex, level);

    // populate the OperatorTree
    for (auto entry: operators)
    {
        if(!qobject_cast<SinkInterface *>(entry.op.get()))
        {
            qDebug() << ">>> Adding to the display tree cause it's not a SinkInterface:" << entry.op.get();
            auto opNode = makeOperatorNode(entry.op.get());
            result.operatorTree->addTopLevelItem(opNode);
        }
    }
    result.operatorTree->sortItems(0, Qt::AscendingOrder);

    // populate the DisplayTree
    {
        auto histo1DRoot = new TreeNode({QSL("1D")});
        auto histo2DRoot = new TreeNode({QSL("2D")});
        result.displayTree->addTopLevelItem(histo1DRoot);
        result.displayTree->addTopLevelItem(histo2DRoot);

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

    // populate the OperatorTree
    int moduleIndex = 0;
    for (auto mod: modules)
    {
        auto moduleNode = makeModuleNode(mod);
        result.operatorTree->addTopLevelItem(moduleNode);

        for (auto sourceEntry: analysis->getSources(eventIndex, moduleIndex))
        {
            auto sourceNode = makeOperatorTreeSourceNode(sourceEntry.source.get());
            moduleNode->addChild(sourceNode);
        }
        ++moduleIndex;
    }
    result.operatorTree->sortItems(0, Qt::AscendingOrder);

    // populate the DisplayTree
    moduleIndex = 0;
    auto opEntries = analysis->getOperators(eventIndex, 0);
    for (auto mod: modules)
    {
        auto moduleNode = makeModuleNode(mod);
        result.displayTree->addTopLevelItem(moduleNode);

        for (auto sourceEntry: analysis->getSources(eventIndex, moduleIndex))
        {
            for (const auto &entry: opEntries)
            {
                auto histoSink = qobject_cast<Histo1DSink *>(entry.op.get());
                if (histoSink && (histoSink->getSlot(0)->inputPipe == sourceEntry.source->getOutput(0)))
                {
                    auto histoNode = makeHisto1DNode(histoSink);
                    moduleNode->addChild(histoNode);
                }
            }
        }
        ++moduleIndex;
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
    }
}

void EventWidgetPrivate::addUserLevel(s32 eventIndex)
{
    s32 levelIndex = m_levelTrees.size();
    auto trees = createTrees(eventIndex, levelIndex);
    m_levelTrees.push_back(trees);
    appendTreesToView(trees);
}

void EventWidgetPrivate::doOperatorTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel)
{
    auto node = tree->itemAt(pos);
    auto obj  = getQObject(node);

    QMenu menu;

    if (node)
    {
        if (userLevel == 0 && node->type() == NodeType_Module)
        {
            if (!m_addAnalysisElementWidgetActive)
            {
#if 0
                auto menuNew = new QMenu;

                auto add_action = [this, &menu, menuNew, userLevel](const QString &title, auto srcPtr)
                {
                    menuNew->addAction(title, &menu, [this, userLevel, srcPtr]() {
                        auto widget = new AddSourceWidget(srcPtr, //TODO: module info herem_q);
                             widget->move(QCursor::pos());
                        widget->setAttribute(Qt::WA_DeleteOnClose);
                        widget->show();
                        m_addAnalysisElementWidgetActive = true;
                        clearAllTreeSelections();
                    });
                };

                auto analysis = m_context->getAnalysisNG();
                auto &registry(analysis->getRegistry());

                for (auto sourceName: registry.getSourceNames())
                {
                    SourcePtr src(registry.makeSource(sourceName));
                    add_action(src->getDisplayName(), src);
                }

                auto actionNew = menu.addAction(QSL("New"));
                actionNew->setMenu(menuNew);
                menu.addAction(actionNew);
#endif
            }
        }

        if (userLevel == 0 && node->type() == NodeType_Source)
        {
            auto pipeSource = getPointer<PipeSourceInterface>(node);

            if (pipeSource)
            {
                Q_ASSERT(pipeSource->getNumberOfOutputs() == 1); // TODO: implement the case for multiple outputs
                auto pipe = pipeSource->getOutput(0);

                menu.addAction(QSL("Show Parameters"), [this, pipe]() {
                    auto widget = new PipeDisplay(pipe, m_q);
                    widget->move(QCursor::pos());
                    widget->setAttribute(Qt::WA_DeleteOnClose);
                    widget->show();
                });
            }
        }
        else if (userLevel > 0 && node->type() == NodeType_OutputPipe)
        {
            auto pipe = getPointer<Pipe>(node);

            menu.addAction(QSL("Show Parameters"), [this, pipe]() {
                auto widget = new PipeDisplay(pipe, m_q);
                widget->move(QCursor::pos());
                widget->setAttribute(Qt::WA_DeleteOnClose);
                widget->show();
            });
        }
    }
    else // No node selected
    {
        if (m_mode == EventWidgetPrivate::Default && !m_addAnalysisElementWidgetActive)
        {
            if (userLevel > 0)
            {
                auto menuNew = new QMenu;

                auto add_action = [this, &menu, menuNew, userLevel](const QString &title, auto opPtr)
                {
                    menuNew->addAction(title, &menu, [this, userLevel, opPtr]() {
                        auto widget = new AddOperatorWidget(opPtr, userLevel, m_q);
                        widget->move(QCursor::pos());
                        widget->setAttribute(Qt::WA_DeleteOnClose);
                        widget->show();
                        m_addAnalysisElementWidgetActive = true;
                        clearAllTreeSelections();
                    });
                };

                auto analysis = m_context->getAnalysisNG();
                auto &registry(analysis->getRegistry());

                for (auto operatorName: registry.getOperatorNames())
                {
                    OperatorPtr op(registry.makeOperator(operatorName));
                    add_action(op->getDisplayName(), op);
                }

                auto actionNew = menu.addAction(QSL("New"));
                actionNew->setMenu(menuNew);
                menu.addAction(actionNew);
            }
        }
    }

    if (!menu.isEmpty())
    {
        menu.exec(tree->mapToGlobal(pos));
    }
}

void EventWidgetPrivate::doDisplayTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel)
{
    auto node = tree->itemAt(pos);
    auto obj  = getQObject(node);

    QMenu menu;

    if (node)
    {
        switch (node->type())
        {
            case NodeType_Histo1D:
                {
                    if (auto histo = qobject_cast<Histo1D *>(obj))
                    {
                        menu.addAction(QSL("Open"), m_q, [this, histo]() {
                            m_context->openInNewWindow(histo);
                        });
                    }
                } break;
        }
    }
    else
    {
        if (m_mode == EventWidgetPrivate::Default && !m_addAnalysisElementWidgetActive)
        {
            auto menuNew = new QMenu;

            auto add_action = [this, &menu, menuNew, userLevel](const QString &title, auto opPtr)
            {
                menuNew->addAction(title, &menu, [this, userLevel, opPtr]() {
                    auto widget = new AddOperatorWidget(opPtr, userLevel, m_q);
                    widget->move(QCursor::pos());
                    widget->setAttribute(Qt::WA_DeleteOnClose);
                    widget->show();
                    m_addAnalysisElementWidgetActive = true;
                    clearAllTreeSelections();
                });
            };

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

            auto actionNew = menu.addAction(QSL("New"));
            actionNew->setMenu(menuNew);
            menu.addAction(actionNew);
        }
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
                    clearNodeHighlights(opTree->invisibleRootItem());
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

bool isValidInputNode(QTreeWidgetItem *node, Slot *slot)
{
    bool result = false;
    if ((slot->acceptedInputTypes & InputType::Array)
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

static const QColor ValidInputNodeColor = QColor("lightgreen");

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

void EventWidgetPrivate::clearNodeHighlights(QTreeWidgetItem *node)
{
    node->setBackground(0, QBrush());

    for (s32 childIndex = 0; childIndex < node->childCount(); ++childIndex)
    {
        // recurse
        auto child = node->child(childIndex);
        clearNodeHighlights(child);
    }
}

void EventWidgetPrivate::onNodeClicked(TreeNode *node, int column)
{
    switch (m_mode)
    {
        case Default:
            {
            } break;

        case SelectInput:
            {
                if (isValidInputNode(node, m_selectInputSlot))
                {
                    Slot *slot = m_selectInputSlot;
                    // connect the slot with the selected input source
                    // TODO: don't directly connect here. instead pass info
                    // about the selected input to the AddOperatorWidget
                    // (probably using the callback method).
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

                        default:
                            Q_ASSERT(!"Invalid code path");
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

EventWidget::EventWidget(MVMEContext *ctx, const QUuid &eventId, QWidget *parent)
    : QWidget(parent)
    , m_d(new EventWidgetPrivate)
{
    *m_d = {};
    m_d->m_q = this;
    m_d->m_context = ctx;
    m_d->m_eventId = eventId;

    // TODO: This needs to be done whenever the analysis object is modified.
    auto analysis = ctx->getAnalysisNG();
    analysis->updateRanks();
    analysis->beginRun();

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

    // FIXME: use the actual eventId here instead of going back to get the index
    int eventIndex = -1;
    auto eventConfigs = m_d->m_context->getEventConfigs();
    for (int idx = 0; idx < eventConfigs.size(); ++idx)
    {
        if (eventConfigs[idx]->getId() == eventId)
        {
            eventIndex= idx;
            break;
        }
    }

    if (eventIndex >= 0)
    {
        // This populates m_d->m_levelTrees
        m_d->createView(eventIndex);
    }

    for (auto trees: m_d->m_levelTrees)
    {
        m_d->appendTreesToView(trees);
    }
}

void EventWidget::selectInputFor(Slot *slot, s32 userLevel, SelectInputCallback callback)
{
    m_d->m_mode = EventWidgetPrivate::SelectInput;
    m_d->m_selectInputSlot = slot;
    m_d->m_selectInputUserLevel = userLevel;
    m_d->m_selectInputCallback = callback;
    m_d->modeChanged();
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
    Q_ASSERT(userLevel < m_d->m_levelTrees.size());
    if (userLevel < m_d->m_levelTrees.size())
    {
        m_d->m_context->getAnalysisNG()->addOperator(m_d->eventIndex, op, userLevel);
        op->beginRun();

        auto trees = m_d->m_levelTrees[userLevel];
        QTreeWidget *destTree = nullptr;
        if (auto histoSink = qobject_cast<Histo1DSink *>(op.get()))
        {
            destTree = trees.displayTree;
            auto node = makeHisto1DNode(histoSink);
            // the histo1DRoot node is the first child of the display tree
            destTree->topLevelItem(0)->addChild(node);
        }
        else if (auto histoSink = qobject_cast<Histo2DSink *>(op.get()))
        {
            destTree = trees.displayTree;
            auto node = makeHisto2DNode(histoSink);
            // the histo2DRoot node is the second child of the display tree
            destTree->topLevelItem(1)->addChild(node);
        }
        else if (auto sink = qobject_cast<SinkInterface *>(op.get()))
        {
            // other sink type
            destTree = trees.displayTree;
            auto node = makeSinkNode(sink);
            destTree->addTopLevelItem(node);
        }
        else // It's an operator
        {
            destTree = trees.operatorTree;
            auto node = makeOperatorNode(op.get());
            destTree->addTopLevelItem(node);
        }
    }
}

void EventWidget::addAnalysisElementWidgetCloses()
{
    m_d->m_addAnalysisElementWidgetActive = false;
}

void EventWidget::addUserLevel(s32 eventIndex)
{
    m_d->addUserLevel(eventIndex);
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
};

AnalysisWidget::AnalysisWidget(MVMEContext *ctx, QWidget *parent)
    : QWidget(parent)
    , m_d(new AnalysisWidgetPrivate)
{
    m_d->m_q = this;
    m_d->m_context = ctx;

    auto eventSelectCombo = new QComboBox;
    auto eventWidgetStack = new QStackedWidget;

    connect(eventSelectCombo, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
            eventWidgetStack, &QStackedWidget::setCurrentIndex);

    m_d->m_eventConfigs = m_d->m_context->getEventConfigs();

    // FIXME:  use ids here
    for (s32 eventIndex = 0;
         eventIndex < m_d->m_eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = m_d->m_eventConfigs[eventIndex];
        auto eventId = eventConfig->getId();
        auto eventWidget = new EventWidget(m_d->m_context, eventId);
        eventSelectCombo->addItem(QString("%1 (idx=%2)").arg(eventConfig->objectName()).arg(eventIndex));

        auto scrollArea = new QScrollArea;
        scrollArea->setWidget(eventWidget);
        scrollArea->setWidgetResizable(true);

        eventWidgetStack->addWidget(scrollArea);
        m_d->m_eventWidgetsByEventId[eventId] = eventWidget;
    }

    auto addUserLevelButton = new QPushButton("+");
    connect(addUserLevelButton, &QPushButton::clicked, this, [this, eventSelectCombo]() {
        s32 eventIndex = eventSelectCombo->currentIndex();
        EventConfig *eventConfig = m_d->m_eventConfigs[eventIndex];
        EventWidget *eventWidget = m_d->m_eventWidgetsByEventId.value(eventConfig->getId());
        if (eventWidget)
        {
            eventWidget->addUserLevel(eventIndex);
        }
    });

    auto eventSelectLayout = new QHBoxLayout;
    eventSelectLayout->addWidget(new QLabel(QSL("Event:")));
    eventSelectLayout->addWidget(eventSelectCombo);
    eventSelectLayout->addStretch();
    eventSelectLayout->addWidget(addUserLevelButton);

    auto layout = new QGridLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addLayout(eventSelectLayout, 0, 0);
    layout->addWidget(eventWidgetStack, 1, 0);
}

AnalysisWidget::~AnalysisWidget()
{
    delete m_d;
}

} // end namespace analysis
