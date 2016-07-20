#ifndef UUID_9196420f_dd04_4572_8e4b_952039634913
#define UUID_9196420f_dd04_4572_8e4b_952039634913

#include <QList>
#include <QWidget>

class VMEController;
class VMEModule;
class MesytecChain;
class VMUSBStack;

class MVMEContext: public QObject
{
    Q_OBJECT
    signals:
        void moduleAdded(VMEModule *module);

    public:
        void addModule(VMEModule *module);

        VMEController *controller = 0;
        QList<VMEModule *> modules;
        QList<MesytecChain *> mesytec_chains;
        QList<VMUSBStack *> vmusb_stacks;
};

struct MVMEContextWidgetPrivate;

class MVMEContextWidget: public QWidget
{
    Q_OBJECT
    public:
        MVMEContextWidget(MVMEContext *context, QWidget *parent = 0);

    private slots:
        void onModuleAdded(VMEModule *module);

    private:
        MVMEContextWidgetPrivate *m_d;
};

#endif
