#include "histo_stats_widget.h"
#include "histo_stats_widget_p.h"

#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QStatusBar>
#include <QTableView>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <vector>

#include "histo1d_widget.h"
#include "qt_util.h"
#include "util/cpp17_util.h"
#include "util/qt_monospace_textedit.h"

HistoStatsTableModel::~HistoStatsTableModel()
{ }

struct HistoStatsWidget::Private
{
    static const int AdditionalTableRows = 4; // rms, min, max mean for each of the columns

    // Indexes for the bottom rows containing aggrregate values. The index value
    // is subtracted from model->rowCount() to calculate the final row index.
    enum SpecialRowIndexes
    {
        Rms = 4,
        Min = 3,
        Max = 2,
        Mean = 1,
    };

    struct AggregateStats
    {
        double rms;
        double min;
        double max;
        double mean;
    };

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
    void copyFromTableToClipboard();
    // Number of rows containing histo statistics values. The additional rows
    // contain aggregate values.
    int statsRowCount() const { return itemModel_->rowCount() - AdditionalTableRows; }
    AggregateStats calculateAggregateStats(int column) const;
    QString columnTitle(int col) const;
    QTextStream &makeStatsText(QTextStream &out) const;
    void handleActionExport();
    void handleActionPrint();
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

    auto actionExport = toolBar->addAction(QIcon(":/document-save.png"), "Export");
    auto actionPrint = toolBar->addAction(QIcon(":/printer.png"), "Print");
    toolBar->addWidget(resolutionComboContainer.container.release());

    connect(d->combo_resolution_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]
            {
                d->selectedResolution_ = d->combo_resolution_->currentData().toInt();
                d->refresh();
            });

    connect(d->tableView_, &QWidget::customContextMenuRequested,
            this, [this] (const QPoint &pos) { d->handleTableContextMenu(pos); });

    connect(actionExport, &QAction::triggered,
            this, [this] { d->handleActionExport(); });

    connect(actionPrint, &QAction::triggered,
            this, [this] { d->handleActionPrint(); });

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

