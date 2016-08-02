#include "mvme_context_widget.h"
#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "CVMUSBReadoutList.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>

struct AddEventConfigDialog: public QDialog
{
    AddEventConfigDialog(MVMEContext *context, QWidget *parent = 0)
        : QDialog(parent)
        , context(context)
    {
        setWindowTitle("Add event");

        nameEdit = new QLineEdit;
        nameEdit->setText(QString("event%1").arg(context->getEventConfigs().size()));

        triggerCombo = new QComboBox;
        for (auto type: TriggerConditionNames.keys())
        {
            triggerCombo->addItem(TriggerConditionNames[type], QVariant::fromValue(static_cast<int>(type)));
        }

        irqLevelSpin = new QSpinBox;
        irqLevelSpin->setMinimum(1);
        irqLevelSpin->setMaximum(7);

        irqVectorSpin = new QSpinBox;
        irqVectorSpin->setMinimum(0);
        irqVectorSpin->setMaximum(255);


        auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto layout = new QFormLayout(this);

        layout->addRow("Event Name", nameEdit);
        layout->addRow("Trigger Condition", triggerCombo);
        layout->addRow("IRQ Level", irqLevelSpin);
        layout->addRow("IRQ Vector", irqVectorSpin);
        layout->addRow(bb);

        connect(nameEdit, &QLineEdit::textChanged, [=](const QString &text) {
            bb->button(QDialogButtonBox::Ok)->setEnabled(text.size());
        });

        connect(triggerCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this, [=](int index) {

            auto type = static_cast<TriggerCondition>(triggerCombo->currentData().toInt());
            irqLevelSpin->setEnabled(type == TriggerCondition::Interrupt);
            irqVectorSpin->setEnabled(type == TriggerCondition::Interrupt);
        });
        irqLevelSpin->setEnabled(false);
        irqVectorSpin->setEnabled(false);
    }

    virtual void accept()
    {
        auto event = new EventConfig;
        event->name = nameEdit->text();
        event->triggerCondition = static_cast<TriggerCondition>(triggerCombo->currentData().toInt());
        event->irqLevel = irqLevelSpin->value();
        event->irqVector = irqVectorSpin->value();
        context->addEventConfig(event);
        QDialog::accept();
    }

    MVMEContext *context;
    QLineEdit *nameEdit;
    QComboBox *triggerCombo;
    QSpinBox *irqLevelSpin, *irqVectorSpin;
};

struct AddModuleDialog: public QDialog
{
    AddModuleDialog(MVMEContext *context, EventConfig *parentConfig, QWidget *parent = 0)
        : QDialog(parent)
        , context(context)
        , parentConfig(parentConfig)
    {
        typeCombo = new QComboBox;

        for (auto type: VMEModuleTypeNames.keys())
        {
            typeCombo->addItem(VMEModuleTypeNames[type], QVariant::fromValue(static_cast<int>(type)));
        }

        nameEdit = new QLineEdit;
        nameEdit->setText(QString("mod%1").arg(context->getTotalModuleCount()));

        addressEdit = new QLineEdit;
        addressEdit->setInputMask("\\0\\xHHHHHHHH");
        addressEdit->setText("0x00000000");

        auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto layout = new QFormLayout(this);
        layout->addRow("Type", typeCombo);
        layout->addRow("Name", nameEdit);
        layout->addRow("Address", addressEdit);
        layout->addRow(bb);

        connect(addressEdit, &QLineEdit::textChanged, [=](const QString &) {
            bb->button(QDialogButtonBox::Ok)->setEnabled(addressEdit->hasAcceptableInput());
        });
    }

    virtual void accept()
    {
        bool ok;
        auto module = new ModuleConfig;
        module->type = static_cast<VMEModuleType>(typeCombo->currentData().toInt());
        module->name = nameEdit->text();
        module->baseAddress = addressEdit->text().toUInt(&ok, 16);

        if (isMesytecModule(module->type))
        {
            // load template files
            module->initReset       = readStringFile("templates/mesytec_reset.init");
            module->initReadout     = readStringFile("templates/mesytec_init_readout.init");
            module->initStartDaq    = readStringFile("templates/mesytec_startdaq.init");
            module->initStopDaq     = readStringFile("templates/mesytec_stopdaq.init");
            QString shortname = VMEModuleShortNames[module->type];
            module->initParameters  = readStringFile(QString("templates/%1_parameters.init").arg(shortname));

            // generate readout stack
            VMECommandList readoutCmds;
            readoutCmds.addFifoRead32(module->baseAddress, 254);
            readoutCmds.addMarker(EndOfModuleMarker);
            readoutCmds.addWrite16(module->baseAddress + 0x6034, 1);
            CVMUSBReadoutList readoutList(readoutCmds);
            module->readoutStack = readoutList.toString();
        }

        context->addModule(parentConfig, module);
        QDialog::accept();
    }

