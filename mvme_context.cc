#include "mvme_context.h"
#include "vme_module.h"
#include "vmusb.h"
#include "vmusb_readout_worker.h"
#include "dataprocessor.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSplitter>
#include <QtConcurrent>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QThread>

struct AddVMEModuleDialog: public QDialog
{
    AddVMEModuleDialog(MVMEContext *context, DAQEventConfig *parentConfig, QWidget *parent = 0)
        : QDialog(parent)
        , context(context)
        , parentConfig(parentConfig)
    {
        // TODO: get module types and names from static map
        QStringList moduleTypes = {
            "Unknown",
            "MADC",
            "MQDC",
            "MTDC",
            "MDPP16",
            "MDPP32",
            "MDI12"
        };

        typeCombo = new QComboBox;
        typeCombo->addItems(moduleTypes);

        nameEdit = new QLineEdit;
        addressEdit = new QLineEdit;

        auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto layout = new QFormLayout(this);
        layout->addRow("Type", typeCombo);
        layout->addRow("Name", nameEdit);
        layout->addRow("Address", addressEdit);
        layout->addRow(bb);
    }

    virtual void accept()
    {
        // TODO: create the correct module here
        auto module = new GenericModule(0, "test");
        context->addModule(parentConfig, module);
        QDialog::accept();
    }

    QComboBox *typeCombo;
    QLineEdit *nameEdit;
    QLineEdit *addressEdit;
    MVMEContext *context;
    DAQEventConfig *parentConfig;
};

//
// ===========
//

MVMEContext::MVMEContext(QObject *parent)
    : QObject(parent)
    , m_ctrlOpenTimer(new QTimer(this))
    , m_readoutThread(new QThread(this))
    , m_readoutWorker(new VMUSBReadoutWorker(this))
    , m_dataProcessorThread(new QThread(this))
    , m_dataProcessor(new DataProcessor(this))
{

    for (size_t i=0; i<dataBufferCount; ++i)
    {
        m_freeBuffers.push_back(new DataBuffer(dataBufferSize));
    }


    connect(m_ctrlOpenTimer, &QTimer::timeout,
            this, &MVMEContext::tryOpenController);

    m_ctrlOpenTimer->setInterval(100);
    m_ctrlOpenTimer->start();

    m_readoutWorker->moveToThread(m_readoutThread);
    m_readoutThread->setObjectName("ReadoutThread");
    m_readoutThread->start();

    // XXX
    m_dataProcessor->moveToThread(m_dataProcessorThread);
    //m_dataProcessor->moveToThread(m_readoutThread);
    m_dataProcessorThread->setObjectName("DataProcessorThread");
    m_dataProcessorThread->start();

    connect(m_readoutWorker, &VMUSBReadoutWorker::eventReady, m_dataProcessor, &DataProcessor::processBuffer);
    connect(m_dataProcessor, &DataProcessor::bufferProcessed, m_readoutWorker, &VMUSBReadoutWorker::addFreeBuffer);
}

MVMEContext::~MVMEContext()
{
    QMetaObject::invokeMethod(m_readoutWorker, "stop", Qt::QueuedConnection);
    while (m_readoutWorker->getState() != DAQState::Idle)
    {
        QThread::msleep(50);
    }
    m_readoutThread->quit();
    m_readoutThread->wait();
    m_dataProcessorThread->quit();
    m_dataProcessorThread->wait();
    delete m_readoutWorker;
    delete m_dataProcessor;
    qDeleteAll(m_eventConfigs);
}

void MVMEContext::addModule(DAQEventConfig *eventConfig, VMEModule *module)
{
    eventConfig->modules.push_back(module);
    emit moduleAdded(eventConfig, module);
}

void MVMEContext::addEventConfig(DAQEventConfig *eventConfig)
{
    m_eventConfigs.push_back(eventConfig);
    emit eventConfigAdded(eventConfig);

    for (auto module: eventConfig->modules)
    {
        emit moduleAdded(eventConfig, module);
    }
}

DAQEventConfig *MVMEContext::addNewEventConfig()
{
    auto result = new DAQEventConfig;
    result->name = QString("Event %1").arg(m_eventConfigs.size());
    addEventConfig(result);
    return result;
}

void MVMEContext::setController(VMEController *controller)
{
    m_controller = controller;
    emit vmeControllerSet(controller);
}

void MVMEContext::tryOpenController()
{
    auto *vmusb = dynamic_cast<VMUSB *>(m_controller);

    if (vmusb && !vmusb->isOpen() && !m_ctrlOpenFuture.isRunning())
    {
        m_ctrlOpenFuture = QtConcurrent::run(vmusb, &VMUSB::openFirstUsbDevice);
    }
}

//
// ===============================
//

struct MVMEContextWidgetPrivate
{
    MVMEContextWidgetPrivate(MVMEContextWidget *q, MVMEContext *context)
        : m_q(q)
        , context(context)
    {}

    MVMEContextWidget *m_q;
    MVMEContext *context;

    QPushButton *pb_startDAQ, *pb_startOneCycle, *pb_stopDAQ;
    QLabel *label_daqState;
    QTreeWidget *tw_contextTree;
};

