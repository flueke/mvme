#include "histo_stats_widget.h"
#include "histo_stats_widget_p.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QFormLayout>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <vector>

#include "qt_util.h"

HistoStatsTableModel::~HistoStatsTableModel()
{
}

struct HistoStatsWidget::Private
{
    HistoStatsWidget *q = {};
    std::vector<Entry> entries_;
    QwtScaleDiv xScaleDiv_;

    QComboBox *combo_resolution_ = {};
    QTableView *tableView_ = {};
    std::unique_ptr<QStandardItemModel> itemModel_;
    QLabel *label_infoHistos = {};
    QLabel *label_infoXInterval = {};

    void repopulate();
    void refresh();
};

HistoStatsWidget::HistoStatsWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;

    auto toolBar = new QToolBar;
    auto statusBar = new QStatusBar;
    d->combo_resolution_ = new QComboBox;
    d->tableView_ = new QTableView;
    d->tableView_->verticalHeader()->hide();
    d->label_infoHistos = new QLabel;
    d->label_infoXInterval = new QLabel;
    auto gb_info = new QGroupBox;
    auto l_info = new QFormLayout(gb_info);
    l_info->addRow(d->label_infoHistos);
    l_info->addRow(d->label_infoXInterval);

    auto l = make_vbox<2, 2>(this);
    // TODO: toolbar frame
    l->addWidget(toolBar);
    l->addWidget(gb_info);
    l->addWidget(d->tableView_);
    l->addWidget(statusBar);
    l->setStretch(2, 1);

    auto actionExport = toolBar->addAction("Export");
    auto actionPrint = toolBar->addAction("Print");

    auto refreshTimer = new QTimer(this);
    refreshTimer->setInterval(1000);
    refreshTimer->start();
    connect(refreshTimer, &QTimer::timeout,
            this, [this] { d->refresh(); });
}

HistoStatsWidget::~HistoStatsWidget()
{
}

void HistoStatsWidget::addSink(const SinkPtr &sink)
{
    SinkEntry e{sink};
    d->entries_.emplace_back(e);
    d->repopulate();
}

void HistoStatsWidget::addHistogram(const std::shared_ptr<Histo1D> &histo)
{
    HistoEntry e{histo};
    d->entries_.emplace_back(e);
    d->repopulate();
}

void HistoStatsWidget::setXScaleDiv(const QwtScaleDiv &scaleDiv)
{
    d->xScaleDiv_ = scaleDiv;
    d->refresh();
}

// helper type for the visitor #4
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
// explicit deduction guide (not needed as of C++20)
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

void HistoStatsWidget::Private::repopulate()
{
    auto rowCountVisitor = overloaded
    {
        [] (const SinkEntry &e) { return e.sink->getNumberOfHistos(); },
        [] (const HistoEntry &) { return 1; },
    };

    auto rowCount = std::accumulate(
        std::begin(entries_), std::end(entries_), 0,
        [&rowCountVisitor](auto accu, const auto &entry)
        { return accu + std::visit(rowCountVisitor, entry); });

    QStringList hLabels{ "Histo #", "EntryCount", "Mean", "RMS", "Gauss Mean", "FWHM"};
    auto model = std::make_unique<QStandardItemModel>(rowCount, hLabels.size());
    model->setHorizontalHeaderLabels(hLabels);

    for (int row = 0; row < model->rowCount(); ++row)
    {
        for (int column = 0; column < model->columnCount(); ++column)
        {
            auto item = new QStandardItem;
            item->setEditable(false);
            item->setDragEnabled(false);
            item->setDropEnabled(false);
            model->setItem(row, column, item);
        }
    }

    auto sm = tableView_->selectionModel();
    tableView_->setModel(model.get());
    sm->deleteLater();
    std::swap(itemModel_, model);
    refresh();
}

void HistoStatsWidget::Private::refresh()
{
    auto collectStatsVisitor = overloaded
    {
        [this] (const SinkEntry &e)
        {
            std::vector<Histo1DStatistics> stats;
            auto histos = e.sink->getHistos();
            std::transform(
                std::begin(histos), std::end(histos), std::back_inserter(stats),
                [this](const auto &histo)
                { return histo->calcStatistics(xScaleDiv_.lowerBound(), xScaleDiv_.upperBound()); });
            return stats;
        },
        [this] (const HistoEntry &e)
        {
            return std::vector<Histo1DStatistics>
            {
                e.histo->calcStatistics(xScaleDiv_.lowerBound(), xScaleDiv_.upperBound())
            };
        },
    };

    std::vector<Histo1DStatistics> allStats;

    for (const auto &entry: entries_)
    {
        auto stats = std::visit(collectStatsVisitor, entry);
        std::move(std::begin(stats), std::end(stats), std::back_inserter(allStats));
    }

    // FIXME: add the effective bin count here once it's implemented
    label_infoHistos->setText(QSL("Number of histograms: %1, Effective Resolution: %2")
        .arg(allStats.size()).arg("FIXME"));
    label_infoXInterval->setText(QSL("X-Axis Interval: [%1, %2)")
        .arg(xScaleDiv_.lowerBound()).arg(xScaleDiv_.upperBound()));

    int row = 0;
    for (const auto &stats: allStats)
    {
        int col = 0;

        itemModel_->item(row, col++)->setData(row, Qt::DisplayRole);
        itemModel_->item(row, col++)->setData(stats.entryCount, Qt::DisplayRole);

        if (stats.entryCount)
        {
            itemModel_->item(row, col++)->setData(stats.mean, Qt::DisplayRole);
            itemModel_->item(row, col++)->setData(stats.sigma, Qt::DisplayRole);
            itemModel_->item(row, col++)->setData(stats.fwhmCenter, Qt::DisplayRole);
            itemModel_->item(row, col++)->setData(stats.fwhm, Qt::DisplayRole);
        }
        else while (col < itemModel_->columnCount())
                itemModel_->item(row, col++)->setData({}, Qt::DisplayRole);
        ++row;
    }

    tableView_->resizeColumnsToContents();
    tableView_->resizeRowsToContents();
}