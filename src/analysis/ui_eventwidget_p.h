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
#ifndef __MVME_ANALYSIS_UI_EVENTWIDGET_P_H__
#define __MVME_ANALYSIS_UI_EVENTWIDGET_P_H__

#include "analysis/analysis.h"
#include "analysis/analysis_ui_p.h"
#include "analysis/analysis_util.h"
#include "analysis/ui_eventwidget.h"
#include "analysis/ui_lib.h"
#include "mvme_stream_processor.h"

#include <QSplitter>
#include <QTreeWidget>
#include <QUuid>

namespace analysis
{
namespace ui
{

enum DataRole
{
    DataRole_AnalysisObject = Global_DataRole_AnalysisObject,
    DataRole_RawPointer,
    DataRole_ParameterIndex,
    DataRole_HistoAddress,
};

enum NodeType
{
    NodeType_Event = QTreeWidgetItem::UserType,
    NodeType_Module,
    NodeType_UnassignedModule,
    NodeType_Source,
    NodeType_Operator,
    NodeType_OutputPipe,
    NodeType_OutputPipeParameter,

    NodeType_Histo1DSink,
    NodeType_Histo2DSink,
    NodeType_Sink,          // Sinks that are not handled specifically

    NodeType_Histo1D,

    NodeType_Directory,
    NodeType_PlotGridView,

    NodeType_MaxNodeType
};

class TreeNode: public CheckStateNotifyingNode
{
    public:
        using CheckStateNotifyingNode::CheckStateNotifyingNode;

        // Custom sorting for numeric columns
        virtual bool operator<(const QTreeWidgetItem &other) const override
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
            return CheckStateNotifyingNode::operator<(other);
        }
};

/* Specialized tree for the EventWidget.
 *
 * Note: the declaration is here because of MOC, the implementation is in analysis_ui.cc
 * because of locally defined types.
 */
class ObjectTree: public QTreeWidget, public CheckStateObserver
{
    Q_OBJECT
    public:
        using CheckStateChangeHandler = std::function<void (
            ObjectTree *tree, QTreeWidgetItem *node, const QVariant &prev)>;

        ObjectTree(QWidget *parent = nullptr)
            : QTreeWidget(parent)
        {}

        ObjectTree(EventWidget *eventWidget, s32 userLevel, QWidget *parent = nullptr)
            : QTreeWidget(parent)
            , m_eventWidget(eventWidget)
            , m_userLevel(userLevel)
        {}

        virtual ~ObjectTree() override;

        EventWidget *getEventWidget() const { return m_eventWidget; }
        void setEventWidget(EventWidget *widget) { m_eventWidget = widget; }
        AnalysisServiceProvider *getServiceProvider() const;
        Analysis *getAnalysis() const;
        s32 getUserLevel() const { return m_userLevel; }
        void setUserLevel(s32 userLevel) { m_userLevel = userLevel; }
        QList<QTreeWidgetItem *> getTopLevelSelectedNodes() const;

        void setCheckStateChangeHandler(const CheckStateChangeHandler &csh) { m_csh = csh; }

        virtual void checkStateChanged(QTreeWidgetItem *node, const QVariant &prev) override
        {
            if (m_csh)
            {
                m_csh(this, node, prev);
            }
        }

    protected:
        virtual Qt::DropActions supportedDropActions() const override;
        virtual void dropEvent(QDropEvent *event) override;
        virtual void keyPressEvent(QKeyEvent *event) override;

    private:
        EventWidget *m_eventWidget = nullptr;
        s32 m_userLevel = 0;
        CheckStateChangeHandler m_csh;
};

class OperatorTree: public ObjectTree
{
    Q_OBJECT
    public:
        using ObjectTree::ObjectTree;

        virtual ~OperatorTree() override;

    protected:
        virtual QStringList mimeTypes() const override;

        virtual bool dropMimeData(QTreeWidgetItem *parent, int index,
                                  const QMimeData *data, Qt::DropAction action) override;

        virtual QMimeData *mimeData(const QList<QTreeWidgetItem *> items) const override;
};

class DataSourceTree: public OperatorTree
{
    Q_OBJECT
    public:
        using OperatorTree::OperatorTree;

        virtual ~DataSourceTree() override;

        QTreeWidgetItem *unassignedDataSourcesRoot = nullptr;

