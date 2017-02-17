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

    // FIXME: can't this be done generically via getNumberOfOutputs() and using
    // the pipes parameter vector size?
    if (auto extractor = qobject_cast<Extractor *>(source))
    {
        s32 addressCount = (1 << extractor->getFilter().getAddressBits());
        Pipe *outputPipe = extractor->getOutput(0);

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

    if (sink->histos.size() > 1)
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

#if 0 // Input nodes disabled for now
    auto inputsNode = new TreeNode;
    inputsNode->setText(0, "inputs");
    result->addChild(inputsNode);

    // inputs
    for (s32 inputIndex = 0;
         inputIndex < op->getNumberOfSlots();
         ++inputIndex)
    {
        // FIXME: check for array or value slot
        // display slot acceptance type
        // display if no inputpipe connected
        // add something like NodeType_Slot
        // add something like NodeType_ArrayValue or ParamIndex VectorElement ... best name for this?
        // add NodeType_OutputPipe, Output, ...

        auto slot = op->getSlot(inputIndex);
        auto inputPipe = slot->inputPipe;
        s32 inputParamSize = inputPipe ? inputPipe->parameters.size() : 0;

        auto slotNode = new TreeNode;
        slotNode->setText(0, QString("#%1 \"%2\" (%3 elements)")
                          .arg(inputIndex)
                          .arg(slot->name)
                          .arg(inputParamSize)
                         );

        inputsNode->addChild(slotNode);

        for (s32 paramIndex = 0; paramIndex < inputParamSize; ++paramIndex)
        {
            auto paramNode = new TreeNode;
            paramNode->setText(0, QString("[%1]").arg(paramIndex));

            slotNode->addChild(paramNode);
        }
    }

    auto outputsNode = new TreeNode;
    outputsNode->setText(0, "outputs");
    result->addChild(outputsNode);
#endif

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
        //outputsNode->addChild(pipeNode);
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
    int eventIndex; // FIXME: init

    QVector<DisplayLevelTrees> m_levelTrees;

    Mode m_mode;
    bool m_addAnalysisElementWidgetActive;
    Slot *m_selectInputSlot;
    s32 m_selectInputUserLevel;
    EventWidget::SelectInputCallback m_selectInputCallback;

    void createView(int eventIndex);
    DisplayLevelTrees createTrees(s32 eventIndex, s32 level);
    DisplayLevelTrees createSourceTrees(s32 eventIndex);

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
void EventWidgetPrivate::createView(int eventIndex)
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

        for (auto tree: {trees.operatorTree, trees.displayTree})
        {
            QObject::connect(tree, &QTreeWidget::currentItemChanged, m_q,
                             [this, tree](QTreeWidgetItem *current, QTreeWidgetItem *previous) {
                if (current)
                {
                    clearTreeSelectionsExcept(tree);
                }
            });

        }
#if 0
        QObject::connect(trees.operatorTree, &QTreeWidget::itemClicked, m_q, [](QTreeWidgetItem *node, int column) {
            qDebug() << "item clicked" << node << column;
        });
        QObject::connect(trees.operatorTree, &QTreeWidget::itemPressed, m_q, [](QTreeWidgetItem *node, int column) {
            qDebug() << "item pressed" << node << column;
        });
#endif
    }
}

DisplayLevelTrees EventWidgetPrivate::createTrees(s32 eventIndex, s32 level)
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

    DisplayLevelTrees result = { new QTreeWidget, new QTreeWidget };

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

