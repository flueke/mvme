#include "histo_stats_widget.h"
#include "histo_stats_widget_p.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QFormLayout>
#include <QGridLayout>
#include <QMenu>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <vector>

#include "histo1d_widget.h"
#include "qt_util.h"
#include "util/cpp17_util.h"

HistoStatsTableModel::~HistoStatsTableModel()
{
}

struct HistoStatsWidget::Private
{
    HistoStatsWidget *q = {};
    std::vector<Entry> entries_;
    QwtScaleDiv xScaleDiv_;
    // Resolution in number of bins used for statistics calculation.
    s32 selectedResolution_ = 0;

    QComboBox *combo_resolution_ = {};
    QTableView *tableView_ = {};
    std::unique_ptr<QStandardItemModel> itemModel_;
    QLabel *label_info_ = {};

    void repopulate(); // Recreates the table and model. Used when entries are added/removed.
    void refresh(); // Recalculates the statistics and fills the table view with data.
    void handleTableContextMenu(const QPoint &pos);
    void showColumnHistogram(const int col);
};

HistoStatsWidget::HistoStatsWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;

    auto toolBar = make_toolbar();
    auto statusBar = make_statusbar();
    d->combo_resolution_ = new QComboBox;
    d->combo_resolution_->setSizeAdjustPolicy(QComboBox::SizeAdjustPolicy::AdjustToContents);
    set_widget_font_pointsize(d->combo_resolution_, 7);
    auto resolutionComboContainer = make_vbox_container(
        QSL("Effective Resolution"), d->combo_resolution_, 0, -2);
    set_widget_font_pointsize(resolutionComboContainer.label, 7);

    d->tableView_ = new QTableView;
    d->tableView_->verticalHeader()->hide();
    d->tableView_->setContextMenuPolicy(Qt::ContextMenuPolicy::CustomContextMenu);

    d->label_info_ = new QLabel;
    d->label_info_->setWordWrap(true);
    d->label_info_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto gb_info = new QGroupBox;
    auto l_info = make_vbox<2, 2>(gb_info);
    l_info->addWidget(d->label_info_);

    auto toolBarFrame = new QFrame;
    toolBarFrame->setFrameStyle(QFrame::StyledPanel);
    make_hbox<0, 0>(toolBarFrame)->addWidget(toolBar);

    auto l = make_vbox<2, 2>(this);
    l->addWidget(toolBarFrame);
    l->addWidget(gb_info);
    l->addWidget(d->tableView_);
    l->addWidget(statusBar);
    l->setStretch(2, 1);

    auto actionExport = toolBar->addAction("Export");
    auto actionPrint = toolBar->addAction("Print");
    toolBar->addWidget(resolutionComboContainer.container.release());

    connect(d->combo_resolution_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]
            {
                d->selectedResolution_ = d->combo_resolution_->currentData().toInt();
                d->refresh();
            });

    connect(d->tableView_, &QWidget::customContextMenuRequested,
            this, [this] (const QPoint &pos) { d->handleTableContextMenu(pos); });

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

void HistoStatsWidget::setEffectiveResolution(s32 binCount)
{
    if (auto idx = d->combo_resolution_->findData(binCount);
        idx >= 0)
    {
        d->combo_resolution_->setCurrentIndex(idx);
    }
}

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

    auto currentRes = combo_resolution_->currentData().toInt();
    combo_resolution_->clear();

    s32 maxBins = std::accumulate(
        std::begin(entries_), std::end(entries_), 0,
        [this] (s32 accu, const auto &entry)
        { return std::max(accu, std::visit(BinCountVisitor(), entry)); });


    for (u32 bits=4; bits<=std::ceil(std::log2(maxBins)); ++bits)
    {
        u32 value = 1u << bits;
        auto text = QSL("%1 (%2 bit)").arg(value).arg(bits);
        combo_resolution_->addItem(text, value);
    }

    if (int idx = combo_resolution_->findData(currentRes); idx >= 0)
        combo_resolution_->setCurrentIndex(idx);
    else
        combo_resolution_->setCurrentIndex(combo_resolution_->count() - 1);

    refresh();
}

namespace
{
    // Calculate the resolution reduction factor based on the histo bins and the
    // desired maxBins value.
    u32 calculate_rrf(s32 bins, s32 maxBins)
    {
        if (bins > maxBins && maxBins > 0)
            return bins / maxBins;
        return 0;
    }
}

void HistoStatsWidget::Private::refresh()
{
    auto collectStatsVisitor = overloaded
    {
        [this] (const SinkEntry &e)
        {
            u32 rrf = calculate_rrf(BinCountVisitor()(e), selectedResolution_);
            std::vector<Histo1DStatistics> stats;
            auto histos = e.sink->getHistos();

            std::transform(
                std::begin(histos), std::end(histos), std::back_inserter(stats),
                [this, rrf](const auto &histo)
                { return histo->calcStatistics(xScaleDiv_.lowerBound(), xScaleDiv_.upperBound(), rrf); });

            return stats;
        },
        [this] (const HistoEntry &e)
        {
            u32 rrf = calculate_rrf(BinCountVisitor()(e), selectedResolution_);
            return std::vector<Histo1DStatistics>
            {
                e.histo->calcStatistics(xScaleDiv_.lowerBound(), xScaleDiv_.upperBound(), rrf)
            };
        },
    };

    auto allStats = std::accumulate(
        std::begin(entries_), std::end(entries_),
        std::vector<Histo1DStatistics>{},
        [&collectStatsVisitor](auto &accu, const auto &entry)
        {
            auto stats = std::visit(collectStatsVisitor, entry);
            std::move(std::begin(stats), std::end(stats), std::back_inserter(accu));
            return accu;
        });

    QStringList infoRows =
    {
        QSL("* Number of histograms: %1").arg(allStats.size()),

        QSL("* Effective Resolution: %1 (%2 bit)")
            .arg(combo_resolution_->currentData().toInt())
            .arg(std::ceil(std::log2(combo_resolution_->currentData().toInt()))),

        QSL("* X-Axis Interval: [%1, %2)").arg(xScaleDiv_.lowerBound()).arg(xScaleDiv_.upperBound()),
    };

    label_info_->setText(infoRows.join("\n"));

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

void HistoStatsWidget::Private::handleTableContextMenu(const QPoint &pos)
{
    auto sm = tableView_->selectionModel();

    if (!sm->currentIndex().isValid())
        return;

    auto col = sm->currentIndex().column();
    QString colTitle;

    if (auto headerItem = itemModel_->horizontalHeaderItem(col))
        colTitle = headerItem->text();
    else
        return;

    QMenu menu;

    auto a = menu.addAction(QIcon(":/hist1d.png"), QSL("Histogram data in column '%1'").arg(colTitle));
    connect(a, &QAction::triggered, q, [this, col] { showColumnHistogram(col); });

    menu.exec(tableView_->mapToGlobal(pos));
}

void HistoStatsWidget::Private::showColumnHistogram(const int col)
{
    if (col < 0 || col >= itemModel_->columnCount())
        return;

    const auto rowCount = itemModel_->rowCount();
    auto histo = std::make_shared<Histo1D>(rowCount, 0, rowCount-1);

    for (auto row=0; row<rowCount; ++row)
    {
        double value = itemModel_->item(row, col)->data(Qt::DisplayRole).toDouble();
        histo->fill(row, value);
    }

    auto widget = new Histo1DWidget(histo);
    widget->setAttribute(Qt::WA_DeleteOnClose);
    widget->show();
}