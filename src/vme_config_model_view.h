#ifndef __MVME_VME_CONFIG_MODEL_VIEW_H__
#define __MVME_VME_CONFIG_MODEL_VIEW_H__

#include <QAbstractItemModel>
#include <QTreeView>
#include <QStandardItemModel>
#include <QMimeData>
#include "vme_config.h"
#include "libmvme_export.h"

namespace mesytec::mvme
{

class LIBMVME_EXPORT EventModel2: public QStandardItemModel
{
    Q_OBJECT
    public:
        EventModel2(QObject *parent = nullptr)
            : QStandardItemModel(parent)
            , m_eventRoot(
                new QStandardItem(QIcon(":/vme_event.png"), QSL("Event")))
            , m_eventInfo(
                new QStandardItem)
            , m_modulesInitRoot(
                new QStandardItem(QIcon(":/folder_orange.png"), QSL("Modules Init")))
            , m_readoutLoopRoot(
                new QStandardItem(QIcon(":/folder_orange.png"), QSL("Readout Loop")))
            , m_multicastRoot(
                new QStandardItem(QIcon(":/folder_orange.png"), QSL("Multicast DAQ Start/Stop")))
        {
            m_eventRoot->appendRow(m_modulesInitRoot);
            m_eventRoot->appendRow(m_readoutLoopRoot);
            m_eventRoot->appendRow(m_multicastRoot);
            invisibleRootItem()->appendRow({ m_eventRoot, m_eventInfo });
        }

        void setEventConfig(EventConfig *eventConfig)
        {
            beginResetModel();
            if (m_event)
                m_event->disconnect(this);

            m_event = eventConfig;

            // TODO: updating the item texts must happen dynamically when the eventconfig or any children are modified
            m_eventRoot->setText(m_event->objectName());

            QString infoText;

            switch (eventConfig->triggerCondition)
            {
                case TriggerCondition::Interrupt:
                    {
                        infoText = QString("Trigger=IRQ%1")
                            .arg(eventConfig->irqLevel);
                    } break;
                case TriggerCondition::NIM1:
                    {
                        infoText = QSL("Trigger=NIM");
                    } break;
                case TriggerCondition::Periodic:
                    {
                        infoText = QSL("Trigger=Periodic");
                        if (auto vmeConfig = eventConfig->getVMEConfig())
                        {
                            if (is_mvlc_controller(vmeConfig->getControllerType()))
                            {
                                auto tp = eventConfig->getMVLCTimerPeriod();
                                infoText += QSL(", every %1%2").arg(tp.first).arg(tp.second);
                            }
                        }
                    } break;
                default:
                    {
                        infoText = QString("Trigger=%1")
                            .arg(TriggerConditionNames.value(eventConfig->triggerCondition));
                    } break;
            }
            m_eventInfo->setText(infoText);
            endResetModel();
        }

    private:
        EventConfig *m_event = nullptr;
        QStandardItem *m_eventRoot,
                      *m_eventInfo,
                      *m_modulesInitRoot,
                      *m_readoutLoopRoot,
                      *m_multicastRoot;
        std::vector<std::vector<QStandardItem *>> m_moduleRoots;
};

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

QString config_object_pointers_mimetype();

class VmeConfigItemModel: public QStandardItemModel
{
    Q_OBJECT
    signals:
        void rootObjectChanged(ConfigObject *oldRoot, ConfigObject *root);

    public:
        using QStandardItemModel::QStandardItemModel;
        ~VmeConfigItemModel() override;

        void setRootObject(ConfigObject *obj);

        QStringList mimeTypes() const override;

        QMimeData *mimeData(const QModelIndexList &indexes) const override;

        bool canDropMimeData(const QMimeData *data, Qt::DropAction action,
                             int row, int column, const QModelIndex &parent) const override;

        bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                          int row, int /*column*/, const QModelIndex &parent) override;

    private:
        ConfigObject *rootObject_ = nullptr;
};

class VmeConfigItemController: public QObject
{
    Q_OBJECT
    public:
        using QObject::QObject;
        void setModel(VmeConfigItemModel *model);
        void addView(QTreeView *view);

    private slots:
        void onCrateAdded(VMEConfig *config);
        void onCrateAboutToBeRemoved(VMEConfig *config);
        void onEventAdded(EventConfig *config);
        void onEventAboutToBeRemoved(EventConfig *config);
        void onModuleAdded(ModuleConfig *config, int index = -1);
        void onModuleAboutToBeRemoved(ModuleConfig *config);
        void onObjectEnabledChanged(bool enabled);

    private:
        void connectToObjects(ConfigObject *root);
        void disconnectFromObjects(ConfigObject *root);
        VmeConfigItemModel *model_ = nullptr;
        QVector<QTreeView *> views_;
};

#if 0
class LIBMVME_EXPORT VmeConfigView: public QTreeView
{
    Q_OBJECT
    public:
        VmeConfigView(QObject *parent = nullptr);
};
#endif

}

#endif /* __MVME_VME_CONFIG_MODEL_VIEW_H__ */