MVMEContextWidget::MVMEContextWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(new MVMEContextWidgetPrivate(this, context))
{
    auto daqWidget = new QGroupBox("DAQ");
    {
        m_d->pb_startDAQ = new QPushButton("Start");
        m_d->pb_startOneCycle = new QPushButton("1 Cycle");
        m_d->pb_stopDAQ = new QPushButton("Stop");
        m_d->pb_stopDAQ->setEnabled(false);
        m_d->label_daqState = new QLabel("Idle");

        auto readoutWorker = m_d->context->getReadoutWorker();

        connect(m_d->pb_startDAQ, &QPushButton::clicked, readoutWorker, &VMUSBReadoutWorker::start);
        connect(m_d->pb_startOneCycle, &QPushButton::clicked, [=] {
                QMetaObject::invokeMethod(readoutWorker, "start", Qt::QueuedConnection, Q_ARG(quint32, 1));
                });
        connect(m_d->pb_stopDAQ, &QPushButton::clicked, readoutWorker, &VMUSBReadoutWorker::stop);
        connect(readoutWorker, &VMUSBReadoutWorker::stateChanged, this, &MVMEContextWidget::daqStateChanged);

        auto layout = new QGridLayout(daqWidget);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->addWidget(m_d->pb_startDAQ, 0, 0);
        layout->addWidget(m_d->pb_startOneCycle, 0, 1);
        layout->addWidget(m_d->pb_stopDAQ, 0, 2);

        auto stateLayout = new QFormLayout;
        stateLayout->addRow("State:", m_d->label_daqState);

        layout->addLayout(stateLayout, 1, 0, 1, 3);
    }

    m_d->tw_contextTree = new QTreeWidget;
    m_d->tw_contextTree->header()->close();
    m_d->tw_contextTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_d->tw_contextTree, &QWidget::customContextMenuRequested, this, &MVMEContextWidget::treeContextMenu);
    connect(m_d->tw_contextTree, &QTreeWidget::itemClicked, this, &MVMEContextWidget::treeItemClicked);

    auto splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(daqWidget);

    auto w = new QWidget;
    auto wl = new QVBoxLayout(w);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);
    wl->addWidget(new QLabel("Events"));
    wl->addWidget(m_d->tw_contextTree);
    splitter->addWidget(w);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    connect(context, &MVMEContext::moduleAdded, this, &MVMEContextWidget::onModuleAdded);
    connect(context, &MVMEContext::eventConfigAdded, this, &MVMEContextWidget::onEventConfigAdded);

    qDebug() << __PRETTY_FUNCTION__ << m_d->tw_contextTree->topLevelItemCount();
}

enum TreeItemType
{
    TIT_DAQEventConfig,
    TIT_VMEModule
};

void MVMEContextWidget::onEventConfigAdded(DAQEventConfig *eventConfig)
{
    qDebug() << __PRETTY_FUNCTION__ << eventConfig << m_d->tw_contextTree->topLevelItemCount();

    auto item = new QTreeWidgetItem(TIT_DAQEventConfig);
    item->setText(0, eventConfig->name);
    item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void *>(eventConfig)));

    m_d->tw_contextTree->addTopLevelItem(item);
}

void MVMEContextWidget::onModuleAdded(DAQEventConfig *eventConfig, VMEModule *module)
{
    qDebug() << __PRETTY_FUNCTION__ << eventConfig << module <<  m_d->tw_contextTree->topLevelItemCount();
    QTreeWidgetItem *parentItem = 0;

    for(int i=0; i<m_d->tw_contextTree->topLevelItemCount(); ++i)
    {
        auto item = m_d->tw_contextTree->topLevelItem(i);
        auto ptr = item->data(0, Qt::UserRole).value<void *>();
        if (ptr == eventConfig)
        {
            parentItem = item;
            break;
        }
    }

    if (!parentItem)
        return;

    auto item = new QTreeWidgetItem(TIT_VMEModule);
    item->setText(0, module->getName());
    item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void *>(module)));

    parentItem->addChild(item);
}

void MVMEContextWidget::treeContextMenu(const QPoint &pos)
{
    auto item = m_d->tw_contextTree->itemAt(pos);

    QMenu menu;

    auto actionAddEventConfig = menu.addAction("Add Event Config");
    QAction *actionAddVMEModule = 0;
    if (item && item->type() == TIT_DAQEventConfig)
    {
        actionAddVMEModule = menu.addAction("Add VME Module");
    }

    auto action = menu.exec(m_d->tw_contextTree->mapToGlobal(pos));

    if (!action) return;

    if (action == actionAddEventConfig)
    {
        m_d->context->addNewEventConfig();
    }
    else if (action == actionAddVMEModule)
    {
        auto parent = static_cast<DAQEventConfig *>(item->data(0, Qt::UserRole).value<void *>());
        AddVMEModuleDialog dialog(m_d->context, parent);
        dialog.exec();
    }
}

void MVMEContextWidget::treeItemClicked(QTreeWidgetItem *item, int column)
{
    qDebug() << __PRETTY_FUNCTION__ << item << column;
}

void MVMEContextWidget::daqStateChanged(DAQState state)
{
    auto label = m_d->label_daqState;

    switch (state)
    {
        case DAQState::Idle:
            label->setText("Idle");
            break;

        case DAQState::Starting:
            label->setText("Starting");
            break;

        case DAQState::Running:
            label->setText("Running");
            break;
        case DAQState::Stopping:
            label->setText("Stopping");
            break;
    }

    m_d->pb_startDAQ->setEnabled(state == DAQState::Idle);
    m_d->pb_startOneCycle->setEnabled(state == DAQState::Idle);
    m_d->pb_stopDAQ->setEnabled(state != DAQState::Idle);
}
