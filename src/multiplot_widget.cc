#include "multiplot_widget.h"
#include "multiplot_widget_p.h"

#include <atomic>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDrag>
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

static const QString PlotTileMimeType = QSL("mvme/x-multiplot-tile");

struct MultiPlotWidget::Private
{
    static const int ReplotPeriod_ms = 1000;
    static const int CanStartDragDistancePixels = 6;

    enum class State
    {
        Default,
        Panning,
        PanningActive,
        Zooming,
        Rearrange,
        RearrangeActive,
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
    QPoint dragRef_; // where the mouse was pressed to potentially start a drag operation
    std::atomic<bool> inRefresh_ = false;
    bool inZoomHandling_ = false;
    QCheckBox *cb_combinedZoom_;
    QLabel *rrLabel_ = {};
    QComboBox *combo_maxRes_ = {};
    u32 maxVisibleBins_ = 1u << 16;

    void addSink(const SinkPtr &s)
    {
        if (auto sink = std::dynamic_pointer_cast<Histo1DSink>(s))
        {
            for (const auto &histo : sink->getHistos())
            {
                auto e = std::make_shared<Histo1DSinkPlotEntry>(sink, histo, q);
                e->plot()->canvas()->installEventFilter(q);

                QObject::connect(e->zoomer(), &ScrollZoomer::zoomed,
                                 q, [this, e] (const QRectF &) { onEntryZoomed(e); });

                entries_.emplace_back(std::move(e));
            }
        }
        else if (auto sink = std::dynamic_pointer_cast<Histo2DSink>(s))
        {
            auto e = std::make_shared<Histo2DSinkPlotEntry>(sink, q);
            e->plot()->canvas()->installEventFilter(q);

            QObject::connect(e->zoomer(), &ScrollZoomer::zoomed,
                                q, [this, e] (const QRectF &) { onEntryZoomed(e); });
            entries_.emplace_back(std::move(e));
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
        //plotMatrix_->installEventFilter(q);
        //plotMatrix_->setMouseTracking(true);
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
            const auto maxVisBins = maxVisibleBins_;

            for (Qt::Axis axis: { Qt::XAxis, Qt::YAxis })
            {
                if (auto bins = e->binCount(axis); bins > maxVisBins)
                    e->setRRF(axis, bins / maxVisBins);
                else
                    e->setRRF(axis, 0);
                //qDebug("e=%s, axis=%d, rrf=%lu", e->analysisObject()->objectName().toLatin1().data(), axis, e->rrf(axis));
            }

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
                windowTitle = "PlotGrid " + h1dEntry->sink->objectName();
        }
        q->setWindowTitle(windowTitle);

        inRefresh_ = false;
    }

