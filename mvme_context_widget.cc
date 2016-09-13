#include "mvme_context_widget.h"
#include "vmusb_readout_worker.h"
#include "vmusb_buffer_processor.h"
#include "CVMUSBReadoutList.h"
#include "histogram.h"
#include "hist2ddialog.h"
#include "vmecontroller.h"
#include "config_widgets.h"

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
#include <QTimer>
#include <QCheckBox>
#include <QFileDialog>
#include <QCoreApplication>

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

        auto onTypeComboIndexChanged = [=](int index)
        {
            auto currentType = static_cast<VMEModuleType>(typeCombo->currentData().toInt());
            QString name = context->getUniqueModuleName(VMEModuleShortNames[currentType]);
            nameEdit->setText(name);
        };

        onTypeComboIndexChanged(0);

        connect(typeCombo, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                this, onTypeComboIndexChanged);

        addressEdit = new QLineEdit;
        addressEdit->setInputMask("\\0\\xHHHH\\0\\0\\0\\0");
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
        module->setName(nameEdit->text());
        module->baseAddress = addressEdit->text().toUInt(&ok, 16);

        QStringList templatePaths;
        templatePaths << QDir::currentPath() + "/templates";
        templatePaths << QCoreApplication::applicationDirPath() + "/templates";

        QString templatePath;

        for (auto testPath: templatePaths)
        {
            if (QFileInfo(testPath).exists())
            {
                templatePath = testPath;
                break;
            }
        }

        if (templatePath.isEmpty())
        {
            context->logMessage("No module template directory found!");
        }
        else
        {
            context->logMessage("Using module templates from " + templatePath);

            if (isMesytecModule(module->type))
            {
                // load template files
                module->initReset       = readStringFile(templatePath + "/mesytec_reset.init");
                module->initReadout     = readStringFile(templatePath + "/mesytec_init_readout.init");
                module->initStartDaq    = readStringFile(templatePath + "/mesytec_startdaq.init");
                module->initStopDaq     = readStringFile(templatePath + "/mesytec_stopdaq.init");
                QString shortname = VMEModuleShortNames[module->type];
                module->initParameters  = readStringFile(QString(templatePath + "/%1_parameters.init")
                                                         .arg(shortname));
                module->generateReadoutStack();
            }
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

    QPushButton *pb_startDAQ, *pb_startOneCycle, *pb_stopDAQ, *pb_replay;
    QLabel *label_daqState, *label_daqDuration, *label_buffersReadAndDropped,
           *label_freeBuffers, *label_readSize, *label_mbPerSecond, *label_controllerState,
           *label_writtenToListFile, *label_vmusbAvgEventsPerBuffer;
    QLineEdit *le_outputDirectory;
    QCheckBox *cb_outputEnabled;

    // events and modules
    QTreeWidget *contextTree;
    QMap<QObject *, QTreeWidgetItem *> treeWidgetMap;   // maps config objects to tree items

    // histograms
    QTreeWidget *histoTree;
    QMap<QObject *, QTreeWidgetItem *> histoTreeMap;    // maps histograms (1d and 2d) to tree items
    QTreeWidgetItem *histCollectionParentItem,
                    *hist2dParentItem;

    QTimer *m_updateStatsTimer;
};

