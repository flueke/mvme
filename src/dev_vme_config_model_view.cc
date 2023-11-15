#include <iostream>
#include <QApplication>
#include <QFileDialog>
#include <spdlog/spdlog.h>

#include "multi_crate.h"
#include "mvme_session.h"
#include "util/qt_fs.h"
#include "vme_config_model_view.h"
#include "vme_config_tree.h"
#include "vme_config_ui.h"
#include "vme_config_util.h"

using namespace mesytec::mvme;
using namespace mesytec::multi_crate;

void logger(const QString &msg)
{
    spdlog::info(msg.toStdString());
}

enum class ConfigItemType
{
    Unspecified,
    MulticrateConfig,
    VmeConfig,
    Events,
    Event,
    Module,
    ModuleReset,
    EventModulesInit,
    EventReadoutLoop,
    EventMulticast,
    VMEScript,
    Container,
};

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
    DataRole_ScriptCategory,
};

class BaseItem: public QStandardItem
{
    public:
        BaseItem(ConfigItemType type)
            : type_(type) {}

        BaseItem(ConfigItemType type, const QString &text)
            : QStandardItem(text), type_(type) {}

        BaseItem(ConfigItemType type, const QIcon &icon, const QString &text)
            : QStandardItem(icon, text), type_(type) {}

        int type() const override { return static_cast<int>(type_); }

    private:
        ConfigItemType type_;
};

void enable_flags(QStandardItem *item, Qt::ItemFlags flags)
{
    item->setFlags(item->flags() | flags);
}

void disable_flags(QStandardItem *item, Qt::ItemFlags flags)
{
    item->setFlags(item->flags() & ~flags);
}

class ItemBuilder
{
    public:
        ItemBuilder(ConfigItemType type)
            : type_(type) {}

        ItemBuilder(ConfigItemType type, const QString &text)
            : type_(type), text_(text) {}

        ItemBuilder(ConfigItemType type, const QIcon &icon, const QString &text)
            : type_(type), icon_(icon), text_(text) {}

        ItemBuilder &text(const QString &text) { text_ = text; return *this; }
        ItemBuilder &qObject(QObject *obj) { obj_ = obj; return *this; }
        ItemBuilder &enableFlags(Qt::ItemFlags flag) { flagsEnable_ |= flag; return *this; }
        ItemBuilder &disableFlags(Qt::ItemFlags flag) { flagsDisable_ |= flag; return *this; }

        BaseItem *build()
        {
            auto result = new BaseItem(type_, icon_, text_);

            if (obj_)
            {
                result->setData(QVariant::fromValue(obj_), DataRole_Pointer);

                if (auto p = obj_->property("display_name"); p.isValid() && text_.isEmpty())
                    result->setText(p.toString());

                if (auto p = obj_->property("icon"); p.isValid() && icon_.isNull())
                    result->setIcon(QIcon(p.toString()));
            }

            if (flagsEnable_)
                enable_flags(result, flagsEnable_);

            if (flagsDisable_)
                disable_flags(result, flagsDisable_);

            return result;
        }

    private:
        ConfigItemType type_;
        QIcon icon_;
        QString text_;
        QObject *obj_ = nullptr;
        Qt::ItemFlags flagsEnable_;
        Qt::ItemFlags flagsDisable_;
};

QList<QStandardItem *> build_generic(ConfigObject *config);

QList<QStandardItem *> build_container(ContainerObject *config)
{
    auto root = ItemBuilder(ConfigItemType::Container)
        .qObject(config)
        .build();

    for (auto child: config->getChildren())
    {
        root->appendRow(build_generic(child));
    }

    return { root };
}

QList<QStandardItem *> build_script(VMEScriptConfig *config)
{
    auto root = ItemBuilder(ConfigItemType::VMEScript, config->objectName())
        .qObject(config)
        .build();

    return { root };
}

QList<QStandardItem *> build_multicrate(MulticrateVMEConfig *config)
{
    auto root = ItemBuilder(ConfigItemType::MulticrateConfig, config->objectName())
        .qObject(config)
        .build();

    for (const auto &vmeConfig: config->getCrateConfigs())
    {
        root->appendRow(build_generic(vmeConfig));
    }
    return { root };
}

