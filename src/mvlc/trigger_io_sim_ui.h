#ifndef __MVME_MVLC_TRIGGER_IO_SIM_UI_H__
#define __MVME_MVLC_TRIGGER_IO_SIM_UI_H__

#include <memory>
#include <QHeaderView>
#include <QMetaType>
#include <QStandardItemModel>
#include <QTableView>
#include <QTreeView>
#include <QDebug>

#include "mvlc/mvlc_trigger_io.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

enum class PinPosition
{
    Input,
    Output
};

struct PinAddress
{
    PinAddress() {}

    PinAddress(const UnitAddress &unit_, const PinPosition &pos_)
        : unit(unit_)
        , pos(pos_)
    {}

    PinAddress(const PinAddress &) = default;
    PinAddress &operator=(const PinAddress &) = default;

#if 0
    bool operator==(const PinAddress &o) const
    {
        qDebug() << __PRETTY_FUNCTION__;
        return unit == o.unit
            && pos == o.pos;
    }

    bool operator!=(const PinAddress &o) const
    {
        qDebug() << __PRETTY_FUNCTION__;
        return !(*this == o);
    }

    bool operator<(const PinAddress &o) const
    {
        qDebug() << __PRETTY_FUNCTION__;
        if (this->unit < o.unit)
            return true;
        if (this->unit > o.unit)
            return false;
        return this->pos < o.pos;
    }
#endif

    UnitAddress unit = { 0, 0, 0 };
    PinPosition pos = PinPosition::Input;
};

class TraceTreeModel: public QStandardItemModel
{
    public:
        TraceTreeModel(QObject *parent = nullptr)
            : QStandardItemModel(parent)
        {
        }
};

class TraceTableModel: public QStandardItemModel
{
    public:
        TraceTableModel(QObject *parent = nullptr)
            : QStandardItemModel(parent)
        {
        }

        bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                          int row, int /*column*/, const QModelIndex &parent) override
        {
            // Pretend to always move column 0 to circumvent QStandardItemModel from adding columns.
            return QStandardItemModel::dropMimeData(data, action, row, 0, parent);
        }
};

class TraceTreeView: public QTreeView
{
    public:
        TraceTreeView(QWidget *parent = nullptr)
            : QTreeView(parent)
        {
            setExpandsOnDoubleClick(true);
            setDragEnabled(true);
        }
};

class TraceTableView: public QTableView
{
    public:
        TraceTableView(QWidget *parent = nullptr)
            : QTableView(parent)
        {
            setSelectionMode(QAbstractItemView::SingleSelection);
            setSelectionBehavior(QAbstractItemView::SelectRows);
            setDefaultDropAction(Qt::MoveAction);
            setDragDropMode(QAbstractItemView::DragDrop);
            setDragDropOverwriteMode(false);
            setDragEnabled(true);
            verticalHeader()->hide();
        }
};

std::unique_ptr<TraceTreeModel> make_trace_tree_model();
std::unique_ptr<TraceTableModel> make_trace_table_model();

#if 0
class TraceSelectWidget: public QWidget
{
    Q_OBJECT

    signals:
        void selectionChanged(const QVector<PinAddress> &selection);

    public:
        TraceSelectWidget(QWidget *parent = nullptr);
        ~TraceSelectWidget() override;

        void setTriggerIO(const TriggerIO &trigIO);
        void setSelection(const QVector<PinAddress> &selection);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};
#endif

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

Q_DECLARE_METATYPE(mesytec::mvme_mvlc::trigger_io::PinAddress);

QDataStream &operator<<(QDataStream &out, const mesytec::mvme_mvlc::trigger_io::PinAddress &pin);
QDataStream &operator>>(QDataStream &in, mesytec::mvme_mvlc::trigger_io::PinAddress &pin);

QDebug operator<<(QDebug dbg, const mesytec::mvme_mvlc::trigger_io::PinAddress &pin);

#endif /* __MVME_MVLC_TRIGGER_IO_SIM_UI_H__ */