MVMEContextWidget::MVMEContextWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(new MVMEContextWidgetPrivate(this, context))
{
    /*
     * DAQ control
     */
    auto gb_daqControl = new QGroupBox("DAQ Control");
    {
        m_d->pb_startDAQ = new QPushButton("Start");
        m_d->pb_startOneCycle = new QPushButton("1 Cycle");
        m_d->pb_stopDAQ = new QPushButton("Stop");
        m_d->pb_stopDAQ->setEnabled(false);
        m_d->pb_replay = new QPushButton("Replay");

        m_d->label_controllerState = new QLabel("Disconnected");
        m_d->label_daqState = new QLabel("Idle");
        m_d->label_daqDuration = new QLabel();
        m_d->label_buffersReadAndDropped = new QLabel("0 / 0");
        m_d->label_freeBuffers = new QLabel;
        m_d->label_readSize = new QLabel;
        m_d->label_mbPerSecond = new QLabel;
        m_d->label_writtenToListFile = new QLabel;
        m_d->label_vmusbAvgEventsPerBuffer = new QLabel;

        connect(m_d->pb_startDAQ, &QPushButton::clicked, m_d->context, &MVMEContext::startDAQ);

        connect(m_d->pb_startOneCycle, &QPushButton::clicked, this, [=] {
            m_d->context->startDAQ(1);
        });

        connect(m_d->pb_stopDAQ, &QPushButton::clicked, m_d->context, &MVMEContext::stopDAQ);

        connect(m_d->pb_replay, &QPushButton::clicked, context, &MVMEContext::startReplay);

        auto layout = new QGridLayout(gb_daqControl);
        layout->setContentsMargins(2, 4, 2, 2);
        layout->addWidget(m_d->pb_startDAQ, 0, 0);
        layout->addWidget(m_d->pb_startOneCycle, 0, 1);
        layout->addWidget(m_d->pb_stopDAQ, 0, 2);
        layout->addWidget(m_d->pb_replay, 0, 3);

        auto stateLayout = new QFormLayout;
        stateLayout->setContentsMargins(2, 4, 2, 2);
        stateLayout->setSpacing(2);
        stateLayout->addRow("Controller:", m_d->label_controllerState);
        stateLayout->addRow("State:", m_d->label_daqState);
        stateLayout->addRow("Running time:", m_d->label_daqDuration);
        stateLayout->addRow("Free event buffers:", m_d->label_freeBuffers);
        stateLayout->addRow("Buffers read / dropped / errors:", m_d->label_buffersReadAndDropped);
        stateLayout->addRow("Buffers/s / MB/s:", m_d->label_mbPerSecond);
        stateLayout->addRow("Last readbuffer size:", m_d->label_readSize);
        stateLayout->addRow("vmusb avg. events per buffer:", m_d->label_vmusbAvgEventsPerBuffer);
        stateLayout->addRow("ListFile size:", m_d->label_writtenToListFile);

        layout->addLayout(stateLayout, 1, 0, 1, 3);
    }

    m_d->contextTree = new QTreeWidget;
    m_d->contextTree->setExpandsOnDoubleClick(false);
    m_d->contextTree->setColumnCount(2);
    auto headerItem = m_d->contextTree->headerItem();

    headerItem->setText(0, "Object");
    headerItem->setText(1, "Counts");

    m_d->contextTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_d->contextTree, &QWidget::customContextMenuRequested, this, &MVMEContextWidget::treeContextMenu);
    connect(m_d->contextTree, &QTreeWidget::itemClicked, this, &MVMEContextWidget::treeItemClicked);
    connect(m_d->contextTree, &QTreeWidget::itemDoubleClicked, this, &MVMEContextWidget::treeItemDoubleClicked);

    connect(context, &MVMEContext::moduleAdded, this, &MVMEContextWidget::onModuleConfigAdded);
    connect(context, &MVMEContext::eventConfigAdded, this, &MVMEContextWidget::onEventConfigAdded);
    connect(context, &MVMEContext::configChanged, this, &MVMEContextWidget::onConfigChanged);
    connect(context, &MVMEContext::daqStateChanged, this, &MVMEContextWidget::onDAQStateChanged);
    connect(context, &MVMEContext::hist2DAdded, this, &MVMEContextWidget::onHist2DAdded);

    connect(context, &MVMEContext::moduleAboutToBeRemoved, this, [=](ModuleConfig *config) {
        delete m_d->treeWidgetMap.take(config);
    });

    connect(context, &MVMEContext::eventConfigAboutToBeRemoved, this, [=](EventConfig *config) {
        delete m_d->treeWidgetMap.take(config);
    });

    connect(context, &MVMEContext::histogramCollectionAboutToBeRemoved, this, [=](HistogramCollection *hist) {
        delete m_d->histoTreeMap.take(hist);
    });

    connect(context, &MVMEContext::hist2DAboutToBeRemoved, this, [=](Hist2D *hist2d) {
        delete m_d->histoTreeMap.take(hist2d);
    });

    auto onControllerStateChanged = [=](ControllerState state) {
        m_d->label_controllerState->setText(state == ControllerState::Closed
                                            ? QSL("Disconnected")
                                            : QSL("Connected"));
    };

    auto controller = context->getController();
    connect(controller, &VMEController::controllerStateChanged, this, onControllerStateChanged);
    onControllerStateChanged(controller->getState());

    auto splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(gb_daqControl);

    /*
     * DAQ config
     */
    {
        auto gb_daqConfiguration = new QGroupBox("DAQ Configuration");
        auto layout = new QVBoxLayout(gb_daqConfiguration);
        layout->setContentsMargins(2, 4, 2, 2);
        layout->setSpacing(2);

        m_d->le_outputDirectory = new QLineEdit();
        m_d->cb_outputEnabled = new QCheckBox("Write listfile");
        auto pb_outputDirectory = new QPushButton("Select");

        connect(m_d->le_outputDirectory, &QLineEdit::textEdited, this, [=](const QString &newText){
            context->getConfig()->setListFileOutputDirectory(newText);
        });

        connect(pb_outputDirectory, &QPushButton::clicked, this, [=] {
            auto dirName = QFileDialog::getExistingDirectory(this, "Select output directory",
                                                             context->getConfig()->getListFileOutputDirectory());
            if (!dirName.isEmpty())
            {
                m_d->le_outputDirectory->setText(dirName);
                context->getConfig()->setListFileOutputDirectory(dirName);
            }
        });



        auto hbox = new QHBoxLayout;
        hbox->addWidget(m_d->cb_outputEnabled);
        hbox->addWidget(pb_outputDirectory);

        connect(m_d->cb_outputEnabled, &QCheckBox::stateChanged, this, [=](int state) {
            bool enabled = (state != Qt::Unchecked);
            context->getConfig()->setListFileOutputEnabled(enabled);
        });

        auto gb_output  = new QGroupBox("Listfile Output");
        auto listFileLayout = new QFormLayout(gb_output);
        listFileLayout->addRow(m_d->le_outputDirectory);
        listFileLayout->addRow(hbox);

        layout->addWidget(gb_output);

        layout->addWidget(m_d->contextTree);
        splitter->addWidget(gb_daqConfiguration);
    }

    /*
     * histograms
     */
    {
        auto gb_histograms = new QGroupBox("Histograms");
        auto layout = new QVBoxLayout(gb_histograms);
        m_d->histoTree = new QTreeWidget;
        m_d->histoTree->setExpandsOnDoubleClick(false);
        m_d->histoTree->setSortingEnabled(true);
        m_d->histoTree->setColumnCount(1);
        auto headerItem = m_d->histoTree->headerItem();
        headerItem->setText(0, "Histogram Name");

        layout->setContentsMargins(2, 4, 2, 2);
        layout->setSpacing(2);
        layout->addWidget(m_d->histoTree);
        splitter->addWidget(gb_histograms);

        m_d->histCollectionParentItem = new QTreeWidgetItem;
        m_d->histCollectionParentItem->setText(0, "1D");
        m_d->histoTree->addTopLevelItem(m_d->histCollectionParentItem);
        m_d->histCollectionParentItem->setExpanded(true);

        m_d->hist2dParentItem = new QTreeWidgetItem;
        m_d->hist2dParentItem->setText(0, "2D");
        m_d->histoTree->addTopLevelItem(m_d->hist2dParentItem);
        m_d->hist2dParentItem->setExpanded(true);

        m_d->histoTree->sortByColumn(0, Qt::AscendingOrder);

        m_d->histoTree->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_d->histoTree, &QTreeWidget::itemClicked, this, &MVMEContextWidget::histoItemClicked);
        connect(m_d->histoTree, &QTreeWidget::itemDoubleClicked, this, &MVMEContextWidget::histoItemDoubleClicked);
        connect(m_d->histoTree, &QTreeWidget::customContextMenuRequested, this, &MVMEContextWidget::histoTreeContextMenu);
        connect(context, &MVMEContext::histogramCollectionAdded, this, &MVMEContextWidget::onHistogramCollectionAdded);
    }

    /*
     * widget layout
     */
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    m_d->m_updateStatsTimer = new QTimer(this);
    m_d->m_updateStatsTimer->setInterval(250);
    m_d->m_updateStatsTimer->start();
    connect(m_d->m_updateStatsTimer, &QTimer::timeout, this, &MVMEContextWidget::updateStats);
}

