#ifndef UUID_9196420f_dd04_4572_8e4b_952039634913
#define UUID_9196420f_dd04_4572_8e4b_952039634913

#include <QList>
#include <QWidget>
#include <QFuture>

class VMEController;
class VMEModule;
class MesytecChain;
class VMUSBStack;
class QTimer;

enum class TriggerCondition
{
    NIM1,
    Scaler,
    Interrupt
};

struct DAQEventConfig
{
    QString name;
    TriggerCondition triggerCondition;
    uint8_t irqLevel = 0;
    uint8_t irqVector = 0;
    QList<VMEModule *> modules;
};


class MVMEContext: public QObject
{
    Q_OBJECT
    signals:
        void vmeControllerSet(VMEController *controller);
        void eventConfigAdded(DAQEventConfig *eventConfig);
        void moduleAdded(DAQEventConfig *eventConfig, VMEModule *module);

    public:
        MVMEContext(QObject *parent = 0);
        void addModule(DAQEventConfig *eventConfig, VMEModule *module);
        void addEventConfig(DAQEventConfig *eventConfig);
        DAQEventConfig *addNewEventConfig();
        void setController(VMEController *controller);
        VMEController *getController() const { return m_controller; }

    private slots:
        void tryOpenController();

    private:
        VMEController *m_controller = nullptr;
        QTimer *m_ctrlOpenTimer;
        QFuture<void> m_ctrlOpenFuture;
        QList<DAQEventConfig *> m_eventConfigs;
};

struct MVMEContextWidgetPrivate;
class QTreeWidgetItem;

class MVMEContextWidget: public QWidget
{
    Q_OBJECT
    signals:
        void addDAQEventConfig();
        void addVMEModule(DAQEventConfig *parentConfig);

    public:
        MVMEContextWidget(MVMEContext *context, QWidget *parent = 0);

    private slots:
        void onEventConfigAdded(DAQEventConfig *eventConfig);
        void onModuleAdded(DAQEventConfig *eventConfig, VMEModule *module);
        void treeContextMenu(const QPoint &pos);
        void treeItemClicked(QTreeWidgetItem *item, int column);

    private:
        MVMEContextWidgetPrivate *m_d;
};

class DAQEventConfigWidget: public QWidget
{
    Q_OBJECT
    public:
        DAQEventConfigWidget(DAQEventConfig *eventConfig, QWidget *parent = 0);
}

#endif
