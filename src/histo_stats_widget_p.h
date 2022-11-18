#ifndef __MVME2_SRC_HISTO_STATS_WIDGET_P_H_
#define __MVME2_SRC_HISTO_STATS_WIDGET_P_H_

#include "histo_stats_widget.h"

#include <QStandardItemModel>
#include <variant>

struct SinkEntry
{
    HistoStatsWidget::SinkPtr sink;
};

struct HistoEntry
{
    std::shared_ptr<Histo1D> histo;
};

using Entry = std::variant<SinkEntry, HistoEntry>;

class HistoStatsTableModel: public QStandardItemModel
{
    Q_OBJECT
    public:
        using QStandardItemModel::QStandardItemModel;
        ~HistoStatsTableModel() override;
};

class TableDataProvider
{
    public:
        virtual ~TableDataProvider() {}
        virtual QVariant data(int column, int role) const = 0;
};

class TableDataItem: public QStandardItem
{
    public:
        TableDataItem(TableDataProvider *provider)
            : QStandardItem()
            , provider_(provider)
        {}

        QVariant data(int role = Qt::UserRole + 1) const override
        {
            return provider_->data(column(), role);
        }

    private:
        TableDataProvider *provider_;
};

struct BinCountVisitor
{
    s32 operator()(const SinkEntry &e) const { return e.sink->getHistoBins(); }
    s32 operator()(const HistoEntry &e) const { return static_cast<s32>(e.histo->getNumberOfBins()); }
};

#endif // __MVME2_SRC_HISTO_STATS_WIDGET_P_H_