void MVMEContextWidget::onConfigChanged()
{
    reloadConfig();
}

void MVMEContextWidget::reloadConfig()
{
    auto config = m_d->context->getConfig();

    {
        QSignalBlocker b(m_d->le_outputDirectory);
        m_d->le_outputDirectory->setText(config->getListFileOutputDirectory());
    }
    {
        QSignalBlocker b(m_d->cb_outputEnabled);
        m_d->cb_outputEnabled->setChecked(config->isListFileOutputEnabled());
    }

    m_d->contextTree->clear();
    m_d->treeWidgetMap.clear();


    for (auto event: config->getEventConfigs())
    {
        onEventConfigAdded(event);
        for (auto module: event->modules)
        {
            onModuleConfigAdded(event, module);
        }
    }

    m_d->contextTree->resizeColumnToContents(0);
}

enum TreeItemType
{
    TIT_EventConfig,
    TIT_VMEModule
};

template<typename Pred>
QTreeWidgetItem *findItem(QTreeWidgetItem *root, Pred predicate)
{
    if (predicate(root))
        return root;

    for (int childIndex=0; childIndex<root->childCount(); ++childIndex)
    {
        auto child = root->child(childIndex);
        if (auto ret = findItem(child, predicate))
            return ret;
    }

    return 0;
}