QList<QStandardItem *> build_vmeconfig(VMEConfig *config)
{
    auto root = ItemBuilder(ConfigItemType::VmeConfig, config->objectName())
        .qObject(config)
        .build();

    auto triggerIo = ItemBuilder(ConfigItemType::VMEScript, "MVLC Trigger/IO")
        .qObject(config->getMVLCTriggerIOScript())
        .disableFlags(Qt::ItemIsDragEnabled | Qt::ItemIsEditable)
        .build();

    auto daqStart = build_generic(config->getGlobalStartsScripts());
    disable_flags(daqStart[0], Qt::ItemIsEditable);

    auto events = ItemBuilder(ConfigItemType::Events, QIcon(":/mvme_16x16.png"), "Events")
        .qObject(config)
        .disableFlags(Qt::ItemIsDragEnabled | Qt::ItemIsEditable)
        .build();

    for (auto eventConfig: config->getEventConfigs())
        events->appendRow(build_generic(eventConfig));

    auto daqStop = build_generic(config->getGlobalStopScripts());
    disable_flags(daqStop[0], Qt::ItemIsEditable);

    auto manual = build_generic(config->getGlobalManualScripts());
    disable_flags(manual[0], Qt::ItemIsEditable);

    root->appendRow(triggerIo);
    root->appendRow(daqStart);
    root->appendRow(events);
    root->appendRow(daqStop);
    root->appendRow(manual);
    return { root };
}


QList<QStandardItem *> build_eventconfig(EventConfig *config)
{
    auto root = ItemBuilder(ConfigItemType::Event, QIcon(":/vme_event.png"), config->objectName())
        .qObject(config)
        .disableFlags(Qt::ItemIsDragEnabled | Qt::ItemIsEditable)
        .build();

    auto root1 = ItemBuilder(ConfigItemType::Unspecified, info_text(config))
        .build();

    auto modules = ItemBuilder(ConfigItemType::EventModulesInit, QIcon(":/config_category.png"), "Modules Init")
        .qObject(config)
        .disableFlags(Qt::ItemIsDragEnabled | Qt::ItemIsEditable)
        .build();

    for (auto moduleConfig: config->getModuleConfigs())
        modules->appendRow(build_generic(moduleConfig));

    auto readout = ItemBuilder(ConfigItemType::EventReadoutLoop, QIcon(":/config_category.png"), "Readout Loop")
        .qObject(config)
        .disableFlags(Qt::ItemIsDragEnabled | Qt::ItemIsEditable)
        .build();

    auto readout_start = build_generic(config->vmeScripts["readout_start"]);
    readout->appendRow(readout_start);

    for (auto moduleConfig: config->getModuleConfigs())
    {
        auto moduleRow = build_generic(moduleConfig->getReadoutScript());
        moduleRow[0]->setIcon(QIcon(":/vme_module.png"));
        moduleRow[0]->setText(moduleConfig->objectName());
        readout->appendRow(moduleRow);
    }

    auto readout_end = build_generic(config->vmeScripts["readout_end"]);
    readout->appendRow(readout_end);

    for (int i=0; i<readout->rowCount(); ++i)
        disable_flags(readout->child(i), Qt::ItemIsDragEnabled | Qt::ItemIsEditable);

    auto multicast = ItemBuilder(ConfigItemType::EventMulticast, QIcon(":/config_category.png"), "Multicast DAQ Start/Stop")
        .qObject(config)
        .disableFlags(Qt::ItemIsDragEnabled | Qt::ItemIsEditable)
        .build();

    auto mcast_start = build_generic(config->vmeScripts["daq_start"]);
    auto mcast_end = build_generic(config->vmeScripts["daq_stop"]);

    multicast->appendRow(mcast_start);
    multicast->appendRow(mcast_end);

    for (int i=0; i<multicast->rowCount(); ++i)
        disable_flags(multicast->child(i), Qt::ItemIsDragEnabled | Qt::ItemIsEditable);

    root->appendRow(modules);
    root->appendRow(readout);
    root->appendRow(multicast);

    return { root, root1 };
}

QList<QStandardItem *> build_moduleconfig(ModuleConfig *config)
{
    auto root = ItemBuilder(ConfigItemType::Module, QIcon(":/vme_module.png"), config->objectName())
        .qObject(config)
        .build();

    auto reset = build_generic(config->getResetScript());
    root->appendRow(reset);

    for (auto script: config->getInitScripts())
    {
        auto row = build_generic(script);
        root->appendRow(row);
    }

    for (int i=0; i<root->rowCount(); ++i)
        disable_flags(root->child(i), Qt::ItemIsDragEnabled | Qt::ItemIsEditable);

    return { root };
}