void HistoStatsWidget::addHistograms(const Histo1DList &histos)
{
    for (const auto &histo: histos)
    {
        HistoEntry e{histo};
        d->entries_.emplace_back(e);
    }
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

    rowCount += Private::AdditionalTableRows;

    QStringList hLabels{ "Histo#", "EntryCount", "Mean", "RMS", "Gauss Mean", "FWHM" };
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

    // Bold font for the additional bottom rows with aggregate statistics.
    auto boldFont = tableView_->font();
    boldFont.setBold(true);

    for (int row=model->rowCount()-AdditionalTableRows; row<model->rowCount(); ++row)
    {
        for (int col=0; col<model->columnCount(); ++col)
            model->item(row, col)->setData(boldFont, Qt::FontRole);
    }

    // Aggregate statistics in the additional bottom rows
    model->item(model->rowCount() - SpecialRowIndexes::Rms, 0)->setData("rms", Qt::DisplayRole);
    model->item(model->rowCount() - SpecialRowIndexes::Min, 0)->setData("min", Qt::DisplayRole);
    model->item(model->rowCount() - SpecialRowIndexes::Max, 0)->setData("max", Qt::DisplayRole);
    model->item(model->rowCount() - SpecialRowIndexes::Mean, 0)->setData("mean", Qt::DisplayRole);

    auto sm = tableView_->selectionModel();
    tableView_->setModel(model.get());
    sm->deleteLater();
    std::swap(itemModel_, model);

    auto currentRes = combo_resolution_->currentData().toInt();
    combo_resolution_->clear();

    s32 maxBins = std::accumulate(
        std::begin(entries_), std::end(entries_), 0,
        [] (s32 accu, const auto &entry)
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
    if (!itemModel_)
        return;

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

    assert(static_cast<ssize_t>(allStats.size()) == statsRowCount());

    // Update the info label (the groupbox above the table).
    QStringList infoLines =
    {
        QSL("Number of histograms: %1").arg(allStats.size()),

        QSL("Effective Resolution: %1 (%2 bit)")
            .arg(combo_resolution_->currentData().toInt())
            .arg(std::ceil(std::log2(combo_resolution_->currentData().toInt()))),

        QSL("X-Axis Interval: [%1, %2)").arg(xScaleDiv_.lowerBound()).arg(xScaleDiv_.upperBound()),
    };

    label_info_->setText(infoLines.join("\n"));

    int row = 0;

    for (const auto &stats: allStats)
    {
        int col = 0;

        itemModel_->item(row, col++)->setData(row, Qt::DisplayRole);

        if (stats.entryCount)
        {
            itemModel_->item(row, col++)->setData(stats.entryCount, Qt::DisplayRole);
            itemModel_->item(row, col++)->setData(stats.mean, Qt::DisplayRole);
            itemModel_->item(row, col++)->setData(stats.sigma, Qt::DisplayRole);
            itemModel_->item(row, col++)->setData(stats.fwhmCenter, Qt::DisplayRole);
            itemModel_->item(row, col++)->setData(stats.fwhm, Qt::DisplayRole);
        }
        else while (col < itemModel_->columnCount())
                itemModel_->item(row, col++)->setData({}, Qt::DisplayRole);
        ++row;
    }

    // Aggregate statistics in the additional bottom rows
    for (int col = 1; col < itemModel_->columnCount(); ++col)
    {
        auto aggs = calculateAggregateStats(col);
        itemModel_->item(itemModel_->rowCount() - SpecialRowIndexes::Rms, col)->setData(aggs.rms, Qt::DisplayRole);
        itemModel_->item(itemModel_->rowCount() - SpecialRowIndexes::Min, col)->setData(aggs.min, Qt::DisplayRole);
        itemModel_->item(itemModel_->rowCount() - SpecialRowIndexes::Max, col)->setData(aggs.max, Qt::DisplayRole);
        itemModel_->item(itemModel_->rowCount() - SpecialRowIndexes::Mean, col)->setData(aggs.mean, Qt::DisplayRole);
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
    auto colTitle = columnTitle(col);

    QMenu menu;
    QAction *a = nullptr;

    a = menu.addAction(QIcon::fromTheme("edit-copy"), QSL("&Copy selected values"));
    connect(a, &QAction::triggered, q, [this] { copyFromTableToClipboard(); });

    a = menu.addAction(QIcon(":/hist1d.png"), QSL("&Histogram data in column '%1'").arg(colTitle));
    connect(a, &QAction::triggered, q, [this, col] { showColumnHistogram(col); });

    menu.exec(tableView_->mapToGlobal(pos));
}

void HistoStatsWidget::Private::showColumnHistogram(const int col)
{
    if (col < 0 || col >= itemModel_->columnCount())
        return;

    const auto rowCount = statsRowCount();
    auto histo = std::make_shared<Histo1D>(rowCount, 0, rowCount);
    histo->setAxisInfo(Qt::XAxis, { "Histo #", "" });
    histo->setTitle(columnTitle(col));

    for (auto row=0; row<rowCount; ++row)
    {
        double value = itemModel_->item(row, col)->data(Qt::DisplayRole).toDouble();
        histo->fill(row, value);
    }

    auto widget = new Histo1DWidget(histo);
    widget->setAttribute(Qt::WA_DeleteOnClose);
    widget->setWindowTitle(QSL("'%1' histogram").arg(columnTitle(col)));
    add_widget_close_action(widget);
    widget->show();
}

void HistoStatsWidget::Private::copyFromTableToClipboard()
{
    auto indexes = tableView_->selectionModel()->selectedIndexes();

    std::sort(std::begin(indexes), std::end(indexes), [] (const QModelIndex &a, const QModelIndex &b)
    {
        if (a.row() == b.row())
            return a.column() < b.column();

        return a.row() < b.row();
    });

    QString text;
    int prevRow = -1;

    for (const auto &idx: indexes)
    {
        if (auto data = itemModel_->item(idx.row(), idx.column())->data(Qt::DisplayRole);
            !data.isNull())
        {
            if (!text.isEmpty())
            {
                if (prevRow >= 0 && prevRow != idx.row())
                    text += "\n";
                else
                    text += "\t";
            }

            auto value = data.toString();
            text += value;
            prevRow = idx.row();
        }
    }

    QGuiApplication::clipboard()->setText(text);
}

HistoStatsWidget::Private::AggregateStats HistoStatsWidget::Private::calculateAggregateStats(int col) const
{
    std::vector<double> values;
    values.reserve(statsRowCount());

    for (int row = 0; row < statsRowCount(); ++row)
    {
        if (auto data = itemModel_->item(row, col)->data(Qt::DisplayRole);
            !data.isNull())
        {
            auto value = data.toDouble();
            values.emplace_back(value);
        }
    }

    AggregateStats result{};

    result.min = std::numeric_limits<double>::max();
    result.max = std::numeric_limits<double>::lowest();

    for (const auto &value: values)
    {
        result.min = std::min(result.min, value);
        result.max = std::max(result.max, value);
        result.mean += value;
    }

    if (!values.empty())
    {
        result.mean /= values.size();

        for (auto value: values)
        {
            value -= result.mean;
            result.rms += value * value;
        }

        result.rms = std::sqrt(result.rms / values.size());
    }

    return result;
}

QString HistoStatsWidget::Private::columnTitle(int col) const
{
    if (auto headerItem = itemModel_->horizontalHeaderItem(col))
        return headerItem->text();
    return {};
}

namespace
{

// Note: assumes a table model structure. Does not work with tree hierarchies.
// Also type conversion is only specialized for doubles, all other cell data is
// converted to string.
QTextStream &table_model_to_text(QTextStream &out, const QStandardItemModel &model, const int fieldWidth = 14)
{
    const int colCount = model.columnCount();
    const int rowCount = model.rowCount();

    out.reset();
    out.setFieldAlignment(QTextStream::AlignLeft);

    out << qSetFieldWidth(fieldWidth);

    for (int col=0; col<colCount; ++col)
        out << model.horizontalHeaderItem(col)->text();

    out << qSetFieldWidth(0) << Qt::endl << qSetFieldWidth(fieldWidth);

    for (int row=0; row<rowCount; ++row)
    {
        for (int col=0; col<colCount; ++col)
        {
            auto data = model.item(row, col)->data(Qt::DisplayRole);
            if (auto d = data; d.convert(QMetaType::Double))
                out << d.toDouble();
            else
                out << data.toString();
        }
        out << qSetFieldWidth(0) << Qt::endl << qSetFieldWidth(fieldWidth);
    }

    return out;
}
}

QTextStream &HistoStatsWidget::Private::makeStatsText(QTextStream &out) const
{
    // Header with info label contents
    out << "# " << q->windowTitle() << Qt::endl;
    for (auto str: label_info_->text().split("\n"))
        out << "# " << str << Qt::endl;
    out << Qt::endl;

    // Table contents
    return table_model_to_text(out, *itemModel_);
}

void HistoStatsWidget::Private::handleActionExport()
{
    auto startdir = QSettings().value("LastHistoStatsSaveDirectory").toString();

    QFileDialog fd(q, "Save histo stats to file", startdir, "*.txt");
    fd.setDefaultSuffix(".txt");
    fd.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);

    if (fd.exec() != QDialog::Accepted || fd.selectedFiles().isEmpty())
        return;

    auto filename = fd.selectedFiles().front();

    if (filename.isEmpty())
        return;

    QFile outfile(filename);

    if (!outfile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(
            q, QSL("File save error"),
            QSL("Error opening %1 for writing: %2")
                .arg(filename)
                .arg(outfile.errorString()));
        return;
    }

    QString buffer;
    QTextStream out(&buffer, QIODevice::Text);
    makeStatsText(out);
    outfile.write(buffer.toUtf8());
    QSettings().setValue("LastHistoStatsSaveDirectory", QFileInfo(filename).absolutePath());
}

void HistoStatsWidget::Private::handleActionPrint()
{
    qDebug()<<"List of printers";
    QList<QPrinterInfo> printerList=QPrinterInfo::availablePrinters();
    foreach (QPrinterInfo printerInfo, printerList) {
        qDebug()<<printerInfo.printerName();
    }

    QPrinter printer;
    QPrintDialog printDialog(&printer);

    if (printDialog.exec() == QDialog::Accepted)
    {
        QString buffer;
        QTextStream out(&buffer);
        makeStatsText(out);
        QTextDocument doc;
        doc.setDefaultFont(make_monospace_font());
        doc.setPlainText(buffer);
        doc.print(&printer);
    }
}
