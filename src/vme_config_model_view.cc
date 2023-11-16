#include "vme_config_model_view.h"

#include "multi_crate.h"
#include "vme_config_ui.h"

using namespace mesytec::multi_crate;

namespace mesytec::mvme
{

class BaseItem: public QStandardItem
{
    public:
        explicit BaseItem(ConfigItemType type = ConfigItemType::Unspecified)
            : type_(type) {}

        BaseItem(ConfigItemType type, const QString &text)
            : QStandardItem(text), type_(type) {}

        BaseItem(ConfigItemType type, const QIcon &icon, const QString &text)
            : QStandardItem(icon, text), type_(type) {}

        int type() const override { return static_cast<int>(type_); }

        QStandardItem *clone() const override
        {
            auto ret = new BaseItem(type_);
            *ret = *this;
            return ret;
        }

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
            result->setEditable(false);
            result->setDropEnabled(false);
            result->setDragEnabled(false);

            if (obj_)
            {
                result->setData(QVariant::fromValue(reinterpret_cast<quintptr>(obj_)), DataRole_Pointer);

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
        Qt::ItemFlags flagsEnable_ = Qt::NoItemFlags;
        Qt::ItemFlags flagsDisable_ = Qt::NoItemFlags;
};

QList<QStandardItem *> build_generic(ConfigObject *config);

QList<QStandardItem *> build_container(ContainerObject *config)
{
    auto root = ItemBuilder(ConfigItemType::Container)
        .qObject(config)
        .enableFlags(Qt::ItemIsDropEnabled)
        .build();

    for (auto child: config->getChildren())
    {
        root->appendRow(build_generic(child));
    }

    for (int i=0; i<root->rowCount(); ++i)
        enable_flags(root->child(i), Qt::ItemIsDragEnabled);

    return { root };
}

QList<QStandardItem *> build_script(VMEScriptConfig *config)
{
    auto root = ItemBuilder(ConfigItemType::VMEScript, config->objectName())
        .qObject(config)
        .build();

    return { root };
}

QList<QStandardItem *> build_readout_script(VMEScriptConfig *config)
{
    auto moduleName = config->parent() ? config->parent()->objectName() : QString();
    auto root = ItemBuilder(ConfigItemType::VMEScript, QIcon(":/vme_module.png"), moduleName)
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
        .build();

    auto daqStart = build_generic(config->getGlobalStartsScripts());

    auto events = ItemBuilder(ConfigItemType::Events, QIcon(":/mvme_16x16.png"), "Events")
        .qObject(config)
        .build();

    for (auto eventConfig: config->getEventConfigs())
        events->appendRow(build_generic(eventConfig));

    auto daqStop = build_generic(config->getGlobalStopScripts());

    auto manual = build_generic(config->getGlobalManualScripts());

    root->appendRow({ triggerIo, new QStandardItem });
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
        .build();

    auto root1 = ItemBuilder(ConfigItemType::Unspecified, info_text(config))
        .build();

    auto modules = ItemBuilder(ConfigItemType::EventModulesInit, QIcon(":/config_category.png"), "Modules Init")
        .qObject(config)
        .enableFlags(Qt::ItemIsDropEnabled)
        .build();

    for (auto moduleConfig: config->getModuleConfigs())
        modules->appendRow(build_generic(moduleConfig));

    auto readout = ItemBuilder(ConfigItemType::EventReadoutLoop, QIcon(":/config_category.png"), "Readout Loop")
        .qObject(config)
        .build();

    auto readout_start = build_generic(config->vmeScripts["readout_start"]);
    readout->appendRow(readout_start);

    for (auto moduleConfig: config->getModuleConfigs())
    {
        auto moduleRow = build_readout_script(moduleConfig->getReadoutScript());
        readout->appendRow(moduleRow);
    }

    auto readout_end = build_generic(config->vmeScripts["readout_end"]);
    readout->appendRow(readout_end);

    auto multicast = ItemBuilder(ConfigItemType::EventMulticast, QIcon(":/config_category.png"), "Multicast DAQ Start/Stop")
        .qObject(config)
        .build();

    auto mcast_start = build_generic(config->vmeScripts["daq_start"]);
    auto mcast_end = build_generic(config->vmeScripts["daq_stop"]);

    multicast->appendRow(mcast_start);
    multicast->appendRow(mcast_end);

    root->appendRow(modules);
    root->appendRow(readout);
    root->appendRow(multicast);

    return { root, root1 };
}

QList<QStandardItem *> build_moduleconfig(ModuleConfig *config)
{
    auto root = ItemBuilder(ConfigItemType::Module, QIcon(":/vme_module.png"), config->objectName())
        .qObject(config)
        .enableFlags(Qt::ItemIsDragEnabled | Qt::ItemIsEditable)
        .build();

    auto reset = build_generic(config->getResetScript());
    root->appendRow(reset);

    for (auto script: config->getInitScripts())
    {
        auto row = build_generic(script);
        root->appendRow(row);
    }

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

template<typename T> T *qobject_from_pointer(const QVariant &pointer)
{
    if (auto obj = reinterpret_cast<QObject *>(pointer.value<quintptr>()))
    {
        if (auto config = qobject_cast<T *>(obj))
            return config;
    }

    return nullptr;
}

QString config_object_pointers_mimetype()
{
    static const QString mimeType("application/x-mvme-config-object-pointers");
    return mimeType;
}

QMimeData *encode_config_object_pointers(const QStandardItemModel *model, const QModelIndexList &indexes)
{
    QVector<QVariant> pointers;

    for (const auto &index: indexes)
    {
        if (auto item = model->itemFromIndex(index))
        {
            if (item->data(DataRole_Pointer).isValid())
            {
                pointers.push_back(item->data(DataRole_Pointer));
            }
        }
    }

    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream << pointers;

    auto result = new QMimeData;
    result->setData(config_object_pointers_mimetype(), buffer);
    return result;
}

QVector<ConfigObject *> decode_config_object_pointers(const QMimeData *mimeData)
{
    auto data = mimeData->data(config_object_pointers_mimetype());
    QVector<QVariant> pointers;
    QDataStream stream(&data, QIODevice::ReadOnly);
    stream >> pointers;
    QVector<ConfigObject *> result;

    for (const auto &pointer: pointers)
    {
        if (auto config = qobject_from_pointer<ConfigObject>(pointer))
            result.push_back(config);
    }

    return result;
}

void VmeConfigItemModel::setRootObject(ConfigObject *obj)
{
    clear();
    setItemPrototype(new BaseItem);
    setHorizontalHeaderLabels({ "Object", "Info"});
    auto row = build_generic(obj);
    invisibleRootItem()->appendRow(row);
    auto oldRoot = rootObject_;
    rootObject_ = obj;
    emit rootObjectChanged(oldRoot, rootObject_);
}

QStringList VmeConfigItemModel::mimeTypes() const
{
    return { config_object_pointers_mimetype() };
}

QMimeData *VmeConfigItemModel::mimeData(const QModelIndexList &indexes) const
{
    return encode_config_object_pointers(this, indexes);
}

bool VmeConfigItemModel::canDropMimeData(const QMimeData *data, Qt::DropAction action,
                        int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(action); Q_UNUSED(row); Q_UNUSED(column); Q_UNUSED(parent);
    qDebug() << __PRETTY_FUNCTION__ << data << data->formats();

    if (!data->hasFormat(config_object_pointers_mimetype()))
        return false;

    return true;
}

bool VmeConfigItemModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                    int row, int /*column*/, const QModelIndex &parent)
{
    qDebug() << __PRETTY_FUNCTION__
        << ", parentIndex=" << parent
        << ", data=" << data << data->formats()
        << ", action=" << action;

    if (!data->hasFormat(config_object_pointers_mimetype()))
        return false;

    auto objects = decode_config_object_pointers(data);
    qDebug() << "objects =" << objects;

    if (objects.isEmpty())
        return false;

    auto sourceObject = objects.first();
    auto destParentItem = itemFromIndex(parent);

    if (!destParentItem)
        return false;

    auto destParentObject = qobject_from_pointer<ConfigObject>(
        destParentItem->data(DataRole_Pointer));

    if (!destParentObject)
        return false;

    qDebug() << "sourceObject =" << sourceObject << "destParentObject =" << destParentObject;

    if (destParentItem->type() == static_cast<int>(ConfigItemType::EventModulesInit))
    {
        auto destEvent = qobject_cast<EventConfig *>(destParentObject);
        auto sourceModule = qobject_cast<ModuleConfig *>(sourceObject);

        if (destEvent && sourceModule)
        {
            qDebug() << "move module" << sourceModule << "to event" << destEvent << ", destRow =" << row;
            move_module(sourceModule, destEvent, row);
        }
    }

    // Always return false to stop QStandardItemModel from updating itself.
    return false;
}

VmeConfigItemModel::~VmeConfigItemModel()
{
}

void VmeConfigItemModelController::setModel(VmeConfigItemModel *model)
{
    if (model_)
        model_->disconnect(this);

    connect(model, &VmeConfigItemModel::rootObjectChanged,
        this, [this] (ConfigObject *oldRoot, ConfigObject *newRoot)
        {
            if (oldRoot)
                disconnectFromObjects(oldRoot);

            if (newRoot)
                connectToObjects(newRoot);
        });

    model_ = model;
}

void VmeConfigItemModelController::onCrateAdded(VMEConfig *config)
{
}

void VmeConfigItemModelController::onCrateAboutToBeRemoved(VMEConfig *config)
{
}

void VmeConfigItemModelController::onEventAdded(EventConfig *config)
{
}

void VmeConfigItemModelController::onEventAboutToBeRemoved(EventConfig *config)
{
}

template<typename Predicate>
void find_items(QStandardItem *root, Predicate p, QVector<QStandardItem *> &result)
{
    if (p(root))
        result.push_back(root);

    for (int row = 0; row < root->rowCount(); ++row)
        find_items(root->child(row), p, result);
}

template<typename Predicate>
QVector<QStandardItem *> find_items(QStandardItem *root, Predicate p)
{
    QVector<QStandardItem *> result;
    find_items(root, p, result);
    return result;
}

template<typename T>
auto make_predicate_pointer_and_item_type(T *pointer, int itemType)
{
    return [=] (QStandardItem *item)
    {
        auto thePointer = qobject_from_pointer<T>(item->data(DataRole_Pointer));
        auto theType = item->type();
        return thePointer == pointer && theType == itemType;
    };
}

template<typename T>
auto make_predicate_pointer_and_item_type(T *pointer, ConfigItemType itemType)
{
    return make_predicate_pointer_and_item_type(pointer, static_cast<int>(itemType));
}

void VmeConfigItemModelController::onModuleAdded(ModuleConfig *config, int /*index*/)
{
    connectToObjects(config);

    // add the module nodes
    {
        auto pred = make_predicate_pointer_and_item_type(
            config->getEventConfig(), ConfigItemType::EventModulesInit);
        auto items = find_items(model_->invisibleRootItem(), pred);

        if (auto eventConfig = config->getEventConfig())
        {
            auto destIndex = eventConfig->getModuleConfigs().indexOf(config);
            qDebug() << "insert dest index =" << destIndex;
            for (auto item: items)
            {
                item->insertRow(destIndex, build_generic(config));
            }
        }
    }

    // add the readout script nodes
    {
        auto pred = make_predicate_pointer_and_item_type(
            config->getEventConfig(), ConfigItemType::EventReadoutLoop);
        auto items = find_items(model_->invisibleRootItem(), pred);

        if (auto eventConfig = config->getEventConfig())
        {
            // +1 to account for the "Cycle Start" node
            auto destIndex = eventConfig->getModuleConfigs().indexOf(config) + 1;
            for (auto item: items)
            {
                item->insertRow(destIndex, build_readout_script(config->getReadoutScript()));
            }
        }
    }
}

void delete_item_rows(const QVector<QStandardItem *> &items)
{
    for (auto item : items)
    {
        if (item->parent() && item->row() >= 0)
        {
            auto row = item->parent()->takeRow(item->row());
            qDeleteAll(row);
        }
    }
}

void VmeConfigItemModelController::onModuleAboutToBeRemoved(ModuleConfig *config)
{
    disconnectFromObjects(config);

    // remove the module nodes
    {
        auto pred = make_predicate_pointer_and_item_type(config, ConfigItemType::Module);
        auto items = find_items(model_->invisibleRootItem(), pred);
        delete_item_rows(items);
    }

    // remove the readout script nodes
    {
        auto pred = make_predicate_pointer_and_item_type(
            config->getReadoutScript(), ConfigItemType::VMEScript);
        auto items = find_items(model_->invisibleRootItem(), pred);
        delete_item_rows(items);
    }
}

void VmeConfigItemModelController::onObjectEnabledChanged(bool enabled)
{
}

void VmeConfigItemModelController::connectToObjects(ConfigObject *root)
{
    if (auto multicrate = qobject_cast<MulticrateVMEConfig *>(root))
    {
        connect(multicrate, &MulticrateVMEConfig::crateConfigAdded,
            this, &VmeConfigItemModelController::onCrateAdded);
        connect(multicrate, &MulticrateVMEConfig::crateConfigAboutToBeRemoved,
            this, &VmeConfigItemModelController::onCrateAboutToBeRemoved);
    }

    if (auto vmeConfig = qobject_cast<VMEConfig *>(root))
    {
        connect(vmeConfig, &VMEConfig::eventAdded,
            this, &VmeConfigItemModelController::onEventAdded);
        connect(vmeConfig, &VMEConfig::eventAboutToBeRemoved,
            this, &VmeConfigItemModelController::onEventAboutToBeRemoved);
    }

    if (auto eventConfig = qobject_cast<EventConfig *>(root))
    {
        connect(eventConfig, &EventConfig::moduleAdded,
            this, &VmeConfigItemModelController::onModuleAdded);
        connect(eventConfig, &EventConfig::moduleAboutToBeRemoved,
            this, &VmeConfigItemModelController::onModuleAboutToBeRemoved);
    }

    for (auto child: root->findChildren<ConfigObject *>(QString(), Qt::FindDirectChildrenOnly))
        connectToObjects(child);
}

void VmeConfigItemModelController::disconnectFromObjects(ConfigObject *root)
{
    root->disconnect(this);

    for (auto child: root->findChildren<ConfigObject *>())
        child->disconnect(this);
}

}