template<typename Pred>
QTreeWidgetItem *findItem(QTreeWidget *widget, Pred predicate)
{
    return findItem(widget->invisibleRootItem(), predicate);
}

void MVMEContextWidget::onEventConfigAdded(EventConfig *eventConfig)
{
    auto item = new QTreeWidgetItem(TIT_EventConfig);
    m_d->treeWidgetMap[eventConfig] = item;

    auto updateTreeItem = [eventConfig, this]()
    {
        QString text;
        switch (eventConfig->triggerCondition)
        {
            case TriggerCondition::Interrupt:
                {
                    text = QString("%1 (IRQ, lvl=%2, vec=%3)")
                        .arg(eventConfig->getName())
                        .arg(eventConfig->irqLevel)
                        .arg(eventConfig->irqVector);
                } break;
            case TriggerCondition::NIM1:
                {
                    text = QString("%1 (NIM)")
                        .arg(eventConfig->getName());
                } break;
            case TriggerCondition::Periodic:
                {
                    text = QString("%1 (Periodic)")
                        .arg(eventConfig->getName());
                } break;
        }


        auto item = m_d->treeWidgetMap[eventConfig];
        if (item)
        {
            item->setText(0, text);
            item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void *>(eventConfig)));
        }
    };

    updateTreeItem();
    m_d->contextTree->addTopLevelItem(item);

    connect(eventConfig, &EventConfig::modified, eventConfig, updateTreeItem);

    m_d->contextTree->resizeColumnToContents(0);
}

template<typename T>
static
T *getPointerFromItem(QTreeWidgetItem *item, int column = 0, int role = Qt::UserRole)
{
    auto voidstar = item->data(column, role).value<void *>();
    return static_cast<T *>(voidstar);
}

void MVMEContextWidget::onModuleConfigAdded(EventConfig *eventConfig, ModuleConfig *module)
{
    auto parentItem = m_d->treeWidgetMap.value(eventConfig);

    if (!parentItem)
    {
        qDebug() << "Error: no tree item for" << eventConfig << "found";
        return;
    }

    auto item = new QTreeWidgetItem(TIT_VMEModule);
    item->setText(0, module->getName());
    item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void *>(module)));

    parentItem->addChild(item);
    m_d->contextTree->expandItem(parentItem);
    m_d->treeWidgetMap[module] = item;

    connect(module, &ModuleConfig::nameChanged, module, [=](const QString &name) {
        auto item = m_d->treeWidgetMap.value(module);
        if (item)
        {
            item->setText(0, name);
        }
    });
}