void EventWidgetPrivate::doOperatorTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel)
{
    auto node = tree->itemAt(pos);
    auto obj  = getQObject(node);

    QMenu menu;

    if (node)
    {
        if (userLevel == 0 && node->type() == NodeType_Module && !m_addAnalysisElementWidgetActive)
        {
        // TODO: implement this stuff
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
    else
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

void EventWidgetPrivate::highlightValidInputNodes(QTreeWidgetItem *node)
{
    if (isValidInputNode(node, m_selectInputSlot))
    {
        node->setBackground(0, Qt::green);
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
    setMinimumSize(1000, 600); // FIXME: find another way to make the window be sanely sized at startup

    // TODO: This needs to be done whenever the analysis object is modified.
    auto analysis = ctx->getAnalysisNG();
    analysis->updateRanks();
    analysis->beginRun();

    auto outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    auto scrollArea = new QScrollArea;
    outerLayout->addWidget(scrollArea);

    // FIXME: I don't get how scrollarea works
    //auto scrollWidget = new QWidget;
    //scrollArea->setWidget(scrollWidget);

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

    sync_splitters(operatorFrameColumnSplitter, displayFrameColumnSplitter);

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
        m_d->createView(eventIndex);
    }

    auto onItemClicked = [](TreeNode *node, int column)
    {
        qDebug() << "EventWidget item clicked:" << node;
        qDebug() << getQObject(node);
    };

    for (s32 levelIndex = 0;
         levelIndex < m_d->m_levelTrees.size();
         ++levelIndex)
    {
        auto opTree   = m_d->m_levelTrees[levelIndex].operatorTree;
        auto dispTree = m_d->m_levelTrees[levelIndex].displayTree;
        s32 minTreeWidth = 200;
        opTree->setMinimumWidth(minTreeWidth);
        dispTree->setMinimumWidth(minTreeWidth);
        opTree->setContextMenuPolicy(Qt::CustomContextMenu);
        dispTree->setContextMenuPolicy(Qt::CustomContextMenu);

        operatorFrameColumnSplitter->addWidget(opTree);
        displayFrameColumnSplitter->addWidget(dispTree);

        // operator tree
        connect(opTree, &QTreeWidget::itemClicked, this, [this] (QTreeWidgetItem *node, int column) {
            m_d->onNodeClicked(reinterpret_cast<TreeNode *>(node), column);
        });

        connect(opTree, &QTreeWidget::itemDoubleClicked, this, [this] (QTreeWidgetItem *node, int column) {
            m_d->onNodeDoubleClicked(reinterpret_cast<TreeNode *>(node), column);
        });

        connect(opTree, &QWidget::customContextMenuRequested, this, [this, opTree, levelIndex] (QPoint pos) {
            m_d->doOperatorTreeContextMenu(opTree, pos, levelIndex);
        });

        // display tree
        connect(dispTree, &QTreeWidget::itemClicked, this, [this] (QTreeWidgetItem *node, int column) {
            m_d->onNodeClicked(reinterpret_cast<TreeNode *>(node), column);
        });

        connect(dispTree, &QTreeWidget::itemDoubleClicked, this, [this] (QTreeWidgetItem *node, int column) {
            m_d->onNodeDoubleClicked(reinterpret_cast<TreeNode *>(node), column);
        });

        connect(dispTree, &QWidget::customContextMenuRequested, this, [this, dispTree, levelIndex] (QPoint pos) {
            m_d->doDisplayTreeContextMenu(dispTree, pos, levelIndex);
        });
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

EventWidget::~EventWidget()
{
    delete m_d;
}

struct AnalysisWidgetPrivate
{
    AnalysisWidget *m_q;
    MVMEContext *m_context;
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

    auto eventConfigs = m_d->m_context->getEventConfigs();

    // FIXME:  use ids here
    for (s32 eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto eventId = eventConfig->getId();
        auto eventWidget = new EventWidget(m_d->m_context, eventId);
        eventSelectCombo->addItem(QString("%1 (id=%2)").arg(eventConfig->objectName()).arg(eventIndex));
        eventWidgetStack->addWidget(eventWidget);
    }

    auto eventSelectLayout = new QHBoxLayout;
    eventSelectLayout->addWidget(new QLabel(QSL("Event:")));
    eventSelectLayout->addWidget(eventSelectCombo);
    eventSelectLayout->addStretch();

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
