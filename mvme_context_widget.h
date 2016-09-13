#ifndef UUID_d7f2fe8a_95dc_414c_b630_191563e5fb16
#define UUID_d7f2fe8a_95dc_414c_b630_191563e5fb16

#include "mvme_context.h"
#include "hist2d.h"
#include <QWidget>

struct MVMEContextWidgetPrivate;
class QTreeWidgetItem;

class MVMEContextWidget: public QWidget
{
    Q_OBJECT
    signals:
        void eventClicked(EventConfig *config);
        void eventDoubleClicked(EventConfig *config);
        void deleteEvent(EventConfig *config);

        void moduleClicked(ModuleConfig *config);
        void moduleDoubleClicked(ModuleConfig *config);
        void deleteModule(ModuleConfig *module);

        void histogramCollectionClicked(HistogramCollection *histo);
        void histogramCollectionDoubleClicked(HistogramCollection *histo);
        void showHistogramCollection(HistogramCollection *histo);

        void hist2DClicked(Hist2D *hist2d);
        void hist2DDoubleClicked(Hist2D *hist2d);
        void showHist2D(Hist2D *hist2d);

    public:
        MVMEContextWidget(MVMEContext *context, QWidget *parent = 0);

        void reloadConfig();

    private slots:
        void onEventConfigAdded(EventConfig *eventConfig);
        void onModuleConfigAdded(EventConfig *eventConfig, ModuleConfig *module);
        void treeContextMenu(const QPoint &pos);
        void treeItemClicked(QTreeWidgetItem *item, int column);
        void treeItemDoubleClicked(QTreeWidgetItem *item, int column);
        void onDAQStateChanged(DAQState state);

        // histograms
        void histoItemClicked(QTreeWidgetItem *item);
        void histoItemDoubleClicked(QTreeWidgetItem *item);
        void histoTreeContextMenu(const QPoint &pos);
        void onHistogramCollectionAdded(HistogramCollection *histo);
        void onHist2DAdded(Hist2D *hist2d);

        void onConfigChanged();
        void updateStats();

    private:
        MVMEContextWidgetPrivate *m_d;
};

#endif
