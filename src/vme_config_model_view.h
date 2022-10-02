#ifndef __MVME_VME_CONFIG_MODEL_VIEW_H__
#define __MVME_VME_CONFIG_MODEL_VIEW_H__

#include <QAbstractItemModel>
#include <QTreeView>
#include <QStandardItemModel>
#include "vme_config.h"
#include "libmvme_export.h"

namespace mesytec
{
namespace mvme
{

class LIBMVME_EXPORT EventModel: public QAbstractItemModel
{
    Q_OBJECT
    public:
        EventModel(QObject *parent = nullptr)
            : QAbstractItemModel(parent)
        { }
        ~EventModel() override;

        void setEventConfig(EventConfig *eventConfig)
        {
            beginResetModel();
            m_event = eventConfig;
            endResetModel();
        }

        QModelIndex index(int row, int column,
                          const QModelIndex &parent = QModelIndex()) const override
        {
            if (!parent.isValid())
                return createIndex(row, column, m_event);

            if (parent.internalPointer() == m_event
                && parent.row() < static_cast<int>(EventChildNodes.size()))
                // FIXME: bad cast
                return createIndex(row, column, (void *)&EventChildNodes[parent.row()]);

            return {};
        }

        QModelIndex parent(const QModelIndex &child) const override
        {
            if (!child.isValid() || child.internalPointer() == m_event)
                return {};

            auto it = std::find_if(
                std::begin(EventChildNodes), std::end(EventChildNodes),
                [&child] (const QString &str)
                {
                    // FIXME: bad cast
                    return child.internalPointer() == (void *)&str;
                });

            if (it != std::end(EventChildNodes))
                return createIndex(0, 0, m_event);

            return {};
        }

        int rowCount(const QModelIndex &parent = QModelIndex()) const override
        {
            if (!parent.isValid())
                return 1;

            if (parent.internalPointer() == m_event)
                return EventChildNodes.size();

            return 0;
        }

        int columnCount(const QModelIndex &parent = QModelIndex()) const override
        {
            if (!parent.isValid())
                return 2;

            return 2;
        }

        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
        {
            if (!m_event)
                return {};

            int row = index.row();
            int col = index.column();

            if (!parent(index).isValid())
            {
                if (col == 0)
                {
                    if (role == Qt::DisplayRole)
                        return m_event->objectName();
                    else if (role == Qt::DecorationRole)
                        return QIcon(":/vme_event.png");
                }
            }

            if (parent(index).isValid()
                && parent(index).internalPointer() == m_event
                && row < static_cast<int>(EventChildNodes.size())
                && col == 0)
            {
                if (role == Qt::DisplayRole)
                    return EventChildNodes[row];
                else if (role == Qt::DecorationRole)
                    return QIcon(":/folder_orange.png");
            }

            return {};
        }

    private:
        static const std::vector<QString> EventChildNodes;

        EventConfig *m_event = nullptr;
};

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

class LIBMVME_EXPORT VmeConfigView: public QTreeView
{
    Q_OBJECT
    public:
        VmeConfigView(QObject *parent = nullptr);
};

}
}

#endif /* __MVME_VME_CONFIG_MODEL_VIEW_H__ */
