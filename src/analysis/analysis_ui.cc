/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#include "analysis_ui.h"
#include "analysis_ui_p.h"
#include "analysis_util.h"
#include "data_extraction_widget.h"
#include "analysis_info_widget.h"
#include "a2_adapter.h"
#include "analysis_impl_switch.h"
#ifdef MVME_ENABLE_HDF5
#include "analysis_session.h"
#endif

#include "../config_ui.h"
#include "../histo1d_widget.h"
#include "../histo2d_widget.h"
#include "../mvme_context.h"
#include "../mvme_stream_worker.h"
#include "../treewidget_utils.h"
#include "util/counters.h"
#include "util/strings.h"
#include "../vme_analysis_common.h"

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QtConcurrent>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>


#include <QJsonObject>

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
    ret->setFlags(ret->flags() & ~(Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled));
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

static QIcon makeIconFor(OperatorInterface *op)
{
    if (qobject_cast<Histo1DSink *>(op))
        return QIcon(":/hist1d.png");

    if (qobject_cast<Histo2DSink *>(op))
        return QIcon(":/hist2d.png");

    if (qobject_cast<SinkInterface *>(op))
        return QIcon(":/sink.png");

    if (qobject_cast<CalibrationMinMax *>(op))
        return QIcon(":/operator_calibration.png");

    if (qobject_cast<Difference *>(op))
        return QIcon(":/operator_difference.png");

    if (qobject_cast<PreviousValue *>(op))
        return QIcon(":/operator_previous.png");

    if (qobject_cast<Sum *>(op))
        return QIcon(":/operator_sum.png");

    return QIcon(":/operator_generic.png");
}

inline TreeNode *makeHisto1DNode(Histo1DSink *sink)
{
    auto node = makeNode(sink, NodeType_Histo1DSink);
    node->setText(0, QString("<b>%1</b> %2").arg(
            sink->getShortName(),
            sink->objectName()));
    node->setIcon(0, makeIconFor(sink));

    if (sink->m_histos.size() > 0)
    {
        for (s32 addr = 0; addr < sink->m_histos.size(); ++addr)
        {
            auto histo = sink->m_histos[addr].get();
            auto histoNode = makeNode(histo, NodeType_Histo1D);
            histoNode->setData(0, DataRole_HistoAddress, addr);
            histoNode->setText(0, QString::number(addr));
            node->setIcon(0, makeIconFor(sink));

            node->addChild(histoNode);
        }
    }
    return node;
};

inline TreeNode *makeHisto2DNode(Histo2DSink *sink)
{
    auto node = makeNode(sink, NodeType_Histo2DSink);
    node->setText(0, QString("<b>%1</b> %2").arg(
            sink->getShortName(),
            sink->objectName()));
    node->setIcon(0, makeIconFor(sink));

    return node;
}

inline TreeNode *makeSinkNode(SinkInterface *sink)
{
    auto node = makeNode(sink, NodeType_Sink);
    node->setText(0, QString("<b>%1</b> %2").arg(
            sink->getShortName(),
            sink->objectName()));
    node->setIcon(0, makeIconFor(sink));

    return node;
}

inline TreeNode *makeOperatorNode(OperatorInterface *op)
{
    auto result = makeNode(op, NodeType_Operator);
    result->setText(0, QString("<b>%1</b> %2").arg(
            op->getShortName(),
            op->objectName()));
    result->setIcon(0, makeIconFor(op));

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
    std::shared_ptr<Histo1DSink> sink;
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
    result.sink = std::dynamic_pointer_cast<Histo1DSink>(histoSink->getSharedPointer());

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

//
// EventWidgetTree
//
static const QString OperatorIdListMIMEType = QSL("application/x-mvme-analysis-operator-id-list");

bool EventWidgetTree::dropMimeData(QTreeWidgetItem *parentItem, int parentIndex, const QMimeData *data, Qt::DropAction action)
{
    if (action != Qt::MoveAction)
        return false;

    if (!data->hasFormat(OperatorIdListMIMEType))
        return false;

    bool isDisplayTree = (qobject_cast<DisplayTree *>(this) != nullptr);

    auto encoded = data->data(OperatorIdListMIMEType);
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    QVector<QByteArray> encodedIds;
    stream >> encodedIds;

    Q_ASSERT(encodedIds.size() == 1);

    if (encodedIds.size() != 1)
        return false;

    QUuid id(encodedIds.at(0));

    auto analysis = m_eventWidget->getContext()->getAnalysis();

    if (auto opEntry = analysis->getOperatorEntry(id))
    {
        bool isSink = (qobject_cast<SinkInterface *>(opEntry->op.get()) != nullptr);

        if ((isSink && !isDisplayTree)
            || (!isSink && isDisplayTree))
        {
            return false;
        }

        s32 levelDelta = m_userLevel - opEntry->userLevel;

        AnalysisPauser pauser(m_eventWidget->getContext());

        adjust_userlevel_forward(analysis->getOperators(), opEntry->op.get(), levelDelta);
        analysis->setModified(true);
        m_eventWidget->repopulate();
        m_eventWidget->getAnalysisWidget()->updateAddRemoveUserLevelButtons();

        return true;
    }

    return false;
}

QMimeData *EventWidgetTree::mimeData(const QList<QTreeWidgetItem *> items) const
{
    QVector<QByteArray> encodedIds;

    for (auto item: items)
    {
        switch (item->type())
        {
            case NodeType_Operator:
            case NodeType_Histo1DSink:
            case NodeType_Histo2DSink:
            case NodeType_Sink:
                {
                    if (auto op = getPointer<OperatorInterface>(item))
                    {
                        encodedIds.push_back(op->getId().toByteArray());
                    }
                } break;

            default:
                break;
        }
    }

    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream << encodedIds;

    auto result = new QMimeData;
    result->setData(OperatorIdListMIMEType, encoded);

    return result;
}

QStringList EventWidgetTree::mimeTypes() const
{
    return { OperatorIdListMIMEType };
}

Qt::DropActions EventWidgetTree::supportedDropActions() const
{
    return Qt::MoveAction;
}

void EventWidgetTree::dropEvent(QDropEvent *event)
{
    if (event->source() == this)
    {
        /* Disables the handling of internal move events implemented in
         * QTreeWidget::dropEvent(). */
        event->ignore();
    }
    else
    {
        /* Non-internal events are passed through */
        QTreeWidget::dropEvent(event);
    }
}

/* Top (operator) and bottom (display) trees for one user level. */
struct DisplayLevelTrees
{
    EventWidgetTree *operatorTree;
    DisplayTree *displayTree;
    s32 userLevel;
};

static const QString AnalysisFileFilter = QSL("MVME Analysis Files (*.analysis);; All Files (*.*)");

using SetOfVoidStar = QSet<void *>;

static const u32 PeriodicUpdateTimerInterval_ms = 1000;

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
    int m_eventIndex;
    AnalysisWidget *m_analysisWidget;

    QVector<DisplayLevelTrees> m_levelTrees;

    Mode m_mode = Default;
    // TODO: get rid of m_uniqueWidgetActive and only use m_uniqueWidget instead
    bool m_uniqueWidgetActive = false;
    QWidget *m_uniqueWidget = nullptr;
    Slot *m_selectInputSlot = nullptr;
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
    QTimer *m_displayRefreshTimer;

    // If set the trees for that user level will not be shown
    QVector<bool> m_hiddenUserLevels;

    // The user level that was manually added via addUserLevel()
    s32 m_manualUserLevel = 0;

    // Actions used in makeToolBar()
    std::unique_ptr<QAction> m_actionImportForModuleFromTemplate;
    std::unique_ptr<QAction> m_actionImportForModuleFromFile;
    QWidgetAction *m_actionModuleImport;

    // Actions and widgets used in makeEventSelectAreaToolBar()
    QAction *m_actionSelectVisibleLevels;
    QLabel *m_eventRateLabel;

    QToolBar* m_upperToolBar;
    QToolBar* m_eventSelectAreaToolBar;

    // Periodically updated extractor hit counts and histo sink entry counts.
    struct ObjectCounters
    {
        QVector<double> hitCounts;
    };

    QHash<Extractor *, ObjectCounters> m_extractorCounters;
    QHash<Histo1DSink *, ObjectCounters> m_histo1DSinkCounters;
    QHash<Histo2DSink *, ObjectCounters> m_histo2DSinkCounters;
    MVMEStreamProcessorCounters m_prevStreamProcessorCounters;

    double m_prevAnalysisTimeticks = 0.0;;

    void createView(const QUuid &eventId);
    DisplayLevelTrees createTrees(const QUuid &eventId, s32 level);
    DisplayLevelTrees createSourceTrees(const QUuid &eventId);
    void appendTreesToView(DisplayLevelTrees trees);
    void repopulate();

    void addUserLevel();
    void removeUserLevel();
    s32 getUserLevelForTree(QTreeWidget *tree);

    void doOperatorTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel);
    void doDisplayTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel);

    void modeChanged();
    void highlightValidInputNodes(QTreeWidgetItem *node);
    void highlightInputNodes(OperatorInterface *op);
    void highlightOutputNodes(PipeSourceInterface *ps);
    void clearToDefaultNodeHighlights(QTreeWidgetItem *node);
    void clearAllToDefaultNodeHighlights();
    void onNodeClicked(TreeNode *node, int column, s32 userLevel);
    void onNodeDoubleClicked(TreeNode *node, int column, s32 userLevel);
    void clearAllTreeSelections();
    void clearTreeSelectionsExcept(QTreeWidget *tree);
    void generateDefaultFilters(ModuleConfig *module);
    PipeDisplay *makeAndShowPipeDisplay(Pipe *pipe);
    void doPeriodicUpdate();
    void periodicUpdateExtractorCounters(double dt_s);
    void periodicUpdateHistoCounters(double dt_s);
    void periodicUpdateEventRate(double dt_s);

    // Returns the currentItem() of the tree widget that has focus.
    QTreeWidgetItem *getCurrentNode();
    void updateActions();

    void importForModuleFromTemplate();
    void importForModuleFromFile();
    void importForModule(ModuleConfig *module, const QString &startPath);
};

void EventWidgetPrivate::createView(const QUuid &eventId)
{
    auto analysis = m_context->getAnalysis();
    s32 maxUserLevel = 0;

    for (const auto &opEntry: analysis->getOperators(eventId))
    {
        maxUserLevel = std::max(maxUserLevel, opEntry.userLevel);
    }

    // Level 0: special case for data sources
    m_levelTrees.push_back(createSourceTrees(eventId));

    for (s32 userLevel = 1; userLevel <= maxUserLevel; ++userLevel)
    {
        auto trees = createTrees(eventId, userLevel);
        m_levelTrees.push_back(trees);
    }
}

DisplayLevelTrees make_displaylevel_trees(const QString &opTitle, const QString &dispTitle, s32 level)
{
    DisplayLevelTrees result = { new EventWidgetTree, new DisplayTree, level };

    result.operatorTree->setObjectName(opTitle);
    result.operatorTree->headerItem()->setText(0, opTitle);

    result.displayTree->setObjectName(dispTitle);
    result.displayTree->headerItem()->setText(0, dispTitle);

    for (auto tree: {result.operatorTree, reinterpret_cast<EventWidgetTree *>(result.displayTree)})
    {
        tree->setExpandsOnDoubleClick(false);
        tree->setItemDelegate(new HtmlDelegate(tree));
        tree->setSelectionMode(QAbstractItemView::SingleSelection);
        if (level > 0)
        {
            tree->setDragEnabled(true);
            tree->viewport()->setAcceptDrops(true);
            tree->setDropIndicatorShown(true);
            tree->setDragDropMode(QAbstractItemView::DragDrop);
        }
    }

    return result;
}

