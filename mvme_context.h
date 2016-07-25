#ifndef UUID_9196420f_dd04_4572_8e4b_952039634913
#define UUID_9196420f_dd04_4572_8e4b_952039634913

#include <QList>
#include <QWidget>
#include <QFuture>
#include "globals.h"
#include "vmecommandlist.h"
#include "vme_module.h"
#include "databuffer.h"

class VMEController;
class MesytecChain;
class VMUSBStack;
class VMUSBReadoutWorker;
class DataProcessor;

class QTimer;
class QThread;

struct DAQEventConfig
{
    VMECommandList getInitCommands()
    {
        VMECommandList ret;
        for (auto module: modules)
        {
            module->addInitCommands(&ret);
        }
        return ret;
    }

    VMECommandList getReadoutCommands()
    {
        VMECommandList ret;
        for (auto module: modules)
        {
            module->addReadoutCommands(&ret);
        }
        return ret;
    }

    VMECommandList getStartDaqCommands()
    {
        VMECommandList ret;
        for (auto module: modules)
        {
            module->addStartDaqCommands(&ret);
        }
        return ret;
    }

    VMECommandList getStopDaqCommands()
    {
        VMECommandList ret;
        for (auto module: modules)
        {
            module->addStopDaqCommands(&ret);
        }
        return ret;
    }

    ~DAQEventConfig()
    {
        qDeleteAll(modules);
    }
    QString name;
    TriggerCondition triggerCondition;
    uint8_t irqLevel = 0;
    uint8_t irqVector = 0;
    uint8_t stackID; // currently set by the readout worker
    QList<VMEModule *> modules;
};

struct DAQConfig
{
    QList<DAQEventConfig *> eventConfigs;
};


class MVMEContext: public QObject
{
    static const size_t dataBufferCount = 20;
    static const size_t dataBufferSize  = 30 * 1024;

    Q_OBJECT
    signals:
        void vmeControllerSet(VMEController *controller);
        void eventConfigAdded(DAQEventConfig *eventConfig);
        void moduleAdded(DAQEventConfig *eventConfig, VMEModule *module);

    public:
        MVMEContext(QObject *parent = 0);
        ~MVMEContext();
        void addModule(DAQEventConfig *eventConfig, VMEModule *module);
        void addEventConfig(DAQEventConfig *eventConfig);
        DAQEventConfig *addNewEventConfig();
        void setController(VMEController *controller);
        VMEController *getController() const { return m_controller; }
        VMUSBReadoutWorker *getReadoutWorker() const { return m_readoutWorker; }
        QList<DAQEventConfig *> getEventConfigs() const { return m_eventConfigs; }
        DataBufferQueue *getFreeBuffers() { return &m_freeBuffers; }

        friend class mvme;

    private slots:
        void tryOpenController();

    private:
        VMEController *m_controller = nullptr;
        QTimer *m_ctrlOpenTimer;
        QFuture<void> m_ctrlOpenFuture;
        QList<DAQEventConfig *> m_eventConfigs;
        QThread *m_readoutThread;

        VMUSBReadoutWorker *m_readoutWorker;
        DataBufferQueue m_freeBuffers;

        QThread *m_dataProcessorThread;
        DataProcessor *m_dataProcessor;
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
        void daqStateChanged(DAQState state);

    private:
        MVMEContextWidgetPrivate *m_d;
};

class DAQEventConfigWidget: public QWidget
{
    Q_OBJECT
    public:
        DAQEventConfigWidget(DAQEventConfig *eventConfig, QWidget *parent = 0);
};

#endif