void MVMEContextWidget::treeContextMenu(const QPoint &pos)
{
    auto item = m_d->contextTree->itemAt(pos);

    QMenu menu;

    QAction *actionAddEvent = new QAction("Add Event", &menu);
    QAction *actionAddModule = new QAction("Add Module", &menu);
    QAction *actionDelEvent = new QAction("Remove Event", &menu);
    QAction *actionDelModule = new QAction("Remove Module", &menu);

    if (!item)
    {
        menu.addAction(actionAddEvent);
    }
    else if (item->type() == TIT_EventConfig)
    {
        menu.addAction(actionAddModule);
        menu.addSeparator();
        menu.addAction(actionDelEvent);
    }
    else if (item->type() == TIT_VMEModule)
    {
        menu.addAction(actionDelModule);
    }

    auto action = menu.exec(m_d->contextTree->mapToGlobal(pos));

    if (action == actionAddEvent)
    {
        auto config = new EventConfig;
        config->setName(QString("event%1").arg(m_d->context->getEventConfigs().size()));
        EventConfigDialog dialog(m_d->context, config);
        int result = dialog.exec();
        if (result == QDialog::Accepted)
        {
            m_d->context->addEventConfig(config);
        }
        else
        {
            delete config;
        }
    }
    else if (action == actionAddModule)
    {
        auto parent = getPointerFromItem<EventConfig>(item);
        AddModuleDialog dialog(m_d->context, parent);
        dialog.exec();
    }
    else if (action == actionDelEvent)
    {
        auto event = getPointerFromItem<EventConfig>(item);
        m_d->context->removeEvent(event);
    }
    else if (action == actionDelModule)
    {
        auto module = getPointerFromItem<ModuleConfig>(item);
        m_d->context->removeModule(module);
    }
}

void MVMEContextWidget::treeItemClicked(QTreeWidgetItem *item, int column)
{
    TreeItemType type = static_cast<TreeItemType>(item->type());

    switch (type)
    {
        case TIT_EventConfig:
            emit eventClicked(getPointerFromItem<EventConfig>(item));
            break;

        case TIT_VMEModule:
            emit moduleClicked(getPointerFromItem<ModuleConfig>(item));
            break;
    };
}