DisplayLevelTrees EventWidgetPrivate::createSourceTrees(const QUuid &eventId)
{
    auto analysis = m_context->getAnalysis();
    auto vmeConfig = m_context->getVMEConfig();

    auto eventConfig = vmeConfig->getEventConfig(eventId);
    auto modules = eventConfig->getModuleConfigs();

    DisplayLevelTrees result = make_displaylevel_trees(
        QSL("L0 Parameter Extraction"),
        QSL("L0 Raw Data Display"),
        0);

    // Populate the OperatorTree
    for (auto mod: modules)
    {
        QObject::disconnect(mod, &ConfigObject::modified, m_q, &EventWidget::repopulate);
        QObject::connect(mod, &ConfigObject::modified, m_q, &EventWidget::repopulate);
        auto moduleNode = makeModuleNode(mod);
        result.operatorTree->addTopLevelItem(moduleNode);
        moduleNode->setExpanded(true);

        for (auto sourceEntry: analysis->getSources(eventId, mod->getId()))
        {
            auto sourceNode = makeOperatorTreeSourceNode(sourceEntry.source.get());
            moduleNode->addChild(sourceNode);
        }
    }
    result.operatorTree->sortItems(0, Qt::AscendingOrder);

    // Populate the DisplayTree
    // Create module nodes and nodes for the raw histograms for each data source for the module.
    QSet<QObject *> sinksAddedBelowModules;
    auto opEntries = analysis->getOperators(eventId, 0);

    for (auto mod: modules)
    {
        auto moduleNode = makeModuleNode(mod);
        result.displayTree->addTopLevelItem(moduleNode);
        moduleNode->setExpanded(true);

        for (auto sourceEntry: analysis->getSources(eventId, mod->getId()))
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

DisplayLevelTrees EventWidgetPrivate::createTrees(const QUuid &eventId, s32 level)
{
    DisplayLevelTrees result = make_displaylevel_trees(
        QString(QSL("L%1 Processing")).arg(level),
        QString(QSL("L%1 Data Display")).arg(level),
        level);

    // Build a list of operators for the current level
    auto analysis = m_context->getAnalysis();
    QVector<Analysis::OperatorEntry> operators = analysis->getOperators(eventId, level);

    // Populate the OperatorTree
    for (auto entry: operators)
    {
        if(!qobject_cast<SinkInterface *>(entry.op.get()))
        {
            auto opNode = makeOperatorNode(entry.op.get());
            if (level > 0)
            {
                opNode->setFlags(opNode->flags() | Qt::ItemIsDragEnabled);
            }
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
        result.displayTree->histo1DRoot = histo1DRoot;
        result.displayTree->histo2DRoot = histo2DRoot;

        for (const auto &entry: operators)
        {
            TreeNode *theNode = nullptr;
            if (auto histoSink = qobject_cast<Histo1DSink *>(entry.op.get()))
            {
                auto histoNode = makeHisto1DNode(histoSink);
                histo1DRoot->addChild(histoNode);
                theNode = histoNode;
            }
            else if (auto histoSink = qobject_cast<Histo2DSink *>(entry.op.get()))
            {
                auto histoNode = makeHisto2DNode(histoSink);
                histo2DRoot->addChild(histoNode);
                theNode = histoNode;
            }
            else if (auto sink = qobject_cast<SinkInterface *>(entry.op.get()))
            {
                auto sinkNode = makeSinkNode(sink);
                result.displayTree->addTopLevelItem(sinkNode);
                theNode = sinkNode;
            }

            if (theNode && level > 0)
            {
                theNode->setFlags(theNode->flags() | Qt::ItemIsDragEnabled);
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

    for (auto tree: {opTree, reinterpret_cast<EventWidgetTree *>(dispTree)})
    {
        //tree->installEventFilter(m_q);

        tree->m_eventWidget = m_q;
        tree->m_userLevel = levelIndex;

        QObject::connect(tree, &QTreeWidget::itemClicked, m_q, [this, levelIndex] (QTreeWidgetItem *node, int column) {
            onNodeClicked(reinterpret_cast<TreeNode *>(node), column, levelIndex);
            updateActions();
        });

        QObject::connect(tree, &QTreeWidget::itemDoubleClicked, m_q, [this, levelIndex] (QTreeWidgetItem *node, int column) {
            onNodeDoubleClicked(reinterpret_cast<TreeNode *>(node), column, levelIndex);
        });


        QObject::connect(tree, &QTreeWidget::currentItemChanged, m_q,
                         [this, tree](QTreeWidgetItem *current, QTreeWidgetItem *previous) {
            if (current)
            {
                clearTreeSelectionsExcept(tree);
            }
            updateActions();
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
    if (!m_eventId.isNull())
    {
        // This populates m_d->m_levelTrees
        createView(m_eventId);
    }

    for (auto trees: m_levelTrees)
    {
        // This populates the operator and display splitters
        appendTreesToView(trees);
    }

    s32 levelsToAdd = m_manualUserLevel - m_levelTrees.size();

    for (s32 i = 0; i < levelsToAdd; ++i)
    {
        s32 levelIndex = m_levelTrees.size();
        auto trees = createTrees(m_eventId, levelIndex);
        m_levelTrees.push_back(trees);
        appendTreesToView(trees);
    }

    if (splitterSizes.size() == m_operatorFrameSplitter->count())
    {
        // Restore the splitter sizes. As the splitters are synced via
        // splitterMoved() they both had the same sizes before.
        m_operatorFrameSplitter->setSizes(splitterSizes);
        m_displayFrameSplitter->setSizes(splitterSizes);
    }

    m_hiddenUserLevels.resize(m_levelTrees.size());

    for (s32 idx = 0; idx < m_hiddenUserLevels.size(); ++idx)
    {
        m_levelTrees[idx].operatorTree->setVisible(!m_hiddenUserLevels[idx]);
        m_levelTrees[idx].displayTree->setVisible(!m_hiddenUserLevels[idx]);
    }

    expandObjectNodes(m_levelTrees, m_expandedObjects);
    clearAllToDefaultNodeHighlights();
    updateActions();
}

void EventWidgetPrivate::addUserLevel()
{
    s32 levelIndex = m_levelTrees.size();
    auto trees = createTrees(m_eventId, levelIndex);
    m_levelTrees.push_back(trees);
    appendTreesToView(trees);
    m_manualUserLevel = levelIndex + 1;
}

void EventWidgetPrivate::removeUserLevel()
{
    Q_ASSERT(m_levelTrees.size() > 1);
    auto trees = m_levelTrees.last();
    m_levelTrees.pop_back();
    delete trees.operatorTree;
    delete trees.displayTree;
    m_manualUserLevel = m_levelTrees.size();
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
    auto menuNew = new QMenu(&menu);
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
                        m_uniqueWidget = widget;
                        clearAllTreeSelections();
                        clearAllToDefaultNodeHighlights();
                    });
                };

                auto analysis = m_context->getAnalysis();
                auto &registry(analysis->getRegistry());

                QVector<SourcePtr> sourceInstances;

                for (auto sourceName: registry.getSourceNames())
                {
                    SourcePtr src(registry.makeSource(sourceName));
                    sourceInstances.push_back(src);
                }

                // Sort sources by displayname
                qSort(sourceInstances.begin(), sourceInstances.end(), [](const SourcePtr &a, const SourcePtr &b) {
                    return a->getDisplayName() < b->getDisplayName();
                });

                for (auto src: sourceInstances)
                {
                    add_action(src->getDisplayName(), src);
                }

                // default data filters and "raw display" creation
                if (moduleConfig)
                {
                    auto defaultExtractors = get_default_data_extractors(moduleConfig->getModuleMeta().typeName);

                    if (!defaultExtractors.isEmpty())
                    {
                        menu.addAction(QSL("Generate default filters"), [this, moduleConfig] () {

                            QMessageBox box(QMessageBox::Question,
                                            QSL("Generate default filters"),
                                            QSL("This action will generate extraction filters, calibrations and histograms"
                                                "for the selected module. Do you want to continue?"),
                                            QMessageBox::Ok | QMessageBox::No,
                                            m_q
                                           );
                            box.button(QMessageBox::Ok)->setText("Yes, generate filters");

                            if (box.exec() == QMessageBox::Ok)
                            {
                                generateDefaultFilters(moduleConfig);
                            }
                        });
                    }

                    auto menuImport = new QMenu(&menu);
                    menuImport->setTitle(QSL("Import"));
                    //menuImport->setIcon(QIcon(QSL(":/analysis_module_import.png")));
                    menuImport->addAction(m_actionImportForModuleFromTemplate.get());
                    menuImport->addAction(m_actionImportForModuleFromFile.get());
                    menu.addMenu(menuImport);
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
                    makeAndShowPipeDisplay(pipe);
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
                            m_uniqueWidget = widget;
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
                makeAndShowPipeDisplay(pipe);
            });
        }

        if (userLevel > 0 && node->type() == NodeType_Operator)
        {
            if (!m_uniqueWidgetActive)
            {
                auto op = getPointer<OperatorInterface>(node);
                Q_ASSERT(op);

                if (op->getNumberOfOutputs() == 1)
                {
                    Pipe *pipe = op->getOutput(0);

                    menu.addAction(QSL("Show Parameters"), [this, pipe]() {
                        makeAndShowPipeDisplay(pipe);
                    });
                }

                menu.addAction(QSL("Edit"), [this, userLevel, op]() {
                    auto widget = new AddEditOperatorWidget(op, userLevel, m_q);
                    widget->move(QCursor::pos());
                    widget->setAttribute(Qt::WA_DeleteOnClose);
                    widget->show();
                    m_uniqueWidgetActive = true;
                    m_uniqueWidget = widget;
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
                        m_uniqueWidget = widget;
                        clearAllTreeSelections();
                        clearAllToDefaultNodeHighlights();
                    });
                };

                auto analysis = m_context->getAnalysis();
                auto &registry(analysis->getRegistry());
                QVector<OperatorPtr> operatorInstances;

                for (auto operatorName: registry.getOperatorNames())
                {
                    OperatorPtr op(registry.makeOperator(operatorName));
                    operatorInstances.push_back(op);
                }

                // Sort operators by displayname
                qSort(operatorInstances.begin(), operatorInstances.end(), [](const OperatorPtr &a, const OperatorPtr &b) {
                    return a->getDisplayName() < b->getDisplayName();
                });

                for (auto op: operatorInstances)
                {
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
            m_uniqueWidget = widget;
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
                    Q_ASSERT(widgetInfo.sink);

                    if (widgetInfo.histoAddress < widgetInfo.histos.size())
                    {
                        menu.addAction(QSL("Open Histogram"), m_q, [this, widgetInfo]() {

                            Histo1D *histo = widgetInfo.histos[widgetInfo.histoAddress].get();

                            if (!m_context->hasObjectWidget(histo) || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                            {
                                auto widget = new Histo1DWidget(widgetInfo.histos[widgetInfo.histoAddress]);
                                widget->setContext(m_context);

                                if (widgetInfo.calib)
                                {
                                    widget->setCalibrationInfo(widgetInfo.calib, widgetInfo.histoAddress);
                                }

                                {
                                    auto context = m_context;
                                    widget->setSink(widgetInfo.sink, [context] (const std::shared_ptr<Histo1DSink> &sink) {
                                        context->analysisOperatorEdited(sink);
                                    });
                                }

                                m_context->addObjectWidget(widget, histo,
                                                           widgetInfo.sink->getId().toString()
                                                           + QSL("_")
                                                           + QString::number(widgetInfo.histoAddress));
                            }
                            else
                            {
                                m_context->activateObjectWidget(histo);
                            }
                        });
                    }
                } break;

            case NodeType_Histo1DSink:
                {
                    Histo1DWidgetInfo widgetInfo = getHisto1DWidgetInfoFromNode(node);
                    Q_ASSERT(widgetInfo.sink);

                    if (widgetInfo.histoAddress < widgetInfo.histos.size())
                    {

                        menu.addAction(QSL("Open 1D List View"), m_q, [this, widgetInfo]() {

                            if (!m_context->hasObjectWidget(widgetInfo.sink.get()) || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                            {
                                auto widget = new Histo1DListWidget(widgetInfo.histos);
                                widget->setContext(m_context);

                                if (widgetInfo.calib)
                                {
                                    widget->setCalibration(widgetInfo.calib);
                                }

                                {
                                    auto context = m_context;
                                    widget->setSink(widgetInfo.sink, [context] (const std::shared_ptr<Histo1DSink> &sink) {
                                        context->analysisOperatorEdited(sink);
                                    });
                                }

                                m_context->addObjectWidget(widget, widgetInfo.sink.get(), widgetInfo.sink->getId().toString());
                            }
                            else
                            {
                                m_context->activateObjectWidget(widgetInfo.sink.get());
                            }
                        });
                    }


                    if (widgetInfo.histos.size())
                    {
                        menu.addAction(QSL("Open 2D Combined View"), m_q, [this, widgetInfo]() {
                            auto widget = new Histo2DWidget(widgetInfo.sink, m_context);
                            widget->setContext(m_context);
                            m_context->addWidget(widget, widgetInfo.sink->getId().toString() + QSL("_2dCombined"));
                        });
                    }
                } break;

            case NodeType_Histo2DSink:
                {
                    if (auto histoSink = qobject_cast<Histo2DSink *>(obj))
                    {
                        auto histo = histoSink->m_histo;
                        if (histo)
                        {
                            auto sinkPtr = std::dynamic_pointer_cast<Histo2DSink>(histoSink->getSharedPointer());
                            menu.addAction(QSL("Open"), m_q, [this, histo, sinkPtr, userLevel]() {

                                if (!m_context->hasObjectWidget(sinkPtr.get()) || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                                {
                                    auto histoPtr = sinkPtr->m_histo;
                                    auto widget = new Histo2DWidget(histoPtr);

                                    auto context = m_context;
                                    auto eventId = m_eventId;

                                    widget->setSink(sinkPtr,
                                                    // addSinkCallback
                                                    [context, eventId, userLevel] (const std::shared_ptr<Histo2DSink> &sink) {
                                                        context->addAnalysisOperator(eventId, sink, userLevel);
                                                    },
                                                    // sinkModifiedCallback
                                                    [context] (const std::shared_ptr<Histo2DSink> &sink) {
                                                        context->analysisOperatorEdited(sink);
                                                    },
                                                    // makeUniqueOperatorNameFunction
                                                    [context] (const QString &name) {
                                                        return make_unique_operator_name(context->getAnalysis(), name);
                                                    });

                                    widget->setContext(m_context);

                                    m_context->addObjectWidget(widget, sinkPtr.get(), sinkPtr->getId().toString());
                                }
                                else
                                {
                                    m_context->activateObjectWidget(sinkPtr.get());
                                }
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
                    m_uniqueWidget = widget;
                    clearAllTreeSelections();
                    clearAllToDefaultNodeHighlights();
                });

                menu.addAction(QSL("Remove"), [this, op]() {
                    // TODO: QMessageBox::question or similar as there's no way to undo the action
                    m_q->removeOperator(op);
                });
            }
        }

        if (userLevel > 0 && !m_uniqueWidgetActive)
        {
            auto displayTree = m_levelTrees[userLevel].displayTree;
            Q_ASSERT(displayTree->topLevelItemCount() >= 2);
            Q_ASSERT(displayTree->histo1DRoot);
            Q_ASSERT(displayTree->histo2DRoot);

            auto histo1DRoot = displayTree->histo1DRoot;
            auto histo2DRoot = displayTree->histo2DRoot;

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
                auto analysis = m_context->getAnalysis();
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

    updateActions();
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
        // highlightInputNodes()...
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

void EventWidgetPrivate::onNodeClicked(TreeNode *node, int column, s32 userLevel)
{
    clearTreeSelectionsExcept(node->treeWidget());

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

void EventWidgetPrivate::onNodeDoubleClicked(TreeNode *node, int column, s32 userLevel)
{
    if (m_mode == Default)
    {
        switch (node->type())
        {
            case NodeType_Histo1D:
                {
                    Histo1DWidgetInfo widgetInfo = getHisto1DWidgetInfoFromNode(node);
                    Q_ASSERT(widgetInfo.sink);

                    if (widgetInfo.histoAddress < widgetInfo.histos.size())
                    {
                        Histo1D *histo = widgetInfo.histos[widgetInfo.histoAddress].get();

                        if (!m_context->hasObjectWidget(histo) || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                        {
                            auto widget = new Histo1DWidget(widgetInfo.histos[widgetInfo.histoAddress]);
                            widget->setContext(m_context);

                            if (widgetInfo.calib)
                            {
                                widget->setCalibrationInfo(widgetInfo.calib, widgetInfo.histoAddress);
                            }

                            {
                                auto context = m_context;
                                widget->setSink(widgetInfo.sink, [context] (const std::shared_ptr<Histo1DSink> &sink) {
                                    context->analysisOperatorEdited(sink);
                                });
                            }

                            m_context->addObjectWidget(widget, histo,
                                                       widgetInfo.sink->getId().toString()
                                                       + QSL("_")
                                                       + QString::number(widgetInfo.histoAddress));
                        }
                        else
                        {
                            m_context->activateObjectWidget(histo);
                        }
                    }
                } break;

            case NodeType_Histo1DSink:
                {
                    Histo1DWidgetInfo widgetInfo = getHisto1DWidgetInfoFromNode(node);
                    Q_ASSERT(widgetInfo.sink);

                    if (widgetInfo.histos.size())
                    {
                        if (!m_context->hasObjectWidget(widgetInfo.sink.get()) || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                        {
                            auto widget = new Histo1DListWidget(widgetInfo.histos);
                            widget->setContext(m_context);

                            if (widgetInfo.calib)
                            {
                                widget->setCalibration(widgetInfo.calib);
                            }

                            {
                                auto context = m_context;
                                widget->setSink(widgetInfo.sink, [context] (const std::shared_ptr<Histo1DSink> &sink) {
                                    context->analysisOperatorEdited(sink);
                                });
                            }

                            m_context->addObjectWidget(widget, widgetInfo.sink.get(), widgetInfo.sink->getId().toString());
                        }
                        else
                        {
                            m_context->activateObjectWidget(widgetInfo.sink.get());
                        }
                    }
                } break;

            case NodeType_Histo2DSink:
                {
                    auto sinkPtr = std::dynamic_pointer_cast<Histo2DSink>(getPointer<Histo2DSink>(node)->getSharedPointer());

                    if (!m_context->hasObjectWidget(sinkPtr.get()) || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                    {
                        auto histoPtr = sinkPtr->m_histo;
                        auto widget = new Histo2DWidget(histoPtr);

                        auto context = m_context;
                        auto eventId = m_eventId;

                        widget->setSink(sinkPtr,
                                        // addSinkCallback
                                        [context, eventId, userLevel] (const std::shared_ptr<Histo2DSink> &sink) {
                                            context->addAnalysisOperator(eventId, sink, userLevel);
                                        },
                                        // sinkModifiedCallback
                                        [context] (const std::shared_ptr<Histo2DSink> &sink) {
                                            context->analysisOperatorEdited(sink);
                                        },
                                        // makeUniqueOperatorNameFunction
                                        [context] (const QString &name) {
                                            return make_unique_operator_name(context->getAnalysis(), name);
                                        });

                        widget->setContext(m_context);

                        m_context->addObjectWidget(widget, sinkPtr.get(), sinkPtr->getId().toString());
                    }
                    else
                    {
                        m_context->activateObjectWidget(sinkPtr.get());
                    }
                } break;
        }
    }
}

void EventWidgetPrivate::clearAllTreeSelections()
{
    for (DisplayLevelTrees trees: m_levelTrees)
    {
        for (auto tree: {trees.operatorTree, reinterpret_cast<EventWidgetTree *>(trees.displayTree)})
        {
            tree->clearSelection();
        }
    }
}

void EventWidgetPrivate::clearTreeSelectionsExcept(QTreeWidget *treeNotToClear)
{
    for (DisplayLevelTrees trees: m_levelTrees)
    {
        for (auto tree: {trees.operatorTree, reinterpret_cast<EventWidgetTree *>(trees.displayTree)})
        {
            if (tree != treeNotToClear)
            {
                tree->clearSelection();
            }
        }
    }
}

void EventWidgetPrivate::generateDefaultFilters(ModuleConfig *module)
{
    AnalysisPauser pauser(m_context);

    auto defaultFilters = get_default_data_extractors(module->getModuleMeta().typeName);

    for (auto &ex: defaultFilters)
    {
        auto dataFilter = ex->getFilter();
        double unitMin = 0.0;
        double unitMax = std::pow(2.0, dataFilter.getDataBits());
        QString name = module->getModuleMeta().typeName + QSL(".") + ex->objectName().section('.', 0, -1);

        RawDataDisplay rawDataDisplay = make_raw_data_display(dataFilter, unitMin, unitMax,
                                                              name,
                                                              ex->objectName().section('.', 0, -1),
                                                              QString());

        add_raw_data_display(m_context->getAnalysis(), m_eventId, module->getId(), rawDataDisplay);
    }

    repopulate();
}

PipeDisplay *EventWidgetPrivate::makeAndShowPipeDisplay(Pipe *pipe)
{
    auto widget = new PipeDisplay(m_context->getAnalysis(), pipe, m_q);
    QObject::connect(m_displayRefreshTimer, &QTimer::timeout, widget, &PipeDisplay::refresh);
    QObject::connect(pipe->source, &QObject::destroyed, widget, &QWidget::close);
    add_widget_close_action(widget);
    widget->move(QCursor::pos());
    widget->setAttribute(Qt::WA_DeleteOnClose);
    widget->show();
    return widget;
}

void EventWidgetPrivate::doPeriodicUpdate()
{
    /* If it's a replay: use timeticks
     * If it's DAQ: use elapsed walltime
     * Reason: if analysis efficiency is < 1.0 timeticks will be lost. Thus
     * using timeticks with a DAQ run may lead to very confusing numbers as
     * sometimes ticks will be lost, at other times they'll appear.
     */

    auto analysis = m_context->getAnalysis();
    bool isReplay = analysis->getRunInfo().isReplay;
    double dt_s = 0.0;
    double currentAnalysisTimeticks = analysis->getTimetickCount();

    if (isReplay)
    {
        dt_s = calc_delta0(currentAnalysisTimeticks, m_prevAnalysisTimeticks);
    }
    else
    {
        dt_s = PeriodicUpdateTimerInterval_ms / 1000.0;
    }

    periodicUpdateExtractorCounters(dt_s);
    periodicUpdateHistoCounters(dt_s);
    periodicUpdateEventRate(dt_s);

    m_prevAnalysisTimeticks = currentAnalysisTimeticks;
}

void EventWidgetPrivate::periodicUpdateExtractorCounters(double dt_s)
{
#if ANALYSIS_USE_A2
    auto analysis = m_context->getAnalysis();
    auto a2State = analysis->getA2AdapterState();
#endif

    //
    // level 0: operator tree (Extractor hitcounts)
    //
    for (auto iter = QTreeWidgetItemIterator(m_levelTrees[0].operatorTree);
         *iter; ++iter)
    {
        auto node(*iter);

        if (node->type() == NodeType_Source)
        {
            auto extractor = qobject_cast<Extractor *>(getPointer<PipeSourceInterface>(node));

            if (!extractor)
                continue;

            if (extractor->getOutput(0)->getSize() != node->childCount())
                continue;

#if ANALYSIS_USE_A2
            if (!a2State)
                continue;

            auto ex_a2 = a2State->sourceMap.value(extractor, nullptr);

            if (!ex_a2)
                continue;

            auto hitCounts = to_qvector(ex_a2->hitCounts);
#else
            auto hitCounts = extractor->getHitCounts();
#endif

            auto &prevHitCounts = m_extractorCounters[extractor].hitCounts;

            prevHitCounts.resize(hitCounts.size());

            auto hitCountDeltas = calc_deltas0(hitCounts, prevHitCounts);
            auto hitCountRates = hitCountDeltas;
            std::for_each(hitCountRates.begin(), hitCountRates.end(), [dt_s](double &d) { d /= dt_s; });

            Q_ASSERT(hitCounts.size() == node->childCount());

            for (s32 addr = 0; addr < node->childCount(); ++addr)
            {
                Q_ASSERT(node->child(addr)->type() == NodeType_OutputPipeParameter);

                QString addrString = QString("%1").arg(addr, 2).replace(QSL(" "), QSL("&nbsp;"));

                double hitCount = hitCounts[addr];
                auto childNode = node->child(addr);

                if (hitCount <= 0.0)
                {
                    childNode->setText(0, addrString);
                }
                else
                {
                    auto rateString = format_number(hitCountRates[addr], QSL("cps"), UnitScaling::Decimal,
                                                    0, 'g', 3);

                    childNode->setText(0, QString("%1 (hits=%2, rate=%3, dt=%4 s)")
                                       .arg(addrString)
                                       .arg(hitCount)
                                       .arg(rateString)
                                       .arg(dt_s)
                                      );
                }
            }

            prevHitCounts = hitCounts;
        }
    }
}

void EventWidgetPrivate::periodicUpdateHistoCounters(double dt_s)
{
#if ANALYSIS_USE_A2
    auto analysis = m_context->getAnalysis();
    auto a2State = analysis->getA2AdapterState();
#endif

    //
    // level > 0: display trees (histo counts)
    //
    for (auto trees: m_levelTrees)
    {
        for (auto iter = QTreeWidgetItemIterator(trees.displayTree);
             *iter; ++iter)
        {
            auto node(*iter);

            if (node->type() == NodeType_Histo1DSink)
            {
                auto histoSink = qobject_cast<Histo1DSink *>(getPointer<OperatorInterface>(node));

                if (!histoSink)
                    continue;

                if (histoSink->m_histos.size() != node->childCount())
                    continue;

                QVector<double> entryCounts;

#if ANALYSIS_USE_A2
                if (a2State)
                {
                    if (auto a2_sink = a2State->operatorMap.value(histoSink, nullptr))
                    {
                        auto sinkData = reinterpret_cast<a2::H1DSinkData *>(a2_sink->d);

                        entryCounts.reserve(sinkData->histos.size);

                        for (s32 i = 0; i < sinkData->histos.size; i++)
                        {
                            entryCounts.push_back(sinkData->histos[i].entryCount);
                        }
                    }
                }
#else
                entryCounts.reserve(histoSink->m_histos.size());

                for (const auto &histo: histoSink->m_histos)
                {
                    entryCounts.push_back(histo->getEntryCount());
                }
#endif

                auto &prevEntryCounts = m_histo1DSinkCounters[histoSink].hitCounts;

                prevEntryCounts.resize(entryCounts.size());

                auto entryCountDeltas = calc_deltas0(entryCounts, prevEntryCounts);
                auto entryCountRates = entryCountDeltas;
                std::for_each(entryCountRates.begin(), entryCountRates.end(), [dt_s](double &d) { d /= dt_s; });

                auto maxCount = std::min(entryCounts.size(), node->childCount());

                for (s32 addr = 0; addr < maxCount; ++addr)
                {
                    Q_ASSERT(node->child(addr)->type() == NodeType_Histo1D);

                    QString numberString = QString("%1").arg(addr, 2).replace(QSL(" "), QSL("&nbsp;"));

                    double entryCount = entryCounts[addr];
                    auto childNode = node->child(addr);

                    if (entryCount <= 0.0)
                    {
                        childNode->setText(0, numberString);
                    }
                    else
                    {
                        auto rateString = format_number(entryCountRates[addr], QSL("cps"), UnitScaling::Decimal,
                                                        0, 'g', 3);

                        childNode->setText(0, QString("%1 (entries=%2, rate=%3, dt=%4 s)")
                                           .arg(numberString)
                                           .arg(entryCount, 0, 'g', 3)
                                           .arg(rateString)
                                           .arg(dt_s)
                                          );
                    }
                }

                prevEntryCounts = entryCounts;
            }
            else if (node->type() == NodeType_Histo2DSink)
            {
                auto sink = getPointer<Histo2DSink>(node);
                auto histo = sink->m_histo;

                if (histo)
                {
                    double entryCount = 0.0;
#if ANALYSIS_USE_A2
                    if (auto a2_sink = a2State->operatorMap.value(sink, nullptr))
                    {
                        auto sinkData = reinterpret_cast<a2::H2DSinkData *>(a2_sink->d);

                        entryCount = sinkData->histo.entryCount;
                    }
#else
                    entryCount = histo->getEntryCount();
#endif
                    auto &prevEntryCounts = m_histo2DSinkCounters[sink].hitCounts;
                    prevEntryCounts.resize(1);

                    double prevEntryCount = prevEntryCounts[0];

                    double countDelta = calc_delta0(entryCount, prevEntryCount);
                    double countRate = countDelta / dt_s;

                    if (entryCount <= 0.0)
                    {
                        node->setText(0, QString("<b>%1</b> %2")
                                      .arg(sink->getShortName())
                                      .arg(sink->objectName())
                                     );
                    }
                    else
                    {
                        auto rateString = format_number(countRate, QSL("cps"), UnitScaling::Decimal,
                                                        0, 'g', 3);

                        node->setText(0, QString("<b>%1</b> %2 (entries=%3, rate=%4, dt=%5)")
                                      .arg(sink->getShortName())
                                      .arg(sink->objectName())
                                      .arg(entryCount, 0, 'g', 3)
                                      .arg(rateString)
                                      .arg(dt_s)
                                     );
                    }

                    prevEntryCounts[0] = entryCount;
                }
            }
        }
    }

}

void EventWidgetPrivate::periodicUpdateEventRate(double dt_s)
{
    auto &prevCounters(m_prevStreamProcessorCounters);
    const auto &counters(m_context->getMVMEStreamWorker()->getCounters());
    Q_ASSERT(0 <= m_eventIndex && m_eventIndex < (s32)counters.eventCounters.size());

    /* Use the counters of the first module in this event as that represents
     * the event rate after multi-event splitting. */
    double deltaEvents = calc_delta0(
        counters.moduleCounters[m_eventIndex][0],
        prevCounters.moduleCounters[m_eventIndex][0]);

    double eventCount = counters.moduleCounters[m_eventIndex][0];
    double eventRate = deltaEvents / dt_s;

    auto labelText = (QString("count=%1\nrate=%2")
                      .arg(format_number(eventCount, QSL(""), UnitScaling::Decimal))
                      .arg(format_number(eventRate, QSL("cps"), UnitScaling::Decimal, 0, 'g', 3))
                     );

    if (m_context->getAnalysis()->getRunInfo().isReplay)
    {
        double walltimeRate = deltaEvents / (PeriodicUpdateTimerInterval_ms / 1000.0);

        labelText += (QString("\nreplayRate=%1")
                      .arg(format_number(walltimeRate, QSL("cps"), UnitScaling::Decimal, 0, 'g', 3))
                      );
    }

    m_eventRateLabel->setText(labelText);

    prevCounters = counters;
}


QTreeWidgetItem *EventWidgetPrivate::getCurrentNode()
{
    QTreeWidgetItem *result = nullptr;

    if (auto activeTree = qobject_cast<QTreeWidget *>(m_q->focusWidget()))
    {
        result = activeTree->currentItem();
    }

    return result;
}

void EventWidgetPrivate::updateActions()
{
    auto node = getCurrentNode();

    m_actionModuleImport->setEnabled(false);
    m_actionImportForModuleFromTemplate->setEnabled(false);
    m_actionImportForModuleFromFile->setEnabled(false);

    if (m_mode == Default && node && node->type() == NodeType_Module)
    {
        if (auto module = getPointer<ModuleConfig>(node))
        {
            m_actionModuleImport->setEnabled(true);
            m_actionImportForModuleFromTemplate->setEnabled(true);
            m_actionImportForModuleFromFile->setEnabled(true);
        }
    }
}

void EventWidgetPrivate::importForModuleFromTemplate()
{
    auto node = getCurrentNode();

    if (node && node->type() == NodeType_Module)
    {
        if (auto module = getPointer<ModuleConfig>(node))
        {
            QString path = vats::get_module_path(module->getModuleMeta().typeName) + QSL("/analysis");
            importForModule(module, path);
        }
    }
}

void EventWidgetPrivate::importForModuleFromFile()
{
    auto node = getCurrentNode();

    if (node && node->type() == NodeType_Module)
    {
        if (auto module = getPointer<ModuleConfig>(node))
        {
            importForModule(module, m_context->getWorkspaceDirectory());
        }
    }
}

/* Importing of module specific analysis objects
 * - Module must be given
 * - Let user pick a file starting in the given startPath
 * - Load analysis from file
 * - Generate new IDs for analysis objects
 * - Try auto assignment but using only the ModuleInfo from the given target module
 * - If auto assignment fails run the assigment gui also using only info for the selected module.
 * - Remove analysis objects not related to the target module.
 * - Add the remaining objects into the existing analysis
 */
void EventWidgetPrivate::importForModule(ModuleConfig *module, const QString &startPath)
{
    auto event = qobject_cast<EventConfig *>(module->parent());

    if (!event)
        return;

    QString fileName = QFileDialog::getOpenFileName(m_q, QSL("Import analysis objects"), startPath, AnalysisFileFilter);

    if (fileName.isEmpty())
        return;

    QJsonDocument doc(gui_read_json_file(fileName));
    auto json = doc.object()[QSL("AnalysisNG")].toObject();

    Analysis analysis;
    auto readResult = analysis.read(json, m_context->getVMEConfig());

    if (!readResult)
    {
        readResult.errorData["Source file"] = fileName;
        QMessageBox::critical(m_q,
                              QSL("Error importing analysis"),
                              readResult.toRichText());
        return;
    }

    using namespace vme_analysis_common;

    ModuleInfo moduleInfo;
    moduleInfo.id = module->getId();
    moduleInfo.typeName = module->getModuleMeta().typeName;
    moduleInfo.name = module->objectName();
    moduleInfo.eventId = event->getId();

    generate_new_object_ids(&analysis);

    if (!auto_assign_vme_modules({moduleInfo}, &analysis))
    {
        if (!run_vme_analysis_module_assignment_ui({moduleInfo}, &analysis))
            return;
    }

    remove_analysis_objects_unless_matching(&analysis, moduleInfo);

    m_context->logMessage(QString("Importing %1").arg(info_string(&analysis)));

    if (analysis.isEmpty())
        return;

    auto sources = analysis.getSources();
    auto operators = analysis.getOperators();

    AnalysisPauser pauser(m_context);
    auto targetAnalysis = m_context->getAnalysis();

    s32 baseUserLevel = targetAnalysis->getMaxUserLevel(moduleInfo.eventId);

    for (auto entry: sources)
    {
        Q_ASSERT(entry.eventId == moduleInfo.eventId);
        Q_ASSERT(entry.moduleId == moduleInfo.id);
        targetAnalysis->addSource(entry.eventId, entry.moduleId, entry.source);
    }

    for (auto entry: operators)
    {
        Q_ASSERT(entry.eventId == moduleInfo.eventId);

        s32 targetUserLevel = baseUserLevel + entry.userLevel;

        if (entry.userLevel == 0 && qobject_cast<SinkInterface *>(entry.op.get()))
        {
            targetUserLevel = 0;
        }

        targetAnalysis->addOperator(entry.eventId, entry.op, targetUserLevel);
    }

    repopulate();
}

static const u32 EventWidgetPeriodicRefreshInterval_ms = 1000;

void run_userlevel_visibility_dialog(QVector<bool> &hiddenLevels, QWidget *parent = 0)
{
    auto listWidget = new QListWidget;

    for (s32 idx = 0; idx < hiddenLevels.size(); ++idx)
    {
        auto item = new QListWidgetItem(QString("Level %1").arg(idx));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(hiddenLevels[idx] ? Qt::Unchecked : Qt::Checked);
        listWidget->addItem(item);
    }

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto layout = new QVBoxLayout;
    layout->addWidget(listWidget);
    layout->addWidget(buttonBox);
    layout->setStretch(0, 1);

    QDialog dialog(parent);
    dialog.setWindowTitle(QSL("Select processing levels to show"));
    dialog.setLayout(layout);
    add_widget_close_action(&dialog);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted)
    {
        for (s32 idx = 0; idx < listWidget->count(); ++idx)
        {
            auto item = listWidget->item(idx);
            hiddenLevels[idx] = (item->checkState() == Qt::Unchecked);
        }
    }
}

EventWidget::EventWidget(MVMEContext *ctx, const QUuid &eventId, int eventIndex,
                         AnalysisWidget *analysisWidget, QWidget *parent)
    : QWidget(parent)
    , m_d(new EventWidgetPrivate)
{
    qDebug() << __PRETTY_FUNCTION__ << this << "event =" << eventId;
    *m_d = {};
    m_d->m_q = this;
    m_d->m_context = ctx;
    m_d->m_eventId = eventId;
    m_d->m_eventIndex = eventIndex;
    m_d->m_analysisWidget = analysisWidget;
    m_d->m_displayRefreshTimer = new QTimer(this);
    m_d->m_displayRefreshTimer->start(EventWidgetPeriodicRefreshInterval_ms);

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
    m_d->m_operatorFrameSplitter->setChildrenCollapsible(false);
    operatorFrameLayout->addWidget(m_d->m_operatorFrameSplitter);

    m_d->m_displayFrameSplitter = new QSplitter;
    m_d->m_displayFrameSplitter->setChildrenCollapsible(false);
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


    /* ToolBar creation. Note that these toolbars are not directly added to the
     * widget but instead they're handled by AnalysisWidget via getToolBar()
     * and getEventSelectAreaToolBar(). */

    // Upper ToolBar actions

    m_d->m_actionImportForModuleFromTemplate = std::make_unique<QAction>("Import from template");
    m_d->m_actionImportForModuleFromFile     = std::make_unique<QAction>("Import from file");
    m_d->m_actionModuleImport = new QWidgetAction(this);
    {
        auto menu = new QMenu(this);
        menu->addAction(m_d->m_actionImportForModuleFromTemplate.get());
        menu->addAction(m_d->m_actionImportForModuleFromFile.get());

        auto toolButton = new QToolButton;
        toolButton->setMenu(menu);
        toolButton->setPopupMode(QToolButton::InstantPopup);
        toolButton->setIcon(QIcon(QSL(":/analysis_module_import.png")));
        toolButton->setText(QSL("Import module objects"));
        toolButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        auto font = toolButton->font();
        font.setPointSize(7);
        toolButton->setFont(font);

        m_d->m_actionModuleImport->setDefaultWidget(toolButton);
    }

    connect(m_d->m_actionImportForModuleFromTemplate.get(), &QAction::triggered, this, [this] {
        m_d->importForModuleFromTemplate();
    });

    connect(m_d->m_actionImportForModuleFromFile.get(), &QAction::triggered, this, [this] {
        m_d->importForModuleFromFile();
    });

    // create the upper toolbar
    {
        m_d->m_upperToolBar = make_toolbar();
        auto tb = m_d->m_upperToolBar;

        //tb->addWidget(new QLabel(QString("Hello, event! %1").arg((uintptr_t)this)));
    }

    // Lower ToolBar, to the right of the event selection combo
    m_d->m_actionSelectVisibleLevels = new QAction(QIcon(QSL(":/eye_pencil.png")), QSL("Level Visiblity"), this);

    connect(m_d->m_actionSelectVisibleLevels, &QAction::triggered, this, [this] {

        m_d->m_hiddenUserLevels.resize(m_d->m_levelTrees.size());

        run_userlevel_visibility_dialog(m_d->m_hiddenUserLevels, this);

        for (s32 idx = 0; idx < m_d->m_hiddenUserLevels.size(); ++idx)
        {
            m_d->m_levelTrees[idx].operatorTree->setVisible(!m_d->m_hiddenUserLevels[idx]);
            m_d->m_levelTrees[idx].displayTree->setVisible(!m_d->m_hiddenUserLevels[idx]);
        }
    });

    m_d->m_eventRateLabel = new QLabel;

    // create the lower toolbar
    {
        m_d->m_eventSelectAreaToolBar = make_toolbar();
        auto tb = m_d->m_eventSelectAreaToolBar;

        tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        tb->addAction(m_d->m_actionSelectVisibleLevels);
        tb->addSeparator();
        tb->addWidget(m_d->m_eventRateLabel);
    }

    m_d->repopulate();
}

EventWidget::~EventWidget()
{
    qDebug() << __PRETTY_FUNCTION__ << this << "event =" << m_d->m_eventId;

    if (m_d->m_uniqueWidgetActive)
    {
        if (auto dialog = qobject_cast<QDialog *>(m_d->m_uniqueWidget))
        {
            dialog->reject();
        }
    }

    delete m_d;
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

void EventWidget::highlightInputOf(Slot *slot, bool doHighlight)
{
    if (!slot || !slot->isParamIndexInRange())
        return;

    QTreeWidgetItem *node = nullptr;

    if (auto source = qobject_cast<SourceInterface *>(slot->inputPipe->getSource()))
    {
        // As the input is a SourceInterface we only need to look in the source tree
        auto tree = m_d->m_levelTrees[0].operatorTree;

        node = findFirstNode(tree->invisibleRootItem(), [source](auto nodeToTest) {
            return (nodeToTest->type() == NodeType_Source
                    && getPointer<SourceInterface>(nodeToTest) == source);
        });

    }
    else if (qobject_cast<OperatorInterface *>(slot->inputPipe->getSource()))
    {
        // The input is another operator
        for (s32 treeIndex = 1;
             treeIndex < m_d->m_levelTrees.size() && !node;
             ++treeIndex)
        {
            auto tree = m_d->m_levelTrees[treeIndex].operatorTree;

            node = findFirstNode(tree->invisibleRootItem(), [slot](auto nodeToTest) {
                return (nodeToTest->type() == NodeType_OutputPipe
                        && getPointer<Pipe>(nodeToTest) == slot->inputPipe);
            });
        }
    }
    else
    {
        InvalidCodePath;
    }

    if (node && slot->isParameterConnection() && slot->paramIndex < node->childCount())
    {
        node = node->child(slot->paramIndex);
    }

    if (node)
    {
        auto highlight_node = [doHighlight](QTreeWidgetItem *node, const QColor &color)
        {
            if (doHighlight)
            {
                node->setBackground(0, color);
            }
            else
            {
                node->setBackground(0, QColor(0, 0, 0, 0));
            }
        };

        highlight_node(node, InputNodeOfColor);

        for (node = node->parent();
             node;
             node = node->parent())
        {
            highlight_node(node, ChildIsInputNodeOfColor);
        }
    }
}

void EventWidget::addOperator(OperatorPtr op, s32 userLevel)
{
    if (!op) return;

    try
    {
        AnalysisPauser pauser(m_d->m_context);
        m_d->m_context->getAnalysis()->addOperator(m_d->m_eventId, op, userLevel);
        m_d->repopulate();
        m_d->m_analysisWidget->updateAddRemoveUserLevelButtons();
    }
    catch (const std::bad_alloc &)
    {
        QMessageBox::critical(this, QSL("Error"), QString("Out of memory when creating analysis object."));
    }
}

void EventWidget::operatorEdited(OperatorInterface *op)
{
    // Updates the edited OperatorInterface and recursively all the operators
    // depending on it.
    AnalysisPauser pauser(m_d->m_context);

    try
    {
        m_d->m_context->getAnalysis()->beginRun();
    }
    catch (const std::bad_alloc &)
    {
        // Not being able to allocate enough memory for the operator is hopefully the rare case.
        // To keep the code simple we just delete the operator in question and show an error message.
        m_d->m_context->getAnalysis()->removeOperator(op);
        QMessageBox::critical(this, QSL("Error"), QString("Out of memory when creating analysis object."));
    }

    m_d->repopulate();
}

void EventWidget::removeOperator(OperatorInterface *op)
{
    AnalysisPauser pauser(m_d->m_context);
    m_d->m_context->getAnalysis()->removeOperator(op);
    m_d->repopulate();
    m_d->m_analysisWidget->updateAddRemoveUserLevelButtons();
}

void EventWidget::addSource(SourcePtr src, ModuleConfig *module, bool addHistogramsAndCalibration,
                            const QString &unitLabel, double unitMin, double unitMax)
{
    if (!src) return;

    auto analysis = m_d->m_context->getAnalysis();

    try
    {
        AnalysisPauser pauser(m_d->m_context);

        if (addHistogramsAndCalibration)
        {
            // Only implemented for Extractor type sources
            auto extractor = std::dynamic_pointer_cast<Extractor>(src);
            Q_ASSERT(extractor);
            auto rawDisplay = make_raw_data_display(extractor, unitMin, unitMax, QString(), // INCOMPLETE: missing title
                                                    unitLabel);
            add_raw_data_display(analysis, m_d->m_eventId, module->getId(), rawDisplay);
        }
        else
        {
            analysis->addSource(m_d->m_eventId, module->getId(), src);
        }
        m_d->repopulate();
    }
    catch (const std::bad_alloc &)
    {
        QMessageBox::critical(this, QSL("Error"), QString("Out of memory when creating analysis object."));
    }
}

void EventWidget::sourceEdited(SourceInterface *src)
{

    AnalysisPauser pauser(m_d->m_context);

    try
    {
        m_d->m_context->getAnalysis()->beginRun();
    }
    catch (const std::bad_alloc &)
    {
        // Not being able to allocate enough memory here should be the rare case.
        // To keep the code simple we just delete the source in question and show an error message.
        m_d->m_context->getAnalysis()->removeSource(src);
        QMessageBox::critical(this, QSL("Error"), QString("Out of memory when editing analysis object."));
    }
    m_d->repopulate();
}

void EventWidget::removeSource(SourceInterface *src)
{
    AnalysisPauser pauser(m_d->m_context);
    m_d->m_context->getAnalysis()->removeSource(src);
    m_d->repopulate();
}

void EventWidget::uniqueWidgetCloses()
{
    m_d->m_uniqueWidgetActive = false;
    m_d->m_uniqueWidget = nullptr;
}

void EventWidget::addUserLevel()
{
    m_d->addUserLevel();
}

void EventWidget::removeUserLevel()
{
    m_d->removeUserLevel();
}

void EventWidget::repopulate()
{
    m_d->repopulate();
}

QToolBar *EventWidget::getToolBar()
{
    return m_d->m_upperToolBar;
}

QToolBar *EventWidget::getEventSelectAreaToolBar()
{
    return m_d->m_eventSelectAreaToolBar;
}

MVMEContext *EventWidget::getContext() const
{
    return m_d->m_context;
}

AnalysisWidget *EventWidget::getAnalysisWidget() const
{
    return m_d->m_analysisWidget;
}

bool EventWidget::eventFilter(QObject *watched, QEvent *event)
{
#if 0
    if (event->type() == QEvent::FocusIn)
    {
        for (auto trees: m_d->m_levelTrees)
        {
            for (auto tree: {trees.operatorTree, reinterpret_cast<QTreeWidget *>(trees.displayTree)})
            {
                if (tree == watched)
                {
                    if (!tree->currentItem())
                    {
                        // FIXME: This does not interact well with scrolling.
                        // Solutions:
                        // - Track the previous "current" item for each tree
                        //   and reselect it on focus in.
                        // - Instead of using focus use node clicked. This
                        // means keyboard focus switching and moving with arrow
                        // keys won't work.
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
#endif

    return QWidget::eventFilter(watched, event);
}

struct AnalysisWidgetPrivate
{
    AnalysisWidget *m_q;
    MVMEContext *m_context;
    QHash<QUuid, EventWidget *> m_eventWidgetsByEventId;


    QToolBar *m_toolbar;
    QComboBox *m_eventSelectCombo;
    QStackedWidget *m_eventWidgetStack;
    QStackedWidget *m_eventWidgetToolBarStack;
    QStackedWidget *m_eventWidgetEventSelectAreaToolBarStack;
    QToolButton *m_removeUserLevelButton;
    QToolButton *m_addUserLevelButton;
    QStatusBar *m_statusBar;
    QLabel *m_labelSinkStorageSize;
    QLabel *m_labelTimetickCount;
    QLabel *m_statusLabelA2;
    QLabel *m_labelEfficiency;
    QTimer *m_periodicUpdateTimer;
    WidgetGeometrySaver *m_geometrySaver;
    AnalysisInfoWidget *m_infoWidget = nullptr;
    QAction *m_actionPause;
    QAction *m_actionStepNextEvent;

    void repopulate();
    void repopulateEventSelectCombo();
    void doPeriodicUpdate();

    void closeAllUniqueWidgets();
    void closeAllHistogramWidgets();

    void updateActions();

    void actionNew();
    void actionOpen();
    QPair<bool, QString> actionSave();
    QPair<bool, QString> actionSaveAs();
    void actionImport();
    void actionClearHistograms();
#ifdef MVME_ENABLE_HDF5
    void actionSaveSession();
    void actionLoadSession();
#endif
    void actionPause();
    void actionStepNextEvent();

    void updateWindowTitle();
    void updateAddRemoveUserLevelButtons();
};

// Clears the stacked widget and deletes its child widgets
static void clear_stacked_widget(QStackedWidget *stackedWidget)
{
    while (auto widget = stackedWidget->currentWidget())
    {
        stackedWidget->removeWidget(widget);
        widget->deleteLater();
    }
    Q_ASSERT(stackedWidget->count() == 0);
}

void AnalysisWidgetPrivate::repopulate()
{
    clear_stacked_widget(m_eventWidgetEventSelectAreaToolBarStack);
    clear_stacked_widget(m_eventWidgetToolBarStack);
    clear_stacked_widget(m_eventWidgetStack);
    m_eventWidgetsByEventId.clear();

    // Repopulate combobox and stacked widget
    auto eventConfigs = m_context->getEventConfigs();

    // FIXME: event creation is still entirely based on the DAQ config. events
    //        that do exist in the analysis but not in the DAQ won't show up at all
    for (s32 eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto eventId = eventConfig->getId();
        auto eventWidget = new EventWidget(m_context, eventId, eventIndex, m_q);

        auto scrollArea = new QScrollArea;
        scrollArea->setWidget(eventWidget);
        scrollArea->setWidgetResizable(true);

        m_eventWidgetStack->addWidget(scrollArea);
        m_eventWidgetToolBarStack->addWidget(eventWidget->getToolBar());
        m_eventWidgetEventSelectAreaToolBarStack->addWidget(eventWidget->getEventSelectAreaToolBar());
        m_eventWidgetsByEventId[eventId] = eventWidget;
    }

    repopulateEventSelectCombo();

    updateWindowTitle();
    updateAddRemoveUserLevelButtons();
}

void AnalysisWidgetPrivate::repopulateEventSelectCombo()
{
    const QUuid lastSelectedEventId = m_eventSelectCombo->currentData().toUuid();
    m_eventSelectCombo->clear();

    auto eventConfigs = m_context->getEventConfigs();

    s32 comboIndexToSelect = -1;

    for (s32 eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto eventId = eventConfig->getId();

        QObject::disconnect(eventConfig, &ConfigObject::modified, m_q, &AnalysisWidget::eventConfigModified);
        QObject::connect(eventConfig, &ConfigObject::modified, m_q, &AnalysisWidget::eventConfigModified);

        m_eventSelectCombo->addItem(eventConfig->objectName(), eventId);
        qDebug() << __PRETTY_FUNCTION__ << eventConfig->objectName() << eventId << eventIndex;

        if (eventId == lastSelectedEventId)
            comboIndexToSelect = eventIndex;
    }

    if (!lastSelectedEventId.isNull() && comboIndexToSelect < m_eventSelectCombo->count())
    {
        m_eventSelectCombo->setCurrentIndex(comboIndexToSelect);
    }
}

void AnalysisWidgetPrivate::doPeriodicUpdate()
{
    for (auto eventWidget: m_eventWidgetsByEventId.values())
    {
        eventWidget->m_d->doPeriodicUpdate();
    }
}

void AnalysisWidgetPrivate::closeAllUniqueWidgets()
{
    for (auto eventWidget: m_eventWidgetsByEventId.values())
    {
        if (eventWidget->m_d->m_uniqueWidget)
        {
                eventWidget->m_d->m_uniqueWidget->close();
                eventWidget->uniqueWidgetCloses();
        }
    }
}

/* Close any open histograms belonging to the current analysis. */
void AnalysisWidgetPrivate::closeAllHistogramWidgets()
{
    auto close_if_not_null = [](QWidget *widget)
    {
        if (widget)
            widget->close();
    };

    for (const auto &opEntry: m_context->getAnalysis()->getOperators())
    {
        if (auto sink = qobject_cast<Histo1DSink *>(opEntry.op.get()))
        {
            close_if_not_null(m_context->getObjectWidget(sink));

            for (const auto &histoPtr: sink->m_histos)
            {
                close_if_not_null(m_context->getObjectWidget(histoPtr.get()));
            }
        }
        else if (auto sink = qobject_cast<Histo2DSink *>(opEntry.op.get()))
        {
            close_if_not_null(m_context->getObjectWidget(sink));
        }
    }
}

void AnalysisWidgetPrivate::actionNew()
{
    if (m_context->getAnalysis()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save analysis configuration?"),
                           QSL("The current analysis configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!actionSave().first)
                return;
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
        // else discard
    }

    /* Close any active unique widgets _before_ replacing the analysis as the
     * unique widgets might perform actions on the analysis in their reject()
     * code. */
    closeAllUniqueWidgets();
    closeAllHistogramWidgets();

    AnalysisPauser pauser(m_context);
    m_context->getAnalysis()->clear();
    m_context->getAnalysis()->setModified(false);
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

    if (m_context->getAnalysis()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save analysis configuration?"),
                           QSL("The current analysis configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!actionSave().first)
                return;
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
        // else discard
    }

    closeAllUniqueWidgets();
    closeAllHistogramWidgets();

    m_context->loadAnalysisConfig(fileName);
}

QPair<bool, QString> AnalysisWidgetPrivate::actionSave()
{
    QString fileName = m_context->getAnalysisConfigFileName();

    if (fileName.isEmpty())
    {
        return actionSaveAs();
    }
    else
    {
        auto result = saveAnalysisConfig(m_context->getAnalysis(), fileName,
                                         m_context->getWorkspaceDirectory(),
                                         AnalysisFileFilter,
                                         m_context);
        if (result.first)
        {
            m_context->setAnalysisConfigFileName(result.second);
            m_context->getAnalysis()->setModified(false);
        }

        return result;
    }
}

QPair<bool, QString> AnalysisWidgetPrivate::actionSaveAs()
{
    auto result = saveAnalysisConfigAs(m_context->getAnalysis(),
                                       m_context->getWorkspaceDirectory(),
                                       AnalysisFileFilter,
                                       m_context);

    if (result.first)
    {
        m_context->setAnalysisConfigFileName(result.second);
        m_context->getAnalysis()->setModified(false);
    }

    return result;
}

void AnalysisWidgetPrivate::actionImport()
{
    // Step 0) Let the user pick a file
    // Step 1) Create Analysis from file contents
    // Step 2) Generate new IDs for analysis objects
    // Step 3) Try auto-assignment of modules
    // Step 4) If auto assignment fails run the assignment gui
    // Step 5) Add the remaining objects into the existing analysis

    // Step 0) Let the user pick a file
    auto path = m_context->getWorkspaceDirectory();
    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getOpenFileName(m_q, QSL("Import analysis"), path, AnalysisFileFilter);

    if (fileName.isEmpty())
        return;

    // Step 1) Create Analysis from file contents
    QJsonDocument doc(gui_read_json_file(fileName));
    auto json = doc.object()[QSL("AnalysisNG")].toObject();

    Analysis analysis;
    auto readResult = analysis.read(json, m_context->getVMEConfig());

    if (!readResult)
    {
        readResult.errorData["Source file"] = fileName;
        QMessageBox::critical(m_q,
                              QSL("Error importing analysis"),
                              readResult.toRichText());
        return;
    }

    // Step 2) Generate new IDs for analysis objects
    generate_new_object_ids(&analysis);

    // Step 3) Try auto-assignment of modules
    using namespace vme_analysis_common;

    if (!auto_assign_vme_modules(m_context->getVMEConfig(), &analysis))
    {
    // Step 4) If auto assignment fails run the assignment gui
        if (!run_vme_analysis_module_assignment_ui(m_context->getVMEConfig(), &analysis))
            return;
    }

    remove_analysis_objects_unless_matching(&analysis, m_context->getVMEConfig());

    m_context->logMessage(QString("Importing %1").arg(info_string(&analysis)));

    if (analysis.isEmpty())
        return;

    auto sources = analysis.getSources();
    auto operators = analysis.getOperators();

#ifndef QT_NO_DEBUG
    {
        QSet<QUuid> vmeEventIds;
        QSet<QUuid> vmeModuleIds;
        for (auto ec: m_context->getVMEConfig()->getEventConfigs())
        {
            vmeEventIds.insert(ec->getId());

            for (auto mc: ec->getModuleConfigs())
            {
                vmeModuleIds.insert(mc->getId());
            }
        }

        for (auto entry: sources)
        {
            Q_ASSERT(vmeEventIds.contains(entry.eventId));
            Q_ASSERT(vmeModuleIds.contains(entry.moduleId));
        }

        for (auto entry: operators)
        {
            Q_ASSERT(vmeEventIds.contains(entry.eventId));
        }
    }
#endif

    // Step 5) Add the remaining objects into the existing analysis
    AnalysisPauser pauser(m_context);
    auto targetAnalysis = m_context->getAnalysis();

    QHash<QUuid, s32> eventMaxUserLevels;
    for (auto opEntry: targetAnalysis->getOperators())
    {
        if (!eventMaxUserLevels.contains(opEntry.eventId))
        {
            eventMaxUserLevels.insert(opEntry.eventId, targetAnalysis->getMaxUserLevel(opEntry.eventId));
        }
    }

    for (auto entry: sources)
    {
        targetAnalysis->addSource(entry.eventId, entry.moduleId, entry.source);
    }

    for (auto entry: operators)
    {
        s32 baseUserLevel = eventMaxUserLevels.value(entry.eventId, 0);
        s32 targetUserLevel = baseUserLevel + entry.userLevel;
        if (entry.userLevel == 0 && qobject_cast<SinkInterface *>(entry.op.get()))
        {
            targetUserLevel = 0;
        }
        targetAnalysis->addOperator(entry.eventId, entry.op, targetUserLevel);
    }

    repopulate();
}

void AnalysisWidgetPrivate::actionClearHistograms()
{
    AnalysisPauser pauser(m_context);

    for (auto &opEntry: m_context->getAnalysis()->getOperators())
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
            if (histoSink->m_histo)
            {
                histoSink->m_histo->clear();
            }
        }
    }
}

#ifdef MVME_ENABLE_HDF5

static const QString SessionFileFilter = QSL("MVME Sessions (*.hdf5);; All Files (*.*)");
static const QString SessionFileExtension = QSL(".hdf5");

void handle_session_error(const QString &title, const QString &message)
{
    SessionErrorDialog dialog(title, message);
    dialog.exec();

    //m_context->logMessage(QString("Error saving session:"));
    //m_context->logMessageRaw(result.second);
}

void AnalysisWidgetPrivate::actionSaveSession()
{
    qDebug() << __PRETTY_FUNCTION__;

    using ResultType = QPair<bool, QString>;

    ResultType result;

    auto sessionPath = m_context->getWorkspacePath(QSL("SessionDirectory"));

    if (sessionPath.isEmpty())
    {
        sessionPath = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString filename = QFileDialog::getSaveFileName(
        m_q, QSL("Save session"), sessionPath, SessionFileFilter);

    if (filename.isEmpty())
        return;

    QFileInfo fileInfo(filename);

    if (fileInfo.completeSuffix().isEmpty())
    {
        filename += SessionFileExtension;
    }

    AnalysisPauser pauser(m_context);

#if 1 // The QtConcurrent path
    QProgressDialog progressDialog;
    progressDialog.setLabelText(QSL("Saving session..."));
    progressDialog.setMinimum(0);
    progressDialog.setMaximum(0);

    QFutureWatcher<ResultType> watcher;
    QObject::connect(&watcher, &QFutureWatcher<ResultType>::finished, &progressDialog, &QDialog::close);

    QFuture<ResultType> future = QtConcurrent::run(save_analysis_session, filename, m_context->getAnalysis());
    watcher.setFuture(future);

    progressDialog.exec();

    result = future.result();
#else // The blocking path
    result = save_analysis_session(filename, m_context->getAnalysis());
#endif

    if (!result.first)
    {
        handle_session_error(result.second, "Error saving session");
    }
}

void AnalysisWidgetPrivate::actionLoadSession()
{
    auto sessionPath = m_context->getWorkspacePath(QSL("SessionDirectory"));

    if (sessionPath.isEmpty())
    {
        sessionPath = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString filename = QFileDialog::getOpenFileName(
        m_q, QSL("Load session"), sessionPath, SessionFileFilter);

    if (filename.isEmpty())
        return;

    AnalysisPauser pauser(m_context);

    QProgressDialog progressDialog;
    progressDialog.setLabelText(QSL("Loading session config..."));
    progressDialog.setMinimum(0);
    progressDialog.setMaximum(0);
    progressDialog.show();

    QEventLoop loop;

    QJsonDocument analysisJson;

    // load the config first
    {
#if 1 // The QtConcurrent path
        using ResultType = QPair<QJsonDocument, QString>;

        QFutureWatcher<ResultType> watcher;
        QObject::connect(&watcher, &QFutureWatcher<ResultType>::finished, &loop, &QEventLoop::quit);

        QFuture<ResultType> future = QtConcurrent::run(load_analysis_config_from_session_file, filename);
        watcher.setFuture(future);

        loop.exec();

        auto result = future.result();
#else // The blocking path
        auto result = load_analysis_config_from_session_file(filename);
#endif

        if (result.first.isNull())
        {
            progressDialog.hide();

            handle_session_error(result.second, "Error loading session config");

            //m_context->logMessage(QString("Error loading session:"));
            //m_context->logMessageRaw(result.second);
            return;
        }

        analysisJson = QJsonDocument(result.first);
    }

    if (m_context->getAnalysis()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save analysis configuration?"),
                           QSL("The current analysis configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);

        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            auto result = saveAnalysisConfig(
                m_context->getAnalysis(),
                m_context->getAnalysisConfigFileName(),
                m_context->getWorkspaceDirectory(),
                AnalysisFileFilter,
                m_context);

            if (!result.first)
            {
                m_context->logMessage(QSL("Error: ") + result.second);
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    // This is the standard procedure when loading an analysis config
    closeAllUniqueWidgets();
    closeAllHistogramWidgets();

    if (m_context->loadAnalysisConfig(analysisJson, filename, { .NoAutoResume = true }))
    {
        m_context->setAnalysisConfigFileName(QString());
        progressDialog.setLabelText(QSL("Loading session data..."));


#if 1 // The QtConcurrent path
        using ResultType = QPair<bool, QString>;

        QFutureWatcher<ResultType> watcher;
        QObject::connect(&watcher, &QFutureWatcher<ResultType>::finished, &loop, &QEventLoop::quit);

        QFuture<ResultType> future = QtConcurrent::run(load_analysis_session, filename, m_context->getAnalysis());
        watcher.setFuture(future);

        loop.exec();

        auto result = future.result();
#else // The blocking path
        auto result = load_analysis_session(filename, m_context->getAnalysis());
#endif

        if (!result.first)
        {
            handle_session_error(result.second, "Error loading session data");
            return;
        }
    }
}
#endif

void AnalysisWidgetPrivate::updateActions()
{
    auto streamWorker = m_context->getMVMEStreamWorker();
    auto workerState = streamWorker->getState();

    switch (workerState)
    {
        case MVMEStreamWorkerState::Idle:
            m_actionPause->setIcon(QIcon(":/control_pause.png"));
            m_actionPause->setText(QSL("Pause"));
            m_actionPause->setEnabled(false);
            m_actionStepNextEvent->setEnabled(false);
            break;

        case MVMEStreamWorkerState::Running:
            m_actionPause->setIcon(QIcon(":/control_pause.png"));
            m_actionPause->setText(QSL("Pause"));
            m_actionPause->setEnabled(true);
            m_actionStepNextEvent->setEnabled(false);
            break;

        case MVMEStreamWorkerState::Paused:
            m_actionPause->setIcon(QIcon(":/control_play.png"));
            m_actionPause->setText(QSL("Resume"));
            m_actionPause->setEnabled(true);
            m_actionStepNextEvent->setEnabled(true);
            break;
    }
}

void AnalysisWidgetPrivate::actionPause()
{
    auto streamWorker = m_context->getMVMEStreamWorker();
    auto workerState = streamWorker->getState();

    switch (workerState)
    {
        case MVMEStreamWorkerState::Idle:
            Q_ASSERT(false);
            break;

        case MVMEStreamWorkerState::Running:
            streamWorker->pause();
            break;

        case MVMEStreamWorkerState::Paused:
            streamWorker->resume();
            break;
    }
}

void AnalysisWidgetPrivate::actionStepNextEvent()
{
    m_context->logMessage(QSL("Single stepping not yet implement! :("));
}

void AnalysisWidgetPrivate::updateWindowTitle()
{
    QString fileName = m_context->getAnalysisConfigFileName();

    if (fileName.isEmpty())
        fileName = QSL("<not saved>");

    auto wsDir = m_context->getWorkspaceDirectory() + '/';

    if (fileName.startsWith(wsDir))
        fileName.remove(wsDir);

    auto title = QString(QSL("%1 - [Analysis UI]")).arg(fileName);

    if (m_context->getAnalysis()->isModified())
    {
        title += " *";
    }

    m_q->setWindowTitle(title);
}

void AnalysisWidgetPrivate::updateAddRemoveUserLevelButtons()
{
    qDebug() << __PRETTY_FUNCTION__;

    QUuid eventId = m_eventSelectCombo->currentData().toUuid();
    auto analysis = m_context->getAnalysis();
    s32 maxUserLevel = 0;

    for (const auto &opEntry: analysis->getOperators(eventId))
    {
        maxUserLevel = std::max(maxUserLevel, opEntry.userLevel);
    }

    s32 numUserLevels = maxUserLevel + 1;

    EventWidget *eventWidget = m_eventWidgetsByEventId.value(eventId);

    s32 visibleUserLevels = 0;

    if (eventWidget)
    {
        visibleUserLevels = eventWidget->m_d->m_levelTrees.size();
    }

    m_removeUserLevelButton->setEnabled(visibleUserLevels > 1 && visibleUserLevels > numUserLevels);
}

AnalysisWidget::AnalysisWidget(MVMEContext *ctx, QWidget *parent)
    : QWidget(parent)
    , m_d(new AnalysisWidgetPrivate)
{
    m_d->m_q = this;
    m_d->m_context = ctx;

    m_d->m_periodicUpdateTimer = new QTimer(this);
    m_d->m_periodicUpdateTimer->start(PeriodicUpdateTimerInterval_ms);
    m_d->m_geometrySaver = new WidgetGeometrySaver(this);

    /* Note: This code is not efficient at all. This AnalysisWidget and the
     * EventWidgets are recreated and repopulated more often than is really
     * necessary. Rebuilding everything when the underlying objects change was
     * just the easiest way to implement it.
     */

    auto do_repopulate_lambda = [this]() { m_d->repopulate(); };

    // DAQ config changes
    connect(m_d->m_context, &MVMEContext::daqConfigChanged, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::eventAdded, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::eventAboutToBeRemoved, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::moduleAdded, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::moduleAboutToBeRemoved, this, do_repopulate_lambda);

    // Analysis changes
    auto on_analysis_changed = [this]()
    {
        /* Assuming the old analysis has been deleted, thus no
         * QObject::disconnect() is needed. */
        connect(m_d->m_context->getAnalysis(), &Analysis::modifiedChanged, this, [this]() {
            m_d->updateWindowTitle();
        });
        m_d->repopulate();
    };

    connect(m_d->m_context, &MVMEContext::analysisChanged, this, on_analysis_changed);

    connect(m_d->m_context, &MVMEContext::analysisConfigFileNameChanged, this, [this](const QString &) {
        m_d->updateWindowTitle();
    });

    // QStackedWidgets for EventWidgets and their toolbars
    m_d->m_eventWidgetStack = new QStackedWidget;
    m_d->m_eventWidgetToolBarStack = new QStackedWidget;
    connect(m_d->m_eventWidgetStack, &QStackedWidget::currentChanged,
            m_d->m_eventWidgetToolBarStack, &QStackedWidget::setCurrentIndex);

    m_d->m_eventWidgetEventSelectAreaToolBarStack = new QStackedWidget;
    connect(m_d->m_eventWidgetStack, &QStackedWidget::currentChanged,
            m_d->m_eventWidgetEventSelectAreaToolBarStack, &QStackedWidget::setCurrentIndex);

    // toolbar
    {
        m_d->m_toolbar = make_toolbar();

        QAction *action;

        // new, open, save, save as
        m_d->m_toolbar->addAction(QIcon(":/document-new.png"), QSL("New"), this, [this]() { m_d->actionNew(); });
        m_d->m_toolbar->addAction(QIcon(":/document-open.png"), QSL("Open"), this, [this]() { m_d->actionOpen(); });
        m_d->m_toolbar->addAction(QIcon(":/document-save.png"), QSL("Save"), this, [this]() { m_d->actionSave(); });
        m_d->m_toolbar->addAction(QIcon(":/document-save-as.png"), QSL("Save As"), this, [this]() { m_d->actionSaveAs(); });

        // import
        action = m_d->m_toolbar->addAction(QIcon(":/folder_import.png"), QSL("Import"), this, [this]() { m_d->actionImport(); });
        action->setToolTip(QSL("Add items from an existing Analysis"));
        action->setStatusTip(action->toolTip());

        // clear histograms
        m_d->m_toolbar->addSeparator();
        m_d->m_toolbar->addAction(QIcon(":/clear_histos.png"), QSL("Clear Histos"), this, [this]() { m_d->actionClearHistograms(); });

        // info window
        m_d->m_toolbar->addSeparator();
        m_d->m_toolbar->addAction(QIcon(":/info.png"), QSL("Info && Stats"), this, [this]() {

            AnalysisInfoWidget *widget = nullptr;

            if (m_d->m_infoWidget)
            {
                widget = m_d->m_infoWidget;
            }
            else
            {
                widget = new AnalysisInfoWidget(m_d->m_context);
                widget->setAttribute(Qt::WA_DeleteOnClose);
                add_widget_close_action(widget);
                m_d->m_geometrySaver->addAndRestore(widget, QSL("WindowGeometries/AnalysisInfo"));

                connect(widget, &QObject::destroyed, this, [this]() {
                    m_d->m_infoWidget = nullptr;
                });

                m_d->m_infoWidget = widget;
            }

            show_and_activate(widget);
        });

        // pause, resume, step
        connect(m_d->m_context->getMVMEStreamWorker(), &MVMEStreamWorker::stateChanged,
                this, [this](MVMEStreamWorkerState) { m_d->updateActions(); });

        m_d->m_toolbar->addSeparator();
        m_d->m_actionPause = m_d->m_toolbar->addAction(
            QIcon(":/control_pause.png"), QSL("Pause"), this, [this] { m_d->actionPause(); });

        m_d->m_actionStepNextEvent = m_d->m_toolbar->addAction(
            QIcon(":/control_play_stop.png"), QSL("Next Event"), this, [this] { m_d->actionStepNextEvent(); });

#ifdef MVME_ENABLE_HDF5
        m_d->m_toolbar->addSeparator();
        m_d->m_toolbar->addAction(QIcon(":/document-open.png"), QSL("Load Session"), this, [this]() { m_d->actionLoadSession(); });
        m_d->m_toolbar->addAction(QIcon(":/document-save.png"), QSL("Save Session"), this, [this]() { m_d->actionSaveSession(); });
#endif
    }

    // After the toolbar entries the EventWidget specific action will be added.
    // See EventWidget::makeToolBar()

    auto toolbarFrame = new QFrame;
    toolbarFrame->setFrameStyle(QFrame::StyledPanel);
    auto toolbarFrameLayout = new QHBoxLayout(toolbarFrame);
    toolbarFrameLayout->setContentsMargins(0, 0, 0, 0);
    toolbarFrameLayout->setSpacing(0);
    toolbarFrameLayout->addWidget(m_d->m_toolbar);
    toolbarFrameLayout->addWidget(m_d->m_eventWidgetToolBarStack);
    toolbarFrameLayout->addStretch();

    // event select combo
    m_d->m_eventSelectCombo = new QComboBox;
    m_d->m_eventSelectCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    connect(m_d->m_eventSelectCombo, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged), this, [this] (int index) {
        m_d->m_eventWidgetStack->setCurrentIndex(index);
        updateAddRemoveUserLevelButtons();
    });

    // remove user level
    m_d->m_removeUserLevelButton = new QToolButton();
    m_d->m_removeUserLevelButton->setIcon(QIcon(QSL(":/list_remove.png")));
    connect(m_d->m_removeUserLevelButton, &QPushButton::clicked, this, [this]() {
        QUuid eventId = m_d->m_eventSelectCombo->currentData().toUuid();
        EventWidget *eventWidget = m_d->m_eventWidgetsByEventId.value(eventId);
        if (eventWidget)
        {
            eventWidget->removeUserLevel();
            updateAddRemoveUserLevelButtons();
        }
    });

    // add user level
    m_d->m_addUserLevelButton = new QToolButton();
    m_d->m_addUserLevelButton->setIcon(QIcon(QSL(":/list_add.png")));

    connect(m_d->m_addUserLevelButton, &QPushButton::clicked, this, [this]() {
        QUuid eventId = m_d->m_eventSelectCombo->currentData().toUuid();
        EventWidget *eventWidget = m_d->m_eventWidgetsByEventId.value(eventId);
        if (eventWidget)
        {
            eventWidget->addUserLevel();
            updateAddRemoveUserLevelButtons();
        }
    });

    // Layout containing event select combo, a 2nd event widget specific
    // toolbar and the add and remove userlevel buttons
    auto eventSelectLayout = new QHBoxLayout;
    eventSelectLayout->addWidget(new QLabel(QSL("Event:")));
    eventSelectLayout->addWidget(m_d->m_eventSelectCombo);
    auto separatorFrame = new QFrame;
    separatorFrame->setFrameStyle(QFrame::VLine | QFrame::Sunken);
    eventSelectLayout->addWidget(separatorFrame);
    eventSelectLayout->addWidget(m_d->m_eventWidgetEventSelectAreaToolBarStack);
    eventSelectLayout->addStretch();
    eventSelectLayout->addWidget(m_d->m_removeUserLevelButton);
    eventSelectLayout->addWidget(m_d->m_addUserLevelButton);

    // statusbar
    m_d->m_statusBar = make_statusbar();
    // efficiency
    m_d->m_labelEfficiency = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_labelEfficiency);
    // timeticks label
    m_d->m_labelTimetickCount = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_labelTimetickCount);
    // histo storage label
    m_d->m_labelSinkStorageSize = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_labelSinkStorageSize);
    // a2 label
    m_d->m_statusLabelA2 = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_statusLabelA2);

#if ANALYSIS_USE_A2
    m_d->m_statusLabelA2->setText(QSL("a2::"));
#endif

    // main layout
    auto layout = new QGridLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    s32 row = 0;
    layout->addWidget(toolbarFrame, row++, 0);
    layout->addLayout(eventSelectLayout, row++, 0);
    layout->addWidget(m_d->m_eventWidgetStack, row++, 0);
    layout->setRowStretch(row-1, 1);
    layout->addWidget(m_d->m_statusBar, row++, 0);

    auto analysis = ctx->getAnalysis();

    analysis->beginRun(ctx->getRunInfo(),
                       vme_analysis_common::build_id_to_index_mapping(
                           ctx->getVMEConfig()));

    on_analysis_changed();

    // Update the histo storage size in the statusbar
    connect(m_d->m_periodicUpdateTimer, &QTimer::timeout, this, [this]() {
        double storageSize = m_d->m_context->getAnalysis()->getTotalSinkStorageSize();
        QString unit("B");

        if (storageSize > Gigabytes(1))
        {
            storageSize /= Gigabytes(1);
            unit = QSL("GiB");
        }
        else if (storageSize > Megabytes(1))
        {
            storageSize /= Megabytes(1);
            unit = QSL("MiB");
        }
        else if (storageSize == 0.0)
        {
            unit = QSL("MiB");
        }

        m_d->m_labelSinkStorageSize->setText(QString("Histo Storage: %1 %2")
                                             .arg(storageSize, 0, 'f', 2)
                                             .arg(unit));
    });

    // Update statusbar timeticks label
    connect(m_d->m_periodicUpdateTimer, &QTimer::timeout, this, [this]() {

        double tickCount = m_d->m_context->getAnalysis()->getTimetickCount();

        m_d->m_labelTimetickCount->setText(QString("Timeticks: %1 s")
                                           .arg(tickCount));


        if (!m_d->m_context->getAnalysis()->getRunInfo().isReplay)
        {

            auto daqStats = m_d->m_context->getDAQStats();

            double totalBuffers = daqStats.totalBuffersRead;
            double droppedBuffers = daqStats.droppedBuffers;
            double analyzedBuffers = totalBuffers - droppedBuffers;
            double efficiency = analyzedBuffers / totalBuffers;

            if (std::isnan(efficiency))
            {
                efficiency = 0.0;
            }

            m_d->m_labelEfficiency->setText(QString("Efficiency: %1")
                                            .arg(efficiency, 0, 'f', 2));

            auto tt = (QString("Analyzed Buffers:\t%1\n"
                               "Skipped Buffers:\t%2\n"
                               "Total Buffers:\t%3")
                       .arg(analyzedBuffers)
                       .arg(droppedBuffers)
                       .arg(totalBuffers)
                      );

            m_d->m_labelEfficiency->setToolTip(tt);
        }
        else
        {
            m_d->m_labelEfficiency->setText(QSL("Replay"));
            m_d->m_labelEfficiency->setToolTip(QSL(""));
        }
    });

    // Run the periodic update
    connect(m_d->m_periodicUpdateTimer, &QTimer::timeout, this, [this]() { m_d->doPeriodicUpdate(); });

    m_d->updateActions();
}

AnalysisWidget::~AnalysisWidget()
{
    if (m_d->m_infoWidget)
    {
        m_d->m_infoWidget->close();
    }

    delete m_d;
    qDebug() << __PRETTY_FUNCTION__;
}

void AnalysisWidget::operatorAdded(const std::shared_ptr<OperatorInterface> &op)
{
    const auto &opEntries(m_d->m_context->getAnalysis()->getOperators());

    // Find the OperatorEntry for the newly added operator
    auto it = std::find_if(opEntries.begin(), opEntries.end(), [op] (const Analysis::OperatorEntry &entry) {
        return entry.op == op;
    });

    if (it != opEntries.end())
    {
        // Get and repopulate the widget by using OperatorEntry.eventId
        auto entry = *it;
        auto eventWidget = m_d->m_eventWidgetsByEventId.value(entry.eventId);
        if (eventWidget)
        {
            eventWidget->m_d->repopulate();
        }
    }
}

void AnalysisWidget::operatorEdited(const std::shared_ptr<OperatorInterface> &op)
{
    const auto &opEntries(m_d->m_context->getAnalysis()->getOperators());

    // Find the OperatorEntry for the edited operator
    auto it = std::find_if(opEntries.begin(), opEntries.end(), [op] (const Analysis::OperatorEntry &entry) {
        return entry.op == op;
    });

    if (it != opEntries.end())
    {
        // Get and repopulate the widget by using OperatorEntry.eventId
        auto entry = *it;
        auto eventWidget = m_d->m_eventWidgetsByEventId.value(entry.eventId);
        if (eventWidget)
        {
            eventWidget->m_d->repopulate();
        }
    }
}

void AnalysisWidget::updateAddRemoveUserLevelButtons()
{
    m_d->updateAddRemoveUserLevelButtons();
}

void AnalysisWidget::eventConfigModified()
{
    m_d->repopulateEventSelectCombo();
}

bool AnalysisWidget::event(QEvent *e)
{
    if (e->type() == QEvent::StatusTip)
    {
        m_d->m_statusBar->showMessage(reinterpret_cast<QStatusTipEvent *>(e)->tip());
        return true;
    }

    return QWidget::event(e);
}

} // end namespace analysis