QList<QStandardItem *> build_generic(ConfigObject *config)
{
    if (auto multicrate = qobject_cast<mesytec::multi_crate::MulticrateVMEConfig *>(config))
        return build_multicrate(multicrate);

    if (auto vmeConfig = qobject_cast<VMEConfig *>(config))
        return build_vmeconfig(vmeConfig);

    if (auto container = qobject_cast<ContainerObject *>(config))
        return build_container(container);

    if (auto script = qobject_cast<VMEScriptConfig *>(config))
        return build_script(script);

    if (auto eventConfig = qobject_cast<EventConfig *>(config))
        return build_eventconfig(eventConfig);

    if (auto moduleConfig = qobject_cast<ModuleConfig *>(config))
        return build_moduleconfig(moduleConfig);

    return {};
}

std::unique_ptr<QStandardItemModel> build_item_model(ConfigObject *config)
{
    auto model = std::make_unique<QStandardItemModel>();
    auto row = build_generic(config);
    model->invisibleRootItem()->appendRow(row);
    return model;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    mvme_init("dev_vme_config_model_view");

    if (app.arguments().size() < 2)
    {
        std::cerr << "Error: missing config file path argument\n";
        return 1;
    }

    auto doc = read_json_file(app.arguments().at(1));
    auto config = mvme::vme_config::deserialize_object(doc.object());

    if (!config)
    {
        std::cerr << "Error reading config from " << app.arguments().at(1).toLocal8Bit().data() << "\n";
        return 1;
    }

    qDebug() << "read config object: " << config.get();

    #if 0
    auto vmeConfigFilename = QSettings().value("LastVMEConfig").toString();

    if (!vmeConfigFilename.isEmpty())
    {
        QString errString;
        std::tie(vmeConfig, errString) = read_vme_config_from_file(vmeConfigFilename, logger);
        if (!vmeConfig)
            spdlog::error("Error loading vme config from {}: {}",
                          vmeConfigFilename.toStdString(), errString.toStdString());
    }

    if (!vmeConfig)
    {
        vmeConfigFilename = QFileDialog::getOpenFileName(
            nullptr, "Load VME config from file", {}, "VME Configs (*.vme)");
        QString errString;
        std::tie(vmeConfig, errString) = read_vme_config_from_file(vmeConfigFilename, logger);
        if (!vmeConfig)
            spdlog::error("Error loading vme config from {}: {}",
                          vmeConfigFilename.toStdString(), errString.toStdString());
    }

    QSettings().setValue("LastVMEConfig", vmeConfigFilename);
    #endif

#if 1
    auto vmeConfig = qobject_cast<VMEConfig *>(config.get());
    VMEConfigTreeWidget treeWidget;
    if (vmeConfig)
    {
        // old style tree widget
        treeWidget.setConfig(vmeConfig);
        treeWidget.show();
        treeWidget.resize(500, 700);
    }
#endif

#if 1
    QTreeView treeView3;
    auto itemModel = build_item_model(config.get());
    if (itemModel)
    {
        treeView3.setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        treeView3.setDragDropMode(QAbstractItemView::DragDrop);
        treeView3.setModel(itemModel.get());
        treeView3.setRootIndex(itemModel->invisibleRootItem()->child(0)->index());
        treeView3.show();
        treeView3.resize(500, 700);

        QObject::connect(&treeView3, &QTreeView::clicked, [&] (const QModelIndex &index)
        {
            auto item = itemModel->itemFromIndex(index);
            qDebug() << item << item->text() << item->data(DataRole_Pointer)
                << item->data(DataRole_Pointer).value<ConfigObject *>();
        });
    }

#endif

#if 0
    // custom model/view
    QTreeView treeView1;
    treeView1.setWindowTitle("custom model");
    EventModel eventModel1;
    treeView1.setModel(&eventModel1);
    if (vmeConfig->getEventConfigs().size())
        eventModel1.setEventConfig(vmeConfig->getEventConfigs()[0]);
    treeView1.show();
    treeView1.resize(500, 700);

    // QStandardItem model/view
    QTreeView treeView2;
    treeView2.setWindowTitle("QStandardItem model");
    EventModel2 eventModel2;
    treeView2.setModel(&eventModel2);
    if (vmeConfig->getEventConfigs().size())
        eventModel2.setEventConfig(vmeConfig->getEventConfigs()[0]);
    treeView2.show();
    treeView2.resize(500, 700);
#endif


    return app.exec();
}