    protected:
        virtual QStringList mimeTypes() const override;

        virtual bool dropMimeData(QTreeWidgetItem *parent, int index,
                                  const QMimeData *data, Qt::DropAction action) override;

        virtual QMimeData *mimeData(const QList<QTreeWidgetItem *> items) const override;
};

class SinkTree: public ObjectTree
{
    Q_OBJECT
    public:
        using ObjectTree::ObjectTree;

        virtual ~SinkTree() override;

    protected:
        virtual QStringList mimeTypes() const override;

        virtual bool dropMimeData(QTreeWidgetItem *parent, int index,
                                  const QMimeData *data, Qt::DropAction action) override;

        virtual QMimeData *mimeData(const QList<QTreeWidgetItem *> items) const override;
};

/* Operator (top) and Sink (bottom) trees showing objects for one userlevel. */
struct UserLevelTrees
{
    ObjectTree *operatorTree;
    SinkTree *sinkTree;
    s32 userLevel;

    std::array<ObjectTree *, 2> getObjectTrees() const
    {
        return
        {
            {
                reinterpret_cast<ObjectTree *>(operatorTree),
                reinterpret_cast<ObjectTree *>(sinkTree)
            }
        };
    }
};

using SetOfVoidStar = QSet<void *>;

struct EventWidgetPrivate
{
    enum Mode
    {
        /* Default mode interactions: add/remove objects, operations on selections,
         * drag and drop. */
        Default,

        /* An data extractor or operator add/edit dialog is active and waits
         * for input selection by the user. */
        SelectInput,

        SelectCondition,
    };

    EventWidget *m_q;
    AnalysisServiceProvider *m_serviceProvider;
    AnalysisWidget *m_analysisWidget;

    QVector<UserLevelTrees> m_levelTrees;
    ObjectToNode m_objectMap;

    Mode m_mode = Default;
    QWidget *m_uniqueWidget = nullptr;

    struct InputSelectInfo
    {
        Slot *slot = nullptr;
        s32 userLevel;
        EventWidget::SelectInputCallback callback;
        // Set of additional pipe sources to be considered invalid as valid
        // inputs for the slot.
        QSet<PipeSourceInterface *> additionalInvalidSources;
    };

    InputSelectInfo m_inputSelectInfo;

    struct ConditionSelectInfo
    {
        OperatorPtr op;
        EventWidget::SelectConditionCallback callback;
    };

    ConditionSelectInfo m_conditionSelectInfo;

    ConditionPtr m_selectedCondition;
    OperatorPtr m_selectedOperator;
    bool m_ignoreNextNodeClick = false; // hack to ignore the next itemClicked signal

    QSplitter *m_operatorFrameSplitter;
    QSplitter *m_displayFrameSplitter;

    enum TreeType
    {
        TreeType_Operator,
        TreeType_Sink,
        TreeType_Count
    };
    // Keeps track of the expansion state of those tree nodes that are storing objects in
    // DataRole_Pointer.
    // There's two sets, one for the operator trees and one for the display trees, because
    // objects may have nodes in both trees.
    // FIXME: does the case with two nodes really occur?
    std::array<SetOfVoidStar, TreeType_Count> m_expandedObjects;
    QTimer *m_displayRefreshTimer;

    // If set the trees for that user level will not be shown
    QVector<bool> m_hiddenUserLevels;

    // The user level that was manually added via addUserLevel()
    s32 m_manualUserLevel = 0;

    // Actions and widgets used in makeEventSelectAreaToolBar()
    QAction *m_actionExport;
    QAction *m_actionImport;
    QAction *m_actionSelectVisibleLevels;
    QLabel *m_eventRateLabel;

    QToolBar* m_upperToolBar;
    QToolBar* m_eventSelectAreaToolBar;

    // Periodically updated extractor hit counts and histo sink entry counts.
    struct ObjectCounters
    {
        QVector<double> hitCounts;
    };

    QHash<SourceInterface *, QVector<ObjectCounters>> m_dataSourceCounters;
    QHash<Histo1DSink *, ObjectCounters> m_histo1DSinkCounters;
    QHash<Histo2DSink *, ObjectCounters> m_histo2DSinkCounters;
    MVMEStreamProcessorCounters m_prevStreamProcessorCounters;