    void transitionState(const State &newState)
    {
        switch (newState)
        {
            case State::Default:
            case State::Rearrange:
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

    int indexOf(const std::shared_ptr<PlotEntry> &e) const
    {
        if (auto it = std::find(std::begin(entries_), std::end(entries_), e);
            it != std::end(entries_))
        {
            return it - std::begin(entries_);
        }

        return -1;
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
    scrollArea->viewport()->setMouseTracking(true);

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

    // combo_maxRes
    {
        d->combo_maxRes_ = new QComboBox;
        auto boxStruct = make_vbox_container(QSL("Max Visible Resolution"), d->combo_maxRes_, 0, -2);
        set_widget_font_pointsize(boxStruct.label, 7);
        tb->addWidget(boxStruct.container.release());

        for (u32 bits=4; bits<=16; ++bits)
        {
            u32 value = 1u << bits;
            auto text = QSL("%1 (%2 bit)").arg(value).arg(bits);
            d->combo_maxRes_->addItem(text, value);
        }

        connect(d->combo_maxRes_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this]
                {
                    d->maxVisibleBins_ = d->combo_maxRes_->currentData().toUInt();
                    d->refresh();
                });

        d->combo_maxRes_->setCurrentIndex(6);
    }

    tb->addSeparator();

    auto actionPan = tb->addAction(QIcon(":/hand.png"), "Pan");
    actionPan->setCheckable(true);

    auto actionRearrange = tb->addAction(QIcon(":/arrow-out.png"), "Rearrange");
    actionRearrange->setCheckable(true);

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
    //plotInteractions->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    plotInteractions->addAction(actionPan);
    plotInteractions->addAction(actionZoom);
    plotInteractions->addAction(actionRearrange);

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
            this, [=] (QAction */*action*/)
            {
                if (actionPan->isChecked())
                    d->transitionState(Private::State::Panning);
                else if (actionZoom->isChecked())
                    d->transitionState(Private::State::Zooming);
                else if (actionRearrange->isChecked())
                    d->transitionState(Private::State::Rearrange);
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
    else if (ev->mimeData()->hasFormat(PlotTileMimeType))
    {
        qDebug() << __PRETTY_FUNCTION__ << "a mime a day is a meme a week";
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
    else if (ev->source() == this && ev->mimeData()->hasFormat(PlotTileMimeType))
    {
        auto sourceIndex = QVariant(ev->mimeData()->data(PlotTileMimeType)).toInt();
        auto destIndex = d->indexOf(d->entryAt(ev->pos()));
        qDebug() << __PRETTY_FUNCTION__
                 << "sourceIndex =" << sourceIndex
                 << "destIndex =" << destIndex;
    }
    else
        QWidget::dropEvent(ev);
}

void MultiPlotWidget::mouseMoveEvent(QMouseEvent *ev)
{
    qDebug() << __PRETTY_FUNCTION__ << ev;

    if (d->state_ == Private::State::PanningActive)
    {
        auto move_scrollbar = [] (QScrollBar *sb, int delta)
        {
            auto pos = sb->sliderPosition();
            pos += delta * 2.0;
            sb->setValue(pos);
        };

        auto delta = ev->pos() - d->panRef_;
        move_scrollbar(d->scrollArea_->horizontalScrollBar(), -delta.x());
        move_scrollbar(d->scrollArea_->verticalScrollBar(), -delta.y());
        d->panRef_ = ev->pos();
    }
    else if (d->state_ == Private::State::Rearrange)
    {
        auto entry = d->entryAt(ev->pos());

        if (entry && (ev->buttons() & Qt::LeftButton))
        {
            if (auto delta = (ev->pos() - d->dragRef_).manhattanLength();
                delta >= Private::CanStartDragDistancePixels)
            {
                QVariant dragData(d->indexOf(entry));
                auto mimeData = new QMimeData;
                mimeData->setData(PlotTileMimeType, dragData.toByteArray());
                auto drag = new QDrag(this);
                drag->setMimeData(mimeData);
                qDebug() << __PRETTY_FUNCTION__ << "pre drag exec";
                drag->exec(Qt::MoveAction);
                qDebug() << __PRETTY_FUNCTION__ << "post drag exec";
            }
        }
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
    else if (d->state_ == Private::State::Rearrange)
    {
        d->dragRef_ = ev->pos();
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
    bool result = false;

    if (watched == d->scrollArea_->viewport()
        && ev->type() == QEvent::Wheel
        && (dynamic_cast<QWheelEvent *>(ev)->modifiers() & Qt::ControlModifier)
        )
    {
        // Do not pass the event to the scrollareas viewport which would scroll
        // the vertical scrollbar. See wheelEvent() for the code dealing with
        // wheel events.
        result = true;
    }
    else if ((watched == d->scrollArea_->viewport() || watched == d->scrollArea_->widget())
        && ev->type() == QEvent::MouseMove
        && d->state_ == Private::State::Rearrange)
    {
        // Similar to the wheel event above: when rearranging we want the mouse
        // events from the scrollareas viewport or its widget for ourselves.
        result = true;
    }

    //qDebug() << __PRETTY_FUNCTION__ << watched << ev << result;

    return result;
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

struct OpenSinkVisitor: public PlotEntryVisitor
{
    explicit OpenSinkVisitor(AnalysisServiceProvider *asp)
       : asp_(asp)
    { }

    void visit(Histo1DSinkPlotEntry *e)
    {
        Histo1DWidgetInfo info{};
        info.sink = e->sink;
        info.histos = e->sink->getHistos();
        info.histoAddress = e->histoIndex;
        show_sink_widget(asp_, info);
    }

    void visit(Histo2DSinkPlotEntry *e)
    {
        show_sink_widget(asp_, e->sink);
    }

    AnalysisServiceProvider *asp_;
};

void MultiPlotWidget::mouseDoubleClickEvent(QMouseEvent *ev)
{
    OpenSinkVisitor osv(d->asp_);

    if ((d->state_ == Private::State::Default
        || d->state_ == Private::State::Panning
        || d->state_ == Private::State::Zooming)
        && ev->button() == Qt::LeftButton)
    {
        // Open the clicked plot if any.
        if (auto e = d->entryAt(ev->pos()))
        {
            e->accept(osv);
            ev->accept();
        }
    }
    else
        QWidget::mouseDoubleClickEvent(ev);
}