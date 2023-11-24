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

enum ConfigItemType
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

class VmeConfigItemModel: public QStandardItemModel
{
    Q_OBJECT
    signals:
        void rootObjectChanged(ConfigObject *root);

    public:
        using QStandardItemModel::QStandardItemModel;
        ~VmeConfigItemModel() override;

        void setRootObject(ConfigObject *obj);
        ConfigObject *getRootObject();

        QStringList mimeTypes() const override;

        QMimeData *mimeData(const QModelIndexList &indexes) const override;

        bool canDropMimeData(const QMimeData *data, Qt::DropAction action,
                             int row, int column, const QModelIndex &parent) const override;

        bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                          int row, int /*column*/, const QModelIndex &parent) override;

    private:
        ConfigObject *rootObject_ = nullptr;
};

void update_info_texts(VmeConfigItemModel *model);

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

class VmeConfigTreeView: public QTreeView
{
    Q_OBJECT
    public:
        VmeConfigTreeView(QWidget *parent = nullptr)
            : QTreeView(parent)
        {
            setEditTriggers(QAbstractItemView::EditKeyPressed);
            setDefaultDropAction(Qt::MoveAction); // internal DnD
            setDragDropMode(QAbstractItemView::DragDrop); // external DnD
            setDragDropOverwriteMode(false);
            setDragEnabled(true);
            show();
            resize(500, 700);
            resizeColumnToContents(0);
        }
};

}

#endif /* __MVME_VME_CONFIG_MODEL_VIEW_H__ */
