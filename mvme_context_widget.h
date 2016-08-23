#ifndef UUID_d7f2fe8a_95dc_414c_b630_191563e5fb16
#define UUID_d7f2fe8a_95dc_414c_b630_191563e5fb16

#include "mvme_context.h"
#include "hist2d.h"
#include <QWidget>

struct MVMEContextWidgetPrivate;
class QTreeWidgetItem;
class QListWidgetItem;

class MVMEContextWidget: public QWidget
{
    Q_OBJECT
    signals:
        void eventClicked(EventConfig *config);
        void moduleClicked(ModuleConfig *config);

        void eventDoubleClicked(EventConfig *config);
        void moduleDoubleClicked(ModuleConfig *config);

        void deleteEvent(EventConfig *config);
        void deleteModule(ModuleConfig *module);

        void histogramClicked(const QString &name, HistogramCollection *histo);
        void histogramDoubleClicked(const QString &name, HistogramCollection *histo);

        void hist2DClicked(Hist2D *hist2d);
        void hist2DDoubleClicked(Hist2D *hist2d);
        void showHistogram(HistogramCollection *histo);
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
        void histoListItemClicked(QListWidgetItem *item);
        void histoListItemDoubleClicked(QListWidgetItem *item);
        void histoListContextMenu(const QPoint &pos);
        void onContextHistoAdded(const QString &name, HistogramCollection *histo);
        void onHist2DAdded(Hist2D *hist2d);
        void onConfigChanged();
        void updateStats();

    private:
        MVMEContextWidgetPrivate *m_d;
};

#endif