    QComboBox *typeCombo;
    QLineEdit *nameEdit;
    QLineEdit *addressEdit;
    MVMEContext *context;
    EventConfig *parentConfig;
};

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
    auto gb_daqControl = new QGroupBox("DAQ Control");
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
        connect(readoutWorker, &VMUSBReadoutWorker::stateChanged, this, &MVMEContextWidget::onDAQStateChanged);

        auto layout = new QGridLayout(gb_daqControl);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->addWidget(m_d->pb_startDAQ, 0, 0);
        layout->addWidget(m_d->pb_startOneCycle, 0, 1);
        layout->addWidget(m_d->pb_stopDAQ, 0, 2);

        auto stateLayout = new QFormLayout;
        stateLayout->addRow("State:", m_d->label_daqState);

        layout->addLayout(stateLayout, 1, 0, 1, 3);
    }

    m_d->tw_contextTree = new QTreeWidget;
    m_d->tw_contextTree->setExpandsOnDoubleClick(false);
    m_d->tw_contextTree->header()->close();
    m_d->tw_contextTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_d->tw_contextTree, &QWidget::customContextMenuRequested, this, &MVMEContextWidget::treeContextMenu);
    connect(m_d->tw_contextTree, &QTreeWidget::itemClicked, this, &MVMEContextWidget::treeItemClicked);
    connect(m_d->tw_contextTree, &QTreeWidget::itemDoubleClicked, this, &MVMEContextWidget::treeItemDoubleClicked);

    auto splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(gb_daqControl);

    auto gb_daqConfiguration = new QGroupBox("DAQ Configuration");

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
    connect(context, &MVMEContext::configChanged, this, &MVMEContextWidget::reloadConfig);

    qDebug() << __PRETTY_FUNCTION__ << m_d->tw_contextTree->topLevelItemCount();
}

void MVMEContextWidget::reloadConfig()
{
    m_d->tw_contextTree->clear();
    auto config = m_d->context->getConfig();

    for (auto event: config->eventConfigs)
    {
        onEventConfigAdded(event);
        for (auto module: event->modules)
        {
            onModuleAdded(event, module);
        }
    }
}

enum TreeItemType
{
    TIT_DAQEventConfig,
    TIT_VMEModule
};

void MVMEContextWidget::onEventConfigAdded(EventConfig *eventConfig)
{
    qDebug() << __PRETTY_FUNCTION__ << eventConfig << m_d->tw_contextTree->topLevelItemCount();

    auto item = new QTreeWidgetItem(TIT_DAQEventConfig);

    QString text;
    switch (eventConfig->triggerCondition)
    {
        case TriggerCondition::Interrupt:
            {
                text = QString("%1 (IRQ, lvl=%2, vec=%3)")
                    .arg(eventConfig->name)
                    .arg(eventConfig->irqLevel)
                    .arg(eventConfig->irqVector);
            } break;
        case TriggerCondition::NIM1:
            {
                text = QString("%1 (NIM)")
                    .arg(eventConfig->name);
            } break;
        case TriggerCondition::Scaler:
            {
                text = QString("%1 (Periodic)")
                    .arg(eventConfig->name);
            } break;
    }


    item->setText(0, text);
    item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void *>(eventConfig)));

    m_d->tw_contextTree->addTopLevelItem(item);
}

void MVMEContextWidget::onModuleAdded(EventConfig *eventConfig, ModuleConfig *module)
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
    item->setText(0, module->name);
    item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void *>(module)));

    parentItem->addChild(item);
    m_d->tw_contextTree->expandItem(parentItem);
}

template<typename T>
static
T *getPointerFromItem(QTreeWidgetItem *item, int column = 0, int role = Qt::UserRole)
{
    auto voidstar = item->data(column, role).value<void *>();
    return static_cast<T *>(voidstar);
}

void MVMEContextWidget::treeContextMenu(const QPoint &pos)
{
    auto item = m_d->tw_contextTree->itemAt(pos);

    QMenu menu;

    QAction *actionAddEventConfig = new QAction("Add Event", &menu);
    QAction *actionAddVMEModule = new QAction("Add Module", &menu);

    if (!item)
    {
        menu.addAction(actionAddEventConfig);
    }
    else if (item->type() == TIT_DAQEventConfig)
    {
        menu.addAction(actionAddVMEModule);
    }

    auto action = menu.exec(m_d->tw_contextTree->mapToGlobal(pos));

    if (action == actionAddEventConfig)
    {
        AddEventConfigDialog dialog(m_d->context);
        dialog.exec();
    }
    else if (action == actionAddVMEModule)
    {
        auto parent = getPointerFromItem<EventConfig>(item);
        AddModuleDialog dialog(m_d->context, parent);
        dialog.exec();
    }
}

void MVMEContextWidget::treeItemClicked(QTreeWidgetItem *item, int column)
{
    qDebug() << __PRETTY_FUNCTION__ << item << column;
    TreeItemType type = static_cast<TreeItemType>(item->type());

    switch (type)
    {
        case TIT_DAQEventConfig:
            emit eventClicked(getPointerFromItem<EventConfig>(item));
            break;

        case TIT_VMEModule:
            emit moduleClicked(getPointerFromItem<ModuleConfig>(item));
            break;
    };
}

void MVMEContextWidget::treeItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    qDebug() << __PRETTY_FUNCTION__ << item << column;

    TreeItemType type = static_cast<TreeItemType>(item->type());

    switch (type)
    {
        case TIT_DAQEventConfig:
            emit eventDoubleClicked(getPointerFromItem<EventConfig>(item));
            break;

        case TIT_VMEModule:
            emit moduleDoubleClicked(getPointerFromItem<ModuleConfig>(item));
            break;
    };
}

void MVMEContextWidget::onDAQStateChanged(DAQState state)
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
