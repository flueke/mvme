#ifndef UUID_d7f2fe8a_95dc_414c_b630_191563e5fb16
#define UUID_d7f2fe8a_95dc_414c_b630_191563e5fb16

#include "mvme_context.h"
#include <QWidget>

struct MVMEContextWidgetPrivate;
class QTreeWidgetItem;

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

    public:
        MVMEContextWidget(MVMEContext *context, QWidget *parent = 0);

        void reloadConfig();

    private slots:
        void onEventConfigAdded(EventConfig *eventConfig);
        void onModuleAdded(EventConfig *eventConfig, ModuleConfig *module);
        void treeContextMenu(const QPoint &pos);
        void treeItemClicked(QTreeWidgetItem *item, int column);
        void treeItemDoubleClicked(QTreeWidgetItem *item, int column);
        void onDAQStateChanged(DAQState state);

    private:
        MVMEContextWidgetPrivate *m_d;
};

#endif
