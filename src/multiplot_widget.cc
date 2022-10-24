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
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStack>
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
    //QGridLayout *plotGrid_;
    PlotMatrix *plotMatrix_ = {};
    QStatusBar *statusBar_;
    //QWidget *scrollWidget_;
    std::vector<std::shared_ptr<PlotEntry>> entries_;
    int maxColumns_ = TilePlot::DefaultMaxColumns;
    State state_ = State::Panning;
    QPoint panRef_;
    GridScaleDrawMode scaleDrawMode_ = GridScaleDrawMode::ShowAll;
    bool inZoomHandling_ = false;

    #if 0
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
                        if (inZoomHandling_)
                            return;

                        inZoomHandling_ = true;

                        if (scaleDrawMode_ == GridScaleDrawMode::HideInner)
                        {
                            for (auto &e2: entries_)
                            {
                                if (e2 != e)
                                {
                                    e2->zoomer()->setZoomStack(e->zoomer()->zoomStack(), e->zoomer()->zoomRectIndex());
                                }
                            }
                        }
                        refresh();
                        inZoomHandling_ = false;
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

        delete plotGrid_;
        plotGrid_ = new QGridLayout;
        scrollWidget_->setLayout(plotGrid_);

        for (auto &e: entries_)
        {
            addEntryToLayout(e);
        }

        set_plot_axes(plotGrid_, scaleDrawMode_);

        for (auto &e: entries_)
        {
            e->refresh();
            qDebug() << "relayout(): clearing zoomstack for" << e.get();
            e->zoomer()->setZoomStack({});
            e->plot()->replot();
        }
    }
    #else
    void addSink(const SinkPtr &s)
    {
        if (auto sink = std::dynamic_pointer_cast<Histo1DSink>(s))
        {
            for (const auto &histo : sink->getHistos())
            {
                auto e = std::make_shared<Histo1DSinkPlotEntry>(sink, histo, q);

                #if 0
                auto on_zoomer_zoomed = [this, e](const QRectF &zoomRect)
                {
                    if (inZoomHandling_)
                        return;

                    inZoomHandling_ = true;

                    if (scaleDrawMode_ == GridScaleDrawMode::HideInner)
                    {
                        for (auto &e2 : entries_)
                        {
                            if (e2 != e)
                            {
                                e2->zoomer()->setZoomStack(e->zoomer()->zoomStack(), e->zoomer()->zoomRectIndex());
                            }
                        }
                    }
                    refresh();
                    inZoomHandling_ = false;
                };

                QObject::connect(e->zoomer(), &ScrollZoomer::zoomed, q, on_zoomer_zoomed);
                #endif

                entries_.emplace_back(std::move(e));
            }
        }

        relayout();
    }

    void relayout()
    {
        if (plotMatrix_)
        {
            // Empty the layout
            while (plotMatrix_->plotGrid()->count())
                delete plotMatrix_->plotGrid()->takeAt(0);
            assert(plotMatrix_->plotGrid()->count() == 0);

            // Orphan the plot instances so ~PlotMatrix() does not delete them.
            for (auto &e: entries_)
                e->plot()->setParent(nullptr);

            delete plotMatrix_;
        }

        plotMatrix_ = new PlotMatrix(entries_, maxColumns_);
        scrollArea_->setWidget(plotMatrix_);
    }
#endif

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
        //qDebug() << ">>> refresh";
        for (auto &e: entries_)
        {
            //e->refresh();
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

            //if (auto sz = e->zoomer()->zoomStack().size())
            //    qDebug() << "zoomstack depth for entry" << e.get() << sz;
        }
        //qDebug() << "<<< refresh";
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

    void setScaleDrawMode(GridScaleDrawMode mode)
    {
        scaleDrawMode_ = mode;
        //relayout();
    }
};

MultiPlotWidget::MultiPlotWidget(AnalysisServiceProvider *asp, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->asp_ = asp;
    d->toolBar_ = make_toolbar();
    //d->plotGrid_ = new QGridLayout;
    d->statusBar_ = make_statusbar();
    //d->scrollWidget_ = new QWidget;

    auto toolBarFrame = new QFrame;
    toolBarFrame->setFrameStyle(QFrame::StyledPanel);
    {
        auto l = make_hbox<0, 0>(toolBarFrame);
        l->addWidget(d->toolBar_);
    }

    //d->scrollWidget_->setLayout(d->plotGrid_);

    d->scrollArea_ = new QScrollArea;
    auto scrollArea = d->scrollArea_;
    //scrollArea->setWidget(d->scrollWidget_);
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

    // x and y axis scale draw visibility
    {
        auto combo = new QComboBox;
        combo->addItem(QSL("Individual"), static_cast<int>(GridScaleDrawMode::ShowAll));
        combo->addItem(QSL("Combined"), static_cast<int>(GridScaleDrawMode::HideInner));

        tb->addWidget(make_vbox_container(QSL("Axis Mode"), combo, 2, -2)
                      .container.release());

        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this, combo]
                {
                    auto mode = static_cast<GridScaleDrawMode>(combo->currentData().toInt());
                    d->setScaleDrawMode(mode);
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
    //d->addSinks({ sink });
    d->addSink(sink);
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
                //qDebug() << sink.get();
                //sinks.emplace_back(std::move(sink));
                //d->addSinks(std::move(sinks));
                d->addSink(sink);
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
        ev->accept();
    }
    else
        QWidget::mousePressEvent(ev);
}

void MultiPlotWidget::mouseReleaseEvent(QMouseEvent *ev)
{
    if (d->state_ == Private::State::PanningActive)
    {
        d->state_ = Private::State::Panning;
        d->scrollArea_->viewport()->setCursor(Qt::OpenHandCursor);
        ev->accept();
    }
    else
        QWidget::mouseReleaseEvent(ev);
}

bool MultiPlotWidget::eventFilter(QObject *watched, QEvent *ev)
{
    if (watched == d->scrollArea_->viewport()
        && ev->type() == QEvent::Wheel
        && (dynamic_cast<QWheelEvent *>(ev)->modifiers() & Qt::ControlModifier)
        )
    {
        // Do not pass the event to the scrollareas viewport which would scroll
        // the vertical scrollbar.
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
    else
        QWidget::wheelEvent(ev);
}

void MultiPlotWidget::mouseDoubleClickEvent(QMouseEvent *ev)
{
    // TODO: open plot at position in its own window
    QWidget::mouseDoubleClickEvent(ev);
}