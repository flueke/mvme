#include "multiplot_widget.h"
#include "multiplot_widget_p.h"

#include <atomic>
#include <list>
#include <set>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStack>
#include <QTimer>
#include <QWheelEvent>
#include <QMenu>

#include "analysis/analysis_ui_util.h"
#include "histo_gui_util.h"
#include "histo_ui.h"
#include "mvme_qwt.h"
#include "qt_util.h"

using namespace analysis;
using namespace analysis::ui;
using namespace histo_ui;
using namespace mvme_qwt;

namespace
{

static const QString PlotTileMimeType = QSL("mvme/x-multiplot-tile");

// For each visited PlotEntry creates and fills an analysis::PlotGridView::Entry
// object before appending it to the 'entries' member.
// Used in saveView()
struct EntryConversionVisitor: public PlotEntryVisitor
{
    PlotGridView::Entry createBasicEntry(PlotEntry *pe)
    {
        PlotGridView::Entry ve;
        if (auto ao = pe->analysisObject())
            ve.sinkId = ao->getId();
        ve.customTitle = pe->title();
        ve.zoomRect = pe->zoomer()->zoomRect();
        return ve;
    }

    void visit(Histo1DPlotEntry *pe)
    {
        auto ve = createBasicEntry(pe);
        ve.elementIndex = -1;
        entries.emplace_back(std::move(ve));
    }

    void visit(Histo1DSinkPlotEntry *pe)
    {
        auto ve = createBasicEntry(pe);
        ve.elementIndex = pe->histoIndex;
        entries.emplace_back(std::move(ve));
    }

    void visit(Histo2DSinkPlotEntry *pe)
    {
        auto ve = createBasicEntry(pe);
        entries.emplace_back(std::move(ve));
    }

    std::vector<PlotGridView::Entry> entries;
};

} // end anon namespace

struct MultiPlotWidget::Private
{
    static const int DefaultReplotPeriod_ms = 1000;
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
    QCheckBox *cb_combinedZoom_ = {};
    QLabel *rrLabel_ = {};
    QComboBox *combo_maxRes_ = {};
    u32 maxVisibleBins_ = 1u << 16;
    std::shared_ptr<analysis::PlotGridView> analysisGridView_;
    QComboBox *combo_axisScaleType_ = {};
    QSpinBox *spin_columns_;
    QAction *actionGauss_ = {};
    QAction *actionSaveView_ = {};
    QTimer replotTimer_;

    void addEntry(std::shared_ptr<PlotEntry> &&e)
    {
        e->plot()->canvas()->installEventFilter(q);
        e->plot()->canvas()->setMouseTracking(true);

        QObject::connect(e->zoomer(), &ScrollZoomer::zoomed,
                         q, [this, e] (const QRectF &) { onEntryZoomed(e); });

        entries_.emplace_back(std::move(e));
    }

    void removeEntry(const std::shared_ptr<PlotEntry> &e)
    {
        if (auto idx = indexOf(e); 0 <= idx && idx < static_cast<ssize_t>(entries_.size()))
            entries_.erase(std::begin(entries_) + idx);
        relayout();
    }

    void addSink(const SinkPtr &s)
    {
        if (auto sink = std::dynamic_pointer_cast<Histo1DSink>(s))
        {
            for (int i=0; i<sink->getNumberOfHistos(); ++i)
                addSinkElement(sink, i);
        }
        else
            addSinkElement(s, -1);

        relayout();
    }

    // Note: does not call relayout as an optimization! It's called in a loop in
    // addSink().
    std::shared_ptr<PlotEntry> addSinkElement(const SinkPtr &s, int index)
    {
        if (auto sink = std::dynamic_pointer_cast<Histo1DSink>(s);
            sink && 0 <= index && index < sink->getNumberOfHistos())
        {
            auto histo = sink->getHisto(index);
            auto e = std::make_shared<Histo1DSinkPlotEntry>(sink, histo, q);
            addEntry(e);
            return e;
        }
        else if (auto sink = std::dynamic_pointer_cast<Histo2DSink>(s))
        {
            auto e = std::make_shared<Histo2DSinkPlotEntry>(sink, q);
            addEntry(e);
            return e;
        }
        return {};
    }

    std::shared_ptr<PlotEntry> addHisto1D(const Histo1DPtr &histo)
    {
        //qDebug() << __PRETTY_FUNCTION__ << histo->objectName();
        auto e = std::make_shared<Histo1DPlotEntry>(histo, q);
        addEntry(e);
        relayout();
        return e;
    }