    double m_prevAnalysisTimeticks = 0.0;

    // Set to false to temporarily disable repopulating the widget. Should be
    // used prior to making a bunch of changes to the underyling analysis.
    bool repopEnabled = true;

    void createView();
    void populateDataSourceTree(DataSourceTree *tree);
    UserLevelTrees createTrees(s32 level);
    void appendTreesToView(UserLevelTrees trees);
    void repopulate();

    void addUserLevel();
    void removeUserLevel();
    s32 getUserLevelForTree(QTreeWidget *tree);

    void doOperatorTreeContextMenu(ObjectTree *tree, QPoint pos, s32 userLevel);
    void doDataSourceOperatorTreeContextMenu(ObjectTree *tree, QPoint pos, s32 userLevel);
    void doSinkTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel);

    void setMode(Mode mode);
    Mode getMode() const;
    void modeChanged(Mode oldMode, Mode mode);
    void highlightValidInputNodes(QTreeWidgetItem *node);
    void highlightInputNodes(OperatorInterface *op);
    void highlightOutputNodes(PipeSourceInterface *ps);
    void clearToDefaultNodeHighlights(QTreeWidgetItem *node);
    void clearAllToDefaultNodeHighlights();
    //bool hasPendingConditionModifications() const;
    void onNodeClicked(TreeNode *node, int column, s32 userLevel);
    void onNodeDoubleClicked(TreeNode *node, int column, s32 userLevel);
    void onNodeChanged(TreeNode *node, int column, s32 userLevel);
    void onNodeCheckStateChanged(QTreeWidget *tree, QTreeWidgetItem *node, const QVariant &prev);
    void clearAllTreeSelections();
    void clearTreeSelectionsExcept(QTreeWidget *tree);
    void generateDefaultFilters(ModuleConfig *module);
    PipeDisplay *makeAndShowPipeDisplay(Pipe *pipe);
    void doPeriodicUpdate();
    void periodicUpdateDataSourceTreeCounters(double dt_s);
    void periodicUpdateHistoCounters(double dt_s);
    void updateActions();
    void showDependencyGraphWidget(const AnalysisObjectPtr &obj);
    void editOperator(const OperatorPtr &op);
    void editConditionInFirstAvailableSink(const ConditionPtr &cond);
    bool editConditionInSink(const ConditionPtr &cond, const SinkPtr &sink);
    QAction *createEditAction(const OperatorPtr &op);

    // Object and node selections

    // Returns the currentItem() of the tree widget that has focus.
    QTreeWidgetItem *getCurrentNode() const;

    QList<QTreeWidgetItem *> getAllSelectedNodes() const;
    AnalysisObjectVector getAllSelectedObjects() const;

    QList<QTreeWidgetItem *> getTopLevelSelectedNodes() const;
    AnalysisObjectVector getTopLevelSelectedObjects() const;

    QVector<QTreeWidgetItem *> getCheckedNodes(
        Qt::CheckState checkState = Qt::Checked,
        int checkStateColumn = 0) const;

    AnalysisObjectVector getCheckedObjects(
        Qt::CheckState checkState = Qt::Checked,
        int checkStateColumn = 0) const;

    void clearSelections();
    void selectObjects(const AnalysisObjectVector &objects);

    // import / export
    bool canExport() const;
    void actionExport();
    void actionImport();

    // context menu action implementations
    void setSinksEnabled(const SinkVector &sinks, bool enabled);

    void removeSinks(const QVector<SinkInterface *> sinks);
    void removeDirectoryRecursively(const DirectoryPtr &dir);
    void removeObjects(const AnalysisObjectVector &objects);

    QTreeWidgetItem *findNode(const AnalysisObjectPtr &obj);
    QTreeWidgetItem *findNode(const void *rawPtr);

    void copyToClipboard(const AnalysisObjectVector &objects);
    bool canPaste();
    void pasteFromClipboard(QTreeWidget *destTree);

    void actionGenerateHistograms(ObjectTree *tree, const std::vector<QTreeWidgetItem *> &nodes);

    Analysis *getAnalysis() const;
};

QString mode_to_string(EventWidgetPrivate::Mode mode);

} // ns ui
} // ns analysis

#endif /* __MVME_ANALYSIS_UI_EVENTWIDGET_P_H__ */