void MVMEContextWidget::treeItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    TreeItemType type = static_cast<TreeItemType>(item->type());

    switch (type)
    {
        case TIT_EventConfig:
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

void MVMEContextWidget::histoItemClicked(QTreeWidgetItem *item)
{
    auto object = Var2Ptr<QObject>(item->data(0, Qt::UserRole));

    auto histo = qobject_cast<HistogramCollection *>(object);
    auto hist2d = qobject_cast<Hist2D *>(object);

    if (histo)
    {
        emit histogramCollectionClicked(histo);
    }
    else if (hist2d)
    {
        emit hist2DClicked(hist2d);
    }
}

void MVMEContextWidget::histoItemDoubleClicked(QTreeWidgetItem *item)
{
    auto object = Var2Ptr<QObject>(item->data(0, Qt::UserRole));

    auto histo = qobject_cast<HistogramCollection *>(object);
    auto hist2d = qobject_cast<Hist2D *>(object);

    if (histo)
    {
        emit histogramCollectionDoubleClicked(histo);
    }
    else if (hist2d)
    {
        emit hist2DDoubleClicked(hist2d);
    }
}

void MVMEContextWidget::histoTreeContextMenu(const QPoint &pos)
{
    auto item = m_d->histoTree->itemAt(pos);

    auto object = item ? Var2Ptr<QObject>(item->data(0, Qt::UserRole)) : nullptr;
    auto histo = qobject_cast<HistogramCollection *>(object);
    auto hist2d = qobject_cast<Hist2D *>(object);

    QMenu menu;
    QAction *openAction = new QAction("Open", &menu);
    QAction *clearAction = new QAction("Clear", &menu);
    QAction *delAction = new QAction("Remove", &menu);
    QAction *addHist2D = new QAction("Add 2D Histogram", &menu);

    if (item)
    {
        menu.addAction(openAction);
        menu.addAction(clearAction);
        menu.addAction(delAction);
    }

    if (m_d->context->getConfig()->getAllModuleConfigs().size())
    {
        menu.addAction(addHist2D);
    }

    if (menu.isEmpty())
        return;

    auto action = menu.exec(m_d->histoTree->mapToGlobal(pos));


    if (action == openAction)
    {
        if (histo)
            emit showHistogramCollection(histo);
        if (hist2d)
            emit showHist2D(hist2d);
    }
    else if (action == clearAction)
    {
        if (histo)
            histo->clearHistogram();
        if (hist2d)
            hist2d->clear();
    }
    else if (action == delAction)
    {
        if (histo)
        {
            m_d->context->removeHistogramCollection(histo);
        }

        if (hist2d)
        {
            m_d->context->removeHist2D(hist2d);
        }
    }
    else if (action == addHist2D)
    {
        Hist2DDialog dialog(m_d->context);
        int result = dialog.exec();

        if (result == QDialog::Accepted)
        {
            auto hist2d = dialog.getHist2D();
            m_d->context->addHist2D(hist2d);
        }
    }
}

void MVMEContextWidget::onHistogramCollectionAdded(HistogramCollection *histo)
{
    auto item = new QTreeWidgetItem(m_d->histCollectionParentItem);
    m_d->histoTreeMap[histo] = item;

    auto updateTreeItem = [histo, this]()
    {
        auto item = m_d->histoTreeMap[histo];
        if (item)
        {
            item->setText(0, histo->objectName());
        }
    };

    updateTreeItem();
    item->setData(0, Qt::UserRole, Ptr2Var(histo));

    connect(histo, &QObject::objectNameChanged, histo, updateTreeItem);
}

void MVMEContextWidget::onHist2DAdded(Hist2D *hist2d)
{
    auto item = new QTreeWidgetItem(m_d->hist2dParentItem);
    m_d->histoTreeMap[hist2d] = item;

    auto updateTreeItem = [hist2d, this]()
    {
        auto item = m_d->histoTreeMap[hist2d];
        if (item)
        {
            item->setText(0, hist2d->objectName());
        }
    };

    updateTreeItem();
    item->setData(0, Qt::UserRole, Ptr2Var(hist2d));

    connect(hist2d, &QObject::objectNameChanged, hist2d, updateTreeItem);
}

void MVMEContextWidget::updateStats()
{
    auto stats = m_d->context->getDAQStats();

    auto startTime = stats.startTime;
    auto endTime   = m_d->context->getDAQState() == DAQState::Idle ? stats.endTime : QDateTime::currentDateTime();
    auto duration  = startTime.secsTo(endTime);
    auto durationString = makeDurationString(duration);
    double mbPerSecond = 0;
    double buffersPerSecond = 0;
    if (duration > 0)
    {
        mbPerSecond = ((double)stats.bytesRead / (1024.0*1024.0)) / (double)duration;
        buffersPerSecond = (double)stats.buffersRead / (double)duration;
    }
    m_d->label_daqDuration->setText(durationString);

    m_d->label_buffersReadAndDropped->setText(QString("%1 / %2 / %3")
                                              .arg(stats.buffersRead)
                                              .arg(stats.droppedBuffers)
                                              .arg(stats.buffersWithErrors)
                                              );

    m_d->label_freeBuffers->setText(QString::number(stats.freeBuffers));
    m_d->label_readSize->setText(QString::number(stats.readSize));
    m_d->label_mbPerSecond->setText(QString("%1 / %2")
                                    .arg(buffersPerSecond, 6, 'f', 2)
                                    .arg(mbPerSecond, 6, 'f', 2)
                                    );
    m_d->label_writtenToListFile->setText(QString("%1 MB")
                                          .arg((double)stats.listFileBytesWritten / (1024.0*1024.0), 6, 'f', 2)
                                         );

    m_d->label_vmusbAvgEventsPerBuffer->setText(QString::number(stats.vmusbAvgEventsPerBuffer));

    for (auto it=m_d->treeWidgetMap.begin(); it!=m_d->treeWidgetMap.end(); ++it)
    {
        it.value()->setText(1, QSL(""));
    }

    auto eventCounts = stats.eventCounts;

    for (auto it=eventCounts.begin(); it!=eventCounts.end(); ++it)
    {
        auto treeItem = m_d->treeWidgetMap.value(it.key());
        if (treeItem)
        {
            treeItem->setText(1, QString::number(it.value()));
        }
    }
}
