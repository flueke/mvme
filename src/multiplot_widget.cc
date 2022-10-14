#include "multiplot_widget.h"
#include "multiplot_widget_p.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>

#include "analysis/analysis_ui_util.h"
#include "histo_ui.h"
#include "mvme_qwt.h"
#include "qt_util.h"

using namespace analysis;
using namespace analysis::ui;
using namespace histo_ui;
using namespace mvme_qwt;

struct MultiPlotWidget::Private
{
    static const int DefaultMaxColumns = 4;
    static const int TileMinWidth = 200;
    static const int TileMinHeight = 200;
    static const int TileDeltaWidth = 50;
    static const int TileDeltaHeight = 50;
    static const int ReplotPeriod_ms = 1000;

    struct PlotEntry
    {
        SinkPtr sink;
        TilePlot *plot;
        QwtPlotItem *plotItem;
        Histo1DIntervalData *histoData;
        QwtPlotCurve *gaussCurve;
        Histo1DGaussCurveData *gaussCurveData;
    };

    AnalysisServiceProvider *asp_;
    QToolBar *toolBar_;
    QGridLayout *plotGrid_;
    QStatusBar *statusBar_;
    std::vector<PlotEntry> entries_;
    int maxColumns_ = DefaultMaxColumns;

    void addSinks(std::vector<SinkPtr> &&sinks)
    {
        for (auto &s: sinks)
        {
            if (auto h1dSink = std::dynamic_pointer_cast<Histo1DSink>(s))
            {
                for (const auto &h1d: h1dSink->getHistos())
                {
                    auto histoData = new Histo1DIntervalData(h1d.get());
                    auto histoItem = new QwtPlotHistogram;
                    histoItem->setStyle(QwtPlotHistogram::Outline);
                    histoItem->setData(histoData); // ownership of histoData goes to qwt

                    PlotEntry e{};
                    e.sink = std::move(s);
                    e.plot = new TilePlot;
                    e.plotItem = histoItem;
                    e.histoData = histoData;
                    e.gaussCurve = make_plot_curve(Qt::green);
                    e.gaussCurveData = new Histo1DGaussCurveData;
                    e.gaussCurve->setData(e.gaussCurveData);
                    e.gaussCurve->hide();

                    //e.plot->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
                    e.plot->setMinimumSize(TileMinWidth, TileMinHeight);

                    e.plotItem->attach(e.plot);
                    e.gaussCurve->attach(e.plot);
                    addEntryToLayout(e);
                    entries_.emplace_back(std::move(e));
                }
            }
        }
    }

    void addEntryToLayout(PlotEntry &e)
    {
        int index = plotGrid_->count();
        int row = std::floor(index / maxColumns_);
        int col = index % maxColumns_;
        e.plot->resize(100, 100);
        plotGrid_->addWidget(e.plot, row, col);
    }

    void relayout()
    {
        while (plotGrid_->count())
            delete plotGrid_->takeAt(0);

        for (auto &e: entries_)
            addEntryToLayout(e);
    }

    void addToTileSize(int dx, int dy)
    {
        addToTileSize({dx, dy});
    }

    void addToTileSize(const QSize &delta)
    {
        for (auto &e: entries_)
        {
            auto tileSize = e.plot->minimumSize();
            tileSize += delta;
            if (tileSize.width() < TileMinWidth)
                tileSize.setWidth(TileMinWidth);
            if (tileSize.height() < TileMinHeight)
                tileSize.setHeight(TileMinHeight);
            e.plot->setMinimumSize(tileSize);
        }
    }

    void refresh()
    {
        for (auto &e: entries_)
        {
            auto histoStats = e.histoData->getHisto()->calcStatistics();
            e.gaussCurveData->setStats(histoStats);
            e.plot->replot();
        }
    }
};

MultiPlotWidget::MultiPlotWidget(AnalysisServiceProvider *asp, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->asp_ = asp;
    d->toolBar_ = make_toolbar();
    d->plotGrid_ = new QGridLayout;
    d->statusBar_ = make_statusbar();

    auto scrollWidget = new QWidget;
    scrollWidget->setLayout(d->plotGrid_);

    auto scrollArea = new QScrollArea;
    scrollArea->setWidget(scrollWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    auto layout = make_vbox<2, 2>(this);
    layout->addWidget(d->toolBar_);
    layout->addWidget(scrollArea);
    layout->addWidget(d->statusBar_);

    setAcceptDrops(true);
    setMouseTracking(true);

    auto &tb = d->toolBar_;
    auto actionEnlargeTiles = tb->addAction("++tilesize");
    auto actionShrinkTiles = tb->addAction("--tilesize");
    auto spinColumns = new QSpinBox;

    {
        spinColumns->setMinimum(1);
        spinColumns->setValue(d->maxColumns_);
        auto w = new QWidget;
        auto l = make_vbox<2, 2>(w);
        l->addWidget(new QLabel("Columns"));
        l->addWidget(spinColumns);
        tb->addWidget(w);
    }

    auto actionGauss = tb->addAction(QIcon(":/generic_chart_with_pencil.png"), QSL("Gauss"));
    actionGauss->setCheckable(true);
    actionGauss->setChecked(false);

    connect(actionEnlargeTiles, &QAction::triggered,
            this, [this] { d->addToTileSize(Private::TileDeltaWidth, Private::TileDeltaHeight); });

    connect(actionShrinkTiles, &QAction::triggered,
            this, [this] { d->addToTileSize(-Private::TileDeltaWidth, -Private::TileDeltaHeight); });

    connect(spinColumns, qOverload<int>(&QSpinBox::valueChanged),
            this, [this] (int value)
            {
                d->maxColumns_ = value;
                d->relayout();
            });

    connect(actionGauss, &QAction::toggled,
            this, [this] (bool checked)
            {
                for (auto &e: d->entries_)
                    e.gaussCurve->setVisible(checked);
                d->refresh();
            });

    auto replotTimer = new QTimer(this);
    connect(replotTimer, &QTimer::timeout, this, [this] { d->refresh(); });
    replotTimer->start(Private::ReplotPeriod_ms);
}

MultiPlotWidget::~MultiPlotWidget()
{
    qDebug() << __PRETTY_FUNCTION__ << actions();
}

void MultiPlotWidget::addSink(const analysis::SinkPtr &sink)
{
    d->addSinks({ sink });
    d->refresh();
}

void MultiPlotWidget::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasFormat(SinkIdListMIMEType))
    {
        qDebug() << __PRETTY_FUNCTION__ << ev << ev->mimeData();
        ev->acceptProposedAction();
    }
    else
        QWidget::dragEnterEvent(ev);
}

void MultiPlotWidget::dragLeaveEvent(QDragLeaveEvent *ev)
{
    //qDebug() << __PRETTY_FUNCTION__ << ev;
    QWidget::dragLeaveEvent(ev);
}

void MultiPlotWidget::dragMoveEvent(QDragMoveEvent *ev)
{
    //qDebug() << __PRETTY_FUNCTION__ << ev;
    QWidget::dragMoveEvent(ev);
}

void MultiPlotWidget::dropEvent(QDropEvent *ev)
{
    auto analysis = d->asp_->getAnalysis();

    if (analysis && ev->mimeData()->hasFormat(SinkIdListMIMEType))
    {
        auto ids = decode_id_list(ev->mimeData()->data(SinkIdListMIMEType));
        qDebug() << __PRETTY_FUNCTION__ << ev << ids;
        std::vector<SinkPtr> sinks;
        for (const auto &id: ids)
        {
            if (auto sink = analysis->getObject<SinkInterface>(id))
            {
                qDebug() << sink.get();
                sinks.emplace_back(std::move(sink));
                d->addSinks(std::move(sinks));
            }
        }
        d->refresh();
    }
    else
        QWidget::dropEvent(ev);
}