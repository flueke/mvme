#include "multiplot_widget.h"
#include "multiplot_widget_p.h"

#include <atomic>
#include <QApplication>
#include <QCheckBox>
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
#include "histo_gui_util.h"
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
        Default,
        Panning,
        PanningActive,
        Zooming,
    };

    MultiPlotWidget *q = {};
    AnalysisServiceProvider *asp_ = {};
    QToolBar *toolBar_ = {};
    QScrollArea *scrollArea_ = {};
    PlotMatrix *plotMatrix_ = {};
    QStatusBar *statusBar_ = {};
    std::vector<std::shared_ptr<PlotEntry>> entries_;
    int maxColumns_ = TilePlot::DefaultMaxColumns;
    State state_ = State::Panning;
    QPoint panRef_; // where the current pan operation started in pixel coordinates
    std::atomic<bool> inRefresh_ = false;
    bool inZoomHandling_ = false;
    QCheckBox *cb_combinedZoom_;
    QLabel *rrLabel_ = {};
    QSlider *rrSlider_ = {};
    u32 rrf_ = Histo1D::NoRR;

    void addSink(const SinkPtr &s)
    {
        if (auto sink = std::dynamic_pointer_cast<Histo1DSink>(s))
        {
            for (const auto &histo : sink->getHistos())
            {
                auto e = std::make_shared<Histo1DSinkPlotEntry>(sink, histo, q);

                QObject::connect(e->zoomer(), &ScrollZoomer::zoomed,
                                 q, [this, e] (const QRectF &) { onEntryZoomed(e); });

                entries_.emplace_back(std::move(e));
            }
        }
        else if (auto sink = std::dynamic_pointer_cast<Histo2DSink>(s))
        {
            auto e = std::make_shared<Histo2DSinkPlotEntry>(sink, q);
            entries_.emplace_back(std::move(e));
        }

        u32 maxBins = 0;

        for (const auto &e: entries_)
        {
            if (auto h1dEntry = std::dynamic_pointer_cast<Histo1DSinkPlotEntry>(e))
                maxBins = std::max(maxBins, h1dEntry->histo->getNumberOfBins());
        }
        rrSlider_->setMaximum(std::log2(maxBins));
        if (rrf_ == Histo1D::NoRR)
            rrSlider_->setValue(rrSlider_->maximum());

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
        if (inRefresh_)
            return;

        inRefresh_ = true;

        for (auto &e: entries_)
        {
            e->setRRF(Qt::XAxis, rrf_);
            e->refresh();

            if (e->plot()->canvas())
            {
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

            e->plot()->replot();
        }

        QString windowTitle("PlotGrid");

        if (!entries_.empty())
        {
            if (auto h1dEntry = std::dynamic_pointer_cast<Histo1DSinkPlotEntry>(entries_[0]))
                windowTitle = "PlotGrid " + h1dEntry->histo->objectName();
        }
        q->setWindowTitle(windowTitle);

        inRefresh_ = false;
    }

    void transitionState(const State &newState)
    {
        switch (newState)
        {
            case State::Default:
                scrollArea_->viewport()->unsetCursor();
                break;
            case State::Panning:
                scrollArea_->viewport()->setCursor(Qt::OpenHandCursor);
                break;
            case State::PanningActive:
                scrollArea_->viewport()->setCursor(Qt::ClosedHandCursor);
                break;
            case State::Zooming:
                // Each zoomer will set the cross cursor on its plot.
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

    void onEntryZoomed(const std::shared_ptr<PlotEntry> &entry)
    {
        if (inZoomHandling_)
            return;

        inZoomHandling_ = true;

        if (cb_combinedZoom_->isChecked())
        {
            std::for_each(
                std::begin(entries_), std::end(entries_),
                [&] (auto &e)
                {
                    if (e != entry)
                    {
                        e->zoomer()->setZoomStack(entry->zoomer()->zoomStack(),
                                                  entry->zoomer()->zoomRectIndex());
                        e->refresh();
                        e->plot()->replot();
                    }
                });
        }

        entry->refresh();
        entry->plot()->replot();

        inZoomHandling_ = false;
    }

    void onRRSliderValueChanged(int sliderValue)
    {
        u32 maxBins = 1u << rrSlider_->maximum();
        u32 visBins = 1u << sliderValue;
        rrf_ = maxBins / visBins;

        rrLabel_->setText(QSL("X-Res: %1, %2 bit")
                              .arg(visBins)
                              .arg(std::log2(visBins)));
        qDebug("maxBins=%d, visBins=%d, rrf=%d", maxBins, visBins, rrf_);
        refresh();
    }

    TilePlot *plotAt(const QPoint &p)
    {
        for (auto w = q->childAt(p); w; w = w->parentWidget())
        {
            if (auto tp = qobject_cast<TilePlot *>(w))
                return tp;
        }

        return {};
    }

    std::shared_ptr<PlotEntry> findEntry(TilePlot *plot)
    {
        if (auto it = std::find_if(std::begin(entries_), std::end(entries_),
            [plot] (const auto &e) { return e->plot() == plot; });
            it != std::end(entries_))
        {
            return *it;
        }

        return {};
    }

    std::shared_ptr<PlotEntry> entryAt(const QPoint &p)
    {
        if (auto tp = plotAt(p))
            return findEntry(tp);
        return {};
    }
};

MultiPlotWidget::MultiPlotWidget(AnalysisServiceProvider *asp, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->asp_ = asp;
    d->toolBar_ = make_toolbar();
    d->statusBar_ = make_statusbar();

    auto toolBarFrame = new QFrame;
    toolBarFrame->setFrameStyle(QFrame::StyledPanel);
    {
        auto l = make_hbox<0, 0>(toolBarFrame);
        l->addWidget(d->toolBar_);
    }

    d->scrollArea_ = new QScrollArea;
    auto scrollArea = d->scrollArea_;
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

    auto actionPan = tb->addAction(QIcon(":/hand.png"), "Pan");
    actionPan->setCheckable(true);

    auto actionZoom = tb->addAction(QIcon(":/resources/magnifier-zoom.png"), "Zoom");
    actionZoom->setCheckable(true);

    {
        d->cb_combinedZoom_ = new QCheckBox;
        auto boxStruct = make_vbox_container(QSL("Zoom all"), d->cb_combinedZoom_);
        set_widget_font_pointsize(d->cb_combinedZoom_, 7);
        set_widget_font_pointsize(boxStruct.label, 7);
        tb->addWidget(boxStruct.container.release());
    }

    auto actionGauss = tb->addAction(QIcon(":/generic_chart_with_pencil.png"), QSL("Gauss"));
    actionGauss->setCheckable(true);
    actionGauss->setChecked(false);

    {
        d->rrSlider_ = make_res_reduction_slider();
        auto boxStruct = make_vbox_container(QSL("Visible X Resolution"), d->rrSlider_, 0, -2);
        set_widget_font_pointsize(boxStruct.label, 7);
        d->rrLabel_ = boxStruct.label;
        tb->addWidget(boxStruct.container.release());

        connect(d->rrSlider_, &QSlider::valueChanged,
                this, [this] (int sliderValue) { d->onRRSliderValueChanged(sliderValue); });

        for (auto w: std::initializer_list<QWidget *>{ d->rrSlider_, d->rrLabel_ })
        {
            // XXX: testing size policy settings
            QSizePolicy p(QSizePolicy::Fixed, QSizePolicy::Fixed);
            w->setSizePolicy(p);
        }
    }

    tb->addSeparator();

    auto actionEnlargeTiles = tb->addAction(QIcon(":/map.png"), "Larger Tiles");
    auto actionShrinkTiles = tb->addAction(QIcon(":/map-medium.png"), "Smaller Tiles");
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

    auto plotInteractions = new QActionGroup(this);
    plotInteractions->addAction(actionPan);
    plotInteractions->addAction(actionZoom);

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
            this, [this, actionPan, actionZoom] (QAction */*action*/)
            {
                if (actionPan->isChecked())
                    d->transitionState(Private::State::Panning);
                else if (actionZoom->isChecked())
                    d->transitionState(Private::State::Zooming);
                else
                    d->transitionState(Private::State::Default);
            });

    actionPan->setChecked(true);

    auto refreshTimer = new QTimer(this);

    connect(refreshTimer, &QTimer::timeout,
            this, [this] { d->refresh(); });
            //Qt::QueuedConnection);

    refreshTimer->start(Private::ReplotPeriod_ms);
}

MultiPlotWidget::~MultiPlotWidget()
{
    qDebug() << __PRETTY_FUNCTION__ << "<<< inRefresh=" << d->inRefresh_;
}

void MultiPlotWidget::addSink(const analysis::SinkPtr &sink)
{
    d->addSink(sink);
    d->refresh();
}

void MultiPlotWidget::dragEnterEvent(QDragEnterEvent *ev)
{
    #if 1
    if (ev->mimeData()->hasFormat(SinkIdListMIMEType))
    {
        qDebug() << __PRETTY_FUNCTION__ << ev << ev->mimeData();
        ev->acceptProposedAction();
    }
    else
    #endif
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
    #if 1
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
                d->addSink(sink);
            }
        }
        d->refresh();
    }
    else
        QWidget::dropEvent(ev);
    #endif
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
    if ((d->state_ == Private::State::Default
        || d->state_ == Private::State::Panning
        || d->state_ == Private::State::Zooming)
        && ev->button() == Qt::LeftButton)
    {
        // Open the clicked plot if any.
        if (auto e = d->entryAt(ev->pos()))
        {
            if (auto h1dEntry = std::dynamic_pointer_cast<Histo1DSinkPlotEntry>(e))
            {
                Histo1DWidgetInfo info{};
                info.sink = h1dEntry->sink;
                info.histos = h1dEntry->sink->getHistos();
                info.histoAddress = h1dEntry->histoIndex;
                show_sink_widget(d->asp_, info);
                ev->accept();
            }
        }
    }
    else
        QWidget::mouseDoubleClickEvent(ev);
}