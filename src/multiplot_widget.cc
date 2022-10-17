#include "multiplot_widget.h"
#include "multiplot_widget_p.h"

#include <QComboBox>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QWheelEvent>

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
    static const int ReplotPeriod_ms = 1000;

    enum class State
    {
        Panning,
        PanningActive,
        Zooming,
    };

    MultiPlotWidget *q;
    AnalysisServiceProvider *asp_;
    QToolBar *toolBar_;
    QScrollArea *scrollArea_;
    QGridLayout *plotGrid_;
    QStatusBar *statusBar_;
    std::vector<std::shared_ptr<PlotEntry>> entries_;
    int maxColumns_ = TilePlot::DefaultMaxColumns;
    State state_ = State::Panning;
    QPoint panRef_;

    void addSinks(std::vector<SinkPtr> &&sinks)
    {
        for (auto &s: sinks)
        {
            if (auto sink = std::dynamic_pointer_cast<Histo1DSink>(s))
            {
                for (const auto &histo: sink->getHistos())
                {
                    auto e = std::make_shared<Histo1DSinkPlotEntry>(sink, histo, q);
                    addEntryToLayout(e);

                    auto on_zoomer_zoomed = [this, e] (const QRectF &zoomRect)
                    {
                        refresh();
                    };

                    QObject::connect(e->zoomer(), &ScrollZoomer::zoomed, q, on_zoomer_zoomed);

                    entries_.emplace_back(std::move(e));
                }
            }
            #if 0
            else if (auto sink = std::dynamic_pointer_cast<Histo2DSink>(s))
            {
                auto e = std::make_shared<Histo2DSinkPlotEntry>(sink, q);
                addEntryToLayout(e);

                entries_.emplace_back(std::move(e));
            }
            #endif
        }
    }

    void addEntryToLayout(PlotEntry &e)
    {
        int index = plotGrid_->count();
        int row = std::floor(index / maxColumns_);
        int col = index % maxColumns_;
        e.plot()->resize(100, 100);
        plotGrid_->addWidget(e.plot(), row, col);
    }

    void addEntryToLayout(const std::shared_ptr<PlotEntry> &e)
    {
        addEntryToLayout(*e);
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
        std::for_each(std::begin(entries_), std::end(entries_),
                      [=](auto &e)
                      { e->plot()->addToTileSize(delta); });
    }

    void enlargeTiles()
    {
        addToTileSize(TilePlot::TileDeltaWidth, TilePlot::TileDeltaHeight);
    }

    void shrinkTiles()
    {
        addToTileSize(-TilePlot::TileDeltaWidth, -TilePlot::TileDeltaHeight);
    }

    void refresh()
    {
        for (auto &e: entries_)
        {
            e->refresh();
            e->plot()->replot();

            if (state_ == State::Zooming)
            {
                e->plot()->canvas()->setCursor(Qt::CrossCursor);
                e->zoomer()->setEnabled(true);
            }
            else
            {
                e->plot()->canvas()->unsetCursor();
                e->zoomer()->setEnabled(false);
            }
        }
    }

    void transitionState(const State &newState)
    {
        switch (newState)
        {
            case State::Panning:
                scrollArea_->viewport()->setCursor(Qt::OpenHandCursor);
                break;
            case State::PanningActive:
                scrollArea_->viewport()->setCursor(Qt::ClosedHandCursor);
                break;
            case State::Zooming:
                scrollArea_->viewport()->unsetCursor();
                break;
        }
        state_ = newState;
        refresh();
    }

    void setAxisScaling(AxisScaleType ast)
    {
        std::for_each(std::begin(entries_), std::end(entries_),
                      [=](auto &e)
                      { e->scaleChanger()->setScaleType(ast); });
        refresh();
    }
};

MultiPlotWidget::MultiPlotWidget(AnalysisServiceProvider *asp, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->asp_ = asp;
    d->toolBar_ = make_toolbar();
    d->plotGrid_ = new QGridLayout;
    d->statusBar_ = make_statusbar();

    auto toolBarFrame = new QFrame;
    toolBarFrame->setFrameStyle(QFrame::StyledPanel);
    {
        auto l = make_hbox<0, 0>(toolBarFrame);
        l->addWidget(d->toolBar_);
    }

    auto scrollWidget = new QWidget;
    scrollWidget->setLayout(d->plotGrid_);

    d->scrollArea_ = new QScrollArea;
    auto scrollArea = d->scrollArea_;
    scrollArea->setWidget(scrollWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->viewport()->installEventFilter(this);
    scrollArea->viewport()->setCursor(Qt::OpenHandCursor);

    auto layout = make_vbox<2, 2>(this);
    layout->addWidget(toolBarFrame);
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

    auto actionPan = tb->addAction("Pan");
    actionPan->setCheckable(true);
    actionPan->setChecked(true);

    auto actionZoom = tb->addAction("Zoom");
    actionZoom->setCheckable(true);

    auto plotInteractions = new QActionGroup(this);
    plotInteractions->addAction(actionPan);
    plotInteractions->addAction(actionZoom);

    // Y/Z-Scale linear/logarithmic toggle
    {
        auto combo = new QComboBox;
        combo->addItem(QSL("Lin"), static_cast<int>(AxisScaleType::Linear));
        combo->addItem(QSL("Log"), static_cast<int>(AxisScaleType::Logarithmic));

        tb->addWidget(make_vbox_container(QSL("Axis Scale"), combo, 2, -2)
                      .container.release());

        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this] (int idx)
                {
                    d->setAxisScaling(static_cast<AxisScaleType>(idx));
                });
    }

    connect(actionEnlargeTiles, &QAction::triggered,
            this, [this] { d->enlargeTiles(); });

    connect(actionShrinkTiles, &QAction::triggered,
            this, [this] { d->shrinkTiles(); });

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
                {
                    if (auto h1dEntry = std::dynamic_pointer_cast<Histo1DSinkPlotEntry>(e))
                        h1dEntry->gaussCurve->setVisible(checked);
                }
                d->refresh();
            });

    connect(plotInteractions, &QActionGroup::triggered,
            this, [this, actionPan, actionZoom] (QAction *action)
            {
                if (action == actionPan)
                    d->transitionState(Private::State::Panning);
                else if (action == actionZoom)
                    d->transitionState(Private::State::Zooming);
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

void MultiPlotWidget::mouseMoveEvent(QMouseEvent *ev)
{
    auto move_scrollbar = [] (QScrollBar *sb, int delta)
    {
        auto pos = sb->sliderPosition();
        pos += delta * 2.0;
        sb->setValue(pos);
    };

    if (d->state_ == Private::State::PanningActive)
    {
        auto delta = ev->pos() - d->panRef_;
        move_scrollbar(d->scrollArea_->horizontalScrollBar(), -delta.x());
        move_scrollbar(d->scrollArea_->verticalScrollBar(), -delta.y());
        d->panRef_ = ev->pos();
    }
}

void MultiPlotWidget::mousePressEvent(QMouseEvent *ev)
{
    if (d->state_ == Private::State::Panning)
    {
        d->state_ = Private::State::PanningActive;
        d->scrollArea_->viewport()->setCursor(Qt::ClosedHandCursor);
        d->panRef_ = ev->pos();
    }
}

void MultiPlotWidget::mouseReleaseEvent(QMouseEvent *ev)
{
    if (d->state_ == Private::State::PanningActive)
    {
        d->state_ = Private::State::Panning;
        d->scrollArea_->viewport()->setCursor(Qt::OpenHandCursor);
    }
}

bool MultiPlotWidget::eventFilter(QObject *watched, QEvent *ev)
{
    if (watched == d->scrollArea_->viewport()
        && ev->type() == QEvent::Wheel
        && (dynamic_cast<QWheelEvent *>(ev)->modifiers() & Qt::ControlModifier)
        )
    {
        return true;
    }

    return false;
}

void MultiPlotWidget::wheelEvent(QWheelEvent *ev)
{
    if (ev->modifiers() & Qt::ControlModifier)
    {
        if (ev->delta() > 0)
            d->enlargeTiles();
        else if (ev->delta() < 0)
            d->shrinkTiles();
        ev->accept();
    }
}