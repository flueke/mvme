#ifndef __HISTOGRAM_TREE_H__
#define __HISTOGRAM_TREE_H__

#include <QWidget>
#include <QMultiMap>
#include <QSet>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

class MVMEContext;
class TreeNode;
class DAQConfig;
class AnalysisConfig;
class ModuleConfig;
class Hist2DConfig;

namespace analysis
{
    class Analysis;
}

class HistogramTreeWidget: public QWidget
{
    Q_OBJECT
    signals:
        void objectClicked(QObject *object);
        void objectDoubleClicked(QObject *object);
        void openInNewWindow(QObject *object);
        void showDiagnostics(ModuleConfig *cfg);
        void addWidgetWindow(QWidget *widget, QSize windowSize = QSize());

    public:
        HistogramTreeWidget(MVMEContext *context, QWidget *parent = 0);

    private:
        void onObjectAdded(QObject *object);
        void onObjectAboutToBeRemoved(QObject *object);
        void onAnyConfigChanged();
        void onObjectNameChanged(QObject *object, const QString &name);

        void onItemClicked(QTreeWidgetItem *item, int column);
        void onItemDoubleClicked(QTreeWidgetItem *item, int column);
        void onItemChanged(QTreeWidgetItem *item, int column);
        void onItemExpanded(QTreeWidgetItem *item);
        void treeContextMenu(const QPoint &pos);

        void clearHistogram();
        void removeHistogram();
        void add2DHistogram();
        void edit2DHistogram();
        void generateDefaultFilters();
        void updateHistogramCountDisplay();
        void updateNodesFor(Hist2DConfig *config);

        void addDataFilter();
        void removeDataFilter(QTreeWidgetItem *node);
        void removeDataFilter();
        void editDataFilter(QTreeWidgetItem *node);
        void editDataFilter();

        void addDualWordDataFilter();
        void removeDualWordDataFilter(QTreeWidgetItem *node);
        void removeDualWordDataFilter();
        void editDualWordDataFilter(QTreeWidgetItem *node);
        void editDualWordDataFilter();

        void clearHistograms();
        void clearHistograms(QTreeWidgetItem *node);
        void removeHist1D(QTreeWidgetItem *node);

        void removeNode(QTreeWidgetItem *node);
        void addToTreeMap(QObject *object, TreeNode *node);
        void removeFromTreeMap(QObject *object);
        void removeFromTreeMap(QObject *object, TreeNode *node);

        void newConfig();
        void loadConfig();
        bool saveConfig();
        bool saveConfigAs();
        void updateConfigLabel();
        void handleShowDiagnostics();
        void openHistoListWidget();

        void updateAnalysisNGStuff();

        MVMEContext *m_context = nullptr;
        DAQConfig *m_daqConfig = nullptr;
        AnalysisConfig *m_analysisConfig = nullptr;
        QTreeWidget *m_tree;
        QMultiMap<QObject *, TreeNode *> m_treeMap;
        QMap<void *, TreeNode *> m_treeMapUntyped;
        TreeNode *m_node1D, *m_node2D;
        TreeNode *m_node1dNew, *m_node2dNew;
        TreeNode *m_nodeAnalysisNG;
        TreeNode *m_nodeAnalysisNGObjects;

        QToolButton *pb_new, *pb_load, *pb_save, *pb_saveAs;
        QLineEdit *le_fileName;

        QSet<QObject *> m_analysisObjects;
};

#endif /* __HISTOGRAM_TREE_H__ */
