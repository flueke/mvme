#ifndef __HISTOGRAM_TREE_H__
#define __HISTOGRAM_TREE_H__

#include <QWidget>
#include <QMap>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

class MVMEContext;
class TreeNode;
class DAQConfig;
class AnalysisConfig;
class ModuleConfig;

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
        void removeHist1D(QTreeWidgetItem *node);

        void removeNode(QTreeWidgetItem *node);
        void addToTreeMap(QObject *object, TreeNode *node);
        void removeFromTreeMap(QObject *object);

        void newConfig();
        void loadConfig();
        bool saveConfig();
        bool saveConfigAs();
        void updateConfigLabel();
        void handleShowDiagnostics();
        void openHistoListWidget();

        MVMEContext *m_context = nullptr;
        DAQConfig *m_daqConfig = nullptr;
        AnalysisConfig *m_analysisConfig = nullptr;
        QTreeWidget *m_tree;
        QMap<QObject *, TreeNode *> m_treeMap;
        TreeNode *m_node1D, *m_node2D;

        QToolButton *pb_new, *pb_load, *pb_save, *pb_saveAs;
        QLabel *label_fileName;
};

#endif /* __HISTOGRAM_TREE_H__ */