    // FIXME: cannot move a plot to the very end. This code always moves to
    // the left of the destination index.
    void moveEntry(int sourceIndex, int destIndex)
    {
        if (sourceIndex == destIndex || std::begin(entries_) + sourceIndex >= std::end(entries_))
            return;

        auto source = entries_.at(sourceIndex);
        entries_.erase(std::begin(entries_) + sourceIndex);
        entries_.insert(std::begin(entries_) + destIndex, source);

        relayout();
    }

    void relayout()
    {
        //qDebug() << __PRETTY_FUNCTION__ << "entering relayout()";
        std::pair<int, int> scrollPositions =
        {
            scrollArea_->horizontalScrollBar()->value(),
            scrollArea_->verticalScrollBar()->value()
        };

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

        if (!entries_.empty())
        {
            plotMatrix_ = new PlotMatrix(entries_, maxColumns_);
            plotMatrix_->installEventFilter(q);
            plotMatrix_->setMouseTracking(true);
            scrollArea_->setWidget(plotMatrix_);

            scrollArea_->horizontalScrollBar()->setValue(scrollPositions.first);
            scrollArea_->verticalScrollBar()->setValue(scrollPositions.second);
        }
        else
        {
            auto helpText = QSL("Drag and drop histograms and plots here to add them to the view.");
            auto label = new QLabel(helpText);
            label->setAlignment(Qt::AlignCenter);
            scrollArea_->setWidget(label);
        }
        //qDebug() << __PRETTY_FUNCTION__ << "leaving relayout()";
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

        //qDebug() << __PRETTY_FUNCTION__ << "entry count" << entries_.size();

        for (auto &e: entries_)
        {
            // Set resolution reductions based on the user selected maximum.
            for (Qt::Axis axis: { Qt::XAxis, Qt::YAxis })
            {
                if (auto bins = e->binCount(axis);
                    bins > maxVisibleBins_ && maxVisibleBins_ > 0)
                    e->setRRF(axis, bins / maxVisibleBins_);
                else
                    e->setRRF(axis, 0);
                //qDebug("e=%s, axis=%d, rrf=%lu", e->title().toLatin1().constData(), axis, e->rrf(axis));
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

        // Select the active max resolution in the combo box
        if (int idx = combo_maxRes_->findData(maxVisibleBins_);
            idx >= 0)
        {
            combo_maxRes_->setCurrentIndex(idx);
        }

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

            case State::RearrangeActive:
                // Drag & Drop
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

    void saveView()
    {
        const bool isNewView = !analysisGridView_;

        if (isNewView)
        {
            // Collect the userlevels of all sinks in this widget. Use the
            // maximum level as the proposed level for the new grid view.
            int proposedUserLevel = 1;
            std::set<s32> sinkUserLevels;

            for (const auto &e: entries_)
            {
                if (auto ao = e->analysisObject())
                    sinkUserLevels.insert(ao->getUserLevel());
            }

            if (auto it = std::max_element(std::begin(sinkUserLevels), std::end(sinkUserLevels));
                it != std::end(sinkUserLevels) && *it >= 1)
            {
                proposedUserLevel = *it;
            }

            QDialog dlg;
            auto l = new QFormLayout(&dlg);
            auto le_name = new QLineEdit;
            auto spin_userLevel = new QSpinBox;
            spin_userLevel->setMinimum(1);
            spin_userLevel->setMaximum(asp_->getAnalysis()->getMaxUserLevel());
            spin_userLevel->setValue(proposedUserLevel);
            auto bb = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
            l->addRow("View Name", le_name);
            l->addRow("Userlevel", spin_userLevel);
            l->addRow(bb);

            QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

            if (dlg.exec() == QDialog::Rejected)
                return;

            analysisGridView_ = std::make_shared<PlotGridView>();
            analysisGridView_->setObjectName(le_name->text());
            analysisGridView_->setUserLevel(spin_userLevel->value());
            q->setWindowTitle(analysisGridView_->objectName());
            actionSaveView_->setEnabled(false);
        }

        EntryConversionVisitor ecv;
        QSize minTileSize = {};

        for (auto &e: entries_)
        {
            e->accept(ecv);
            minTileSize = e->plot()->minTileSize();
        }

        analysisGridView_->setEntries(std::move(ecv.entries));
        analysisGridView_->setMaxVisibleResolution(maxVisibleBins_);
        analysisGridView_->setAxisScaleType(combo_axisScaleType_->currentData().toInt());
        analysisGridView_->setMaxColumns(maxColumns_);
        analysisGridView_->setMinTileSize(minTileSize);
        analysisGridView_->setCombinedZoom(cb_combinedZoom_->isChecked());
        analysisGridView_->setGaussEnabled(actionGauss_->isChecked());

        if (isNewView)
            // FIXME: eventwidget is currently not being repopulated after adding the grid view
            asp_->getAnalysis()->addObject(analysisGridView_);
        else
            asp_->getAnalysis()->setModified();
    }

    void loadView(const std::shared_ptr<PlotGridView> &view)
    {
        entries_.clear();

        for (const auto &ve: view->entries())
        {
            if (auto sink = asp_->getAnalysis()->getObject<SinkInterface>(ve.sinkId))
            {
                if (auto plotEntry = addSinkElement(sink, ve.elementIndex))
                {
                    plotEntry->setTitle(ve.customTitle);
                    plotEntry->zoomer()->zoom(ve.zoomRect);
                    if (auto sz = view->getMinTileSize(); sz.isValid())
                        plotEntry->plot()->setMinTileSize(sz);
                }
            }
        }

        if (auto maxRes = view->getMaxVisibleResolution(); maxRes > 0)
            maxVisibleBins_ = maxRes;

        if (auto idx = combo_axisScaleType_->findData(view->getAxisScaleType()); idx >= 0)
            combo_axisScaleType_->setCurrentIndex(idx);

        maxColumns_ = std::max(1, view->getMaxColumns());
        spin_columns_->setValue(maxColumns_);
        cb_combinedZoom_->setChecked(view->getCombinedZoom());
        actionGauss_->setChecked(view->isGaussEnabled());

        q->setWindowTitle("PlotGrid " + view->objectName());
        analysisGridView_ = view;
        actionSaveView_->setEnabled(false);

        relayout();
        refresh();
    }

    void contextMenuRequested(const QPoint &pos)
    {
        if (state_ != State::Rearrange)
            return;

        auto entry = entryAt(pos);

        if (!entry || !entry->analysisObject())
         return;

        QMenu menu;

        auto set_custom_title = [this, entry] ()
        {
            QDialog dlg;
            auto l = new QFormLayout(&dlg);
            auto le_title = new QLineEdit;
            le_title->setText(entry->title());
            auto bb = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
            l->addRow("Custom Plot Title", le_title);
            l->addRow(bb);

            QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

            if (dlg.exec() == QDialog::Rejected)
                return;

            entry->setTitle(le_title->text());
            refresh();
        };

        auto clear_custom_title = [this, entry] ()
        {
            entry->setTitle({});
            refresh();
        };

        auto remove_entry = [this, entry] ()
        {
            removeEntry(entry);
        };

        auto title = entry->analysisObject()->objectName();
        if (auto h1dEntry = std::dynamic_pointer_cast<Histo1DSinkPlotEntry>(entry))
            title += QSL("[%1]").arg(h1dEntry->histoIndex);
        menu.addSection(title);
        menu.addAction("Set Custom Title", set_custom_title);
        if (entry->hasCustomTitle())
            menu.addAction("Clear Custom Title", clear_custom_title);
        menu.addAction("Remove plot", remove_entry);

        menu.exec(q->mapToGlobal(pos));
    }
};

MultiPlotWidget::MultiPlotWidget(AnalysisServiceProvider *asp, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    assert(asp);

    d->q = this;
    d->asp_ = asp;
    d->toolBar_ = make_toolbar();
    d->statusBar_ = make_statusbar();

    setAcceptDrops(true);
    setMouseTracking(true);
    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this, &QWidget::customContextMenuRequested,
            this, [this] (const QPoint &pos) { d->contextMenuRequested(pos); });

    auto toolBarFrame = new QFrame;
    toolBarFrame->setFrameStyle(QFrame::StyledPanel);
    make_hbox<0, 0>(toolBarFrame)->addWidget(d->toolBar_);

    d->scrollArea_ = new QScrollArea;
    auto scrollArea = d->scrollArea_;
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->installEventFilter(this);
    scrollArea->setMouseTracking(true);
    scrollArea->viewport()->installEventFilter(this);
    scrollArea->viewport()->setCursor(Qt::OpenHandCursor);
    scrollArea->viewport()->setMouseTracking(true);

    auto layout = make_vbox<2, 2>(this);
    layout->addWidget(toolBarFrame);
    layout->addWidget(scrollArea);
    layout->addWidget(d->statusBar_);

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
        d->combo_axisScaleType_ = combo;
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
    d->actionGauss_ = actionGauss;

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

    auto actionRearrange = tb->addAction(QIcon(":/arrow-out.png"), "Edit");
    actionRearrange->setStatusTip("Drag & drop plots to rearrange."); // Double-click plot titles to edit.");
    actionRearrange->setCheckable(true);

    d->actionSaveView_ = tb->addAction(QIcon(":/document-save.png"), "Save View");

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
        d->spin_columns_ = spinColumns;
    }

    #ifndef NDEBUG
    auto pb_replot = new QPushButton("Replot");
    tb->addWidget(pb_replot);
    connect(pb_replot, &QPushButton::clicked, this, &MultiPlotWidget::replot);
    #endif

    auto plotInteractions = new QActionGroup(this);
    //plotInteractions->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    plotInteractions->addAction(actionPan);
    plotInteractions->addAction(actionZoom);
    plotInteractions->addAction(actionRearrange);

    connect(d->actionSaveView_, &QAction::triggered,
            this, [this] { d->saveView(); });

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
                    if (auto h1dEntry = std::dynamic_pointer_cast<Histo1DPlotEntry>(e))
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

    connect(&d->replotTimer_, &QTimer::timeout,
            this, [this] { d->refresh(); });

    d->replotTimer_.start(Private::DefaultReplotPeriod_ms);
    resize(800, 600);
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

void MultiPlotWidget::addSinkElement(
    const analysis::SinkPtr &sink,
    int elementIndex)
{
    d->addSinkElement(sink, elementIndex);
    d->refresh();
}
void MultiPlotWidget::addHisto1D(const Histo1DPtr &histo)
{
    d->addHisto1D(histo);
    d->refresh();
}

void MultiPlotWidget::setMaxVisibleResolution(size_t maxres)
{
    d->maxVisibleBins_ = maxres;
    d->refresh();
}

size_t MultiPlotWidget::getMaxVisibleResolution() const
{
    return d->maxVisibleBins_;
}

void MultiPlotWidget::loadView(const std::shared_ptr<analysis::PlotGridView> &view)
{
    d->loadView(view);
}

void MultiPlotWidget::clear()
{
    d->entries_.clear();
    d->relayout();
    d->refresh();
}

int MultiPlotWidget::getReplotPeriod() const
{
    return d->replotTimer_.interval();
}

void MultiPlotWidget::setReplotPeriod(int ms)
{
    if (ms < 0)
        d->replotTimer_.stop();
    else
        d->replotTimer_.start(ms);
}

void MultiPlotWidget::replot()
{
    //qDebug() << __PRETTY_FUNCTION__ << "entering replot()";
    d->refresh();
    //qDebug() << __PRETTY_FUNCTION__ << "leaving replot()";
}

size_t MultiPlotWidget::getNumberOfEntries() const
{
    return d->entries_.size();
}

void MultiPlotWidget::dragEnterEvent(QDragEnterEvent *ev)
{
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
    //qDebug() << __PRETTY_FUNCTION__ << ev;
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
        qDebug() << __PRETTY_FUNCTION__ << ev
                 << "sourceIndex =" << sourceIndex
                 << "destIndex =" << destIndex;
        if (sourceIndex >= 0 && destIndex >= 0)
            d->moveEntry(sourceIndex, destIndex);
    }
    else
        QWidget::dropEvent(ev);
}

void MultiPlotWidget::mouseMoveEvent(QMouseEvent *ev)
{
    //qDebug() << __PRETTY_FUNCTION__ << ev;

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

        //if (entry)
        //    qDebug() << __PRETTY_FUNCTION__ << ev << "on entry" << entry->analysisObject().get();

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
    qDebug() << __PRETTY_FUNCTION__ << ev;
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
    qDebug() << __PRETTY_FUNCTION__ << ev;
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

    // Ctrl+Mousewheel
    if (watched == d->scrollArea_->viewport()
        && ev->type() == QEvent::Wheel
        && (dynamic_cast<QWheelEvent *>(ev)->modifiers() & Qt::ControlModifier)
        )
    {
        qDebug() << __PRETTY_FUNCTION__ << watched << ev << "for Ctrl-Wheel zoom";
        // Do not pass the event to the scrollareas viewport which would scroll
        // the vertical scrollbar. See wheelEvent() for the code dealing with
        // wheel events.
        result = true;
    }

    //if (result)
    //    qDebug() << __PRETTY_FUNCTION__ << watched << ev << result;

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

// Opens the sink widget for the given PlotEntry. Does nothing if the PlotEntry
// does not contain a sink.
struct OpenSinkVisitor: public PlotEntryVisitor
{
    explicit OpenSinkVisitor(AnalysisServiceProvider *asp)
       : asp_(asp)
    { }

    void visit(Histo1DPlotEntry *e)
    {
        // Not a sink, so we cannot do anything here.
        (void) e;
    }

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
