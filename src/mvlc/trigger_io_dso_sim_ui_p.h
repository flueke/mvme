#ifndef __MVME_MVLC_TRIGGER_IO_SIM_UI_P_H__
#define __MVME_MVLC_TRIGGER_IO_SIM_UI_P_H__

#include <QHeaderView>
#include <QStandardItemModel>

#include "mvlc/trigger_io_sim_ui.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

static const int PinRole = Qt::UserRole + 1;
static const int ColUnit = 0; // unit name/path
static const int ColName = 1; // user defined pin name

class BaseModel: public QStandardItemModel
{
    Q_OBJECT
    public:
        using QStandardItemModel::QStandardItemModel;

        void setTriggerIO(const TriggerIO &trigIO)
        {
            beginResetModel();
            m_trigIO = trigIO;
            endResetModel();
        }

        const TriggerIO &triggerIO() const
        {
            return m_trigIO;
        }

        QStringList pinPathList(const PinAddress &pa) const;
        QString pinPath(const PinAddress &pa) const;
        QString pinName(const PinAddress &pa) const;
        QString pinUserName(const PinAddress &pa) const;

    private:
        TriggerIO m_trigIO; // not efficient at all as both models keep a full copy of the trigger io setup...
};

class TraceTreeModel: public BaseModel
{
    Q_OBJECT
    public:
        using BaseModel::BaseModel;

    QStandardItem *samplesRoot = nullptr;
};

class TraceTableModel: public BaseModel
{
    Q_OBJECT
    public:
        using BaseModel::BaseModel;

        bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                          int row, int /*column*/, const QModelIndex &parent) override
        {
            qDebug() << __PRETTY_FUNCTION__;
            // Pretend to always move column 0 to circumvent QStandardItemModel
            // from adding columns.
            return QStandardItemModel::dropMimeData(data, action, row, 0, parent);
        }
};

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_SIM_UI_P_H__ */
