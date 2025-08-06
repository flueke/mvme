#include "histo_ops_widget.h"
#include "histo_ops_widget_p.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QStackedWidget>
#include <QTimer>
#include <qwt_plot_histogram.h>

#include "analysis_ui_util.h"
#include "histo1d_widget.h"
#include "histo2d_widget.h"

using namespace analysis::ui;

struct HistogramOperationsWidget::Private
{
    HistogramOperationsWidget *q = nullptr;
    HistoOpsEditDialog *editDialog_ = nullptr;
    AnalysisServiceProvider *asp_ = nullptr;
    // This is the analysis object we should display/work on.
    std::shared_ptr<analysis::HistogramOperation> histoOp_;
    // Copy of the 1D histogram obtained from histoOp_ if it is operating on 1D histograms.
    //std::shared_ptr<Histo1D> histo_ = {};
    // Qwt plot item for the 1D histogram.
    //QwtPlotHistogram *plotHisto_ = {};
    // Qwt data object for the 1D histogram. This is owned by qwt.
    //histo_ui::Histo1DIntervalData *histoData_ = {};

    // Widgets to display the resulting histogram or a placeholder message. Only
    // one can be visible/active at a time.
    Histo1DWidget *histo1DWidget_ = nullptr;
    Histo2DWidget *histo2DWidget_ = nullptr;
    HistoOpsWidgetPlaceHolder *placeHolderWidget_ = nullptr;

    QStackedWidget *plotStackWidget_ = nullptr;

    QTimer replotTimer_;
};

static const s32 ReplotPeriod_ms = 1000;

HistogramOperationsWidget::HistogramOperationsWidget(AnalysisServiceProvider *asp, QWidget *parent)
    : histo_ui::IPlotWidget(parent)
    , d(std::make_unique<Private>())
{
    assert(asp);
    d->q = this;
    d->asp_ = asp;
    d->histo1DWidget_ = new Histo1DWidget;
    d->histo2DWidget_ = new Histo2DWidget;
    d->placeHolderWidget_ = new HistoOpsWidgetPlaceHolder;

    d->plotStackWidget_ = new QStackedWidget;
    d->plotStackWidget_->addWidget(d->histo1DWidget_);
    d->plotStackWidget_->addWidget(d->histo2DWidget_);
    d->plotStackWidget_->addWidget(d->placeHolderWidget_);

    auto l = make_vbox<0, 0>(this);
    l->addWidget(d->plotStackWidget_);

    auto switch_to_next_in_stack = [this]() {
        auto idx = (d->plotStackWidget_->currentIndex() + 1) % d->plotStackWidget_->count();
        d->plotStackWidget_->setCurrentIndex(idx);
    };

    insert_action_at_front(d->histo1DWidget_->getToolBar(), "dev next display widget", this, switch_to_next_in_stack);
    insert_action_at_front(d->histo2DWidget_->getToolBar(), "dev next display widget", this, switch_to_next_in_stack);
    insert_action_at_front(d->placeHolderWidget_->getToolBar(), "dev next display widget", this, switch_to_next_in_stack);

    setAcceptDrops(true);
    setMouseTracking(true);
    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(&d->replotTimer_, &QTimer::timeout, this, &HistogramOperationsWidget::replot);
    d->replotTimer_.start(ReplotPeriod_ms);

    d->editDialog_ = new HistoOpsEditDialog(this);
    add_widget_close_action(this);
    // When directly showing the dialog it becomes modal and sticks in front of
    // all other application windows. Showing it delayed in the event loop makes
    // it non-modal and stay on the same layer as the histo ops widget itself.
    QTimer::singleShot(0, this, [this] {
        d->editDialog_->show();
    });
}

HistogramOperationsWidget::~HistogramOperationsWidget()
{
}

void HistogramOperationsWidget::setHistoOp(const std::shared_ptr<analysis::HistogramOperation> &op)
{
    d->histoOp_ = op;
    replot();
}

std::shared_ptr<analysis::HistogramOperation> HistogramOperationsWidget::getHistoOp() const
{
    return d->histoOp_;
}

QwtPlot *HistogramOperationsWidget::getPlot()
{
    return d->histo1DWidget_->getPlot();
}

const QwtPlot *HistogramOperationsWidget::getPlot() const
{
    return d->histo1DWidget_->getPlot();
}

QToolBar *HistogramOperationsWidget::getToolBar()
{
    return d->histo1DWidget_->getToolBar();
}

QStatusBar *HistogramOperationsWidget::getStatusBar()
{
    return d->histo1DWidget_->getStatusBar();
}

AnalysisServiceProvider *HistogramOperationsWidget::getServiceProvider() const
{
    return d->asp_;
}

void HistogramOperationsWidget::replot()
{
    // if no histoOp_ is set switch to the placeholder widget.
    // otherwise if in 1d mode show and update the 1d widget
    // otherwise show and update the 2d widget

    if (!d->histoOp_ || !d->histoOp_->getEntryType().has_value())
    {
        d->plotStackWidget_->setCurrentWidget(d->placeHolderWidget_);
    }
    else if (d->histoOp_->isHisto1D())
    {
        d->plotStackWidget_->setCurrentWidget(d->histo1DWidget_);
        d->histo1DWidget_->setHistogram(d->histoOp_->getResultHisto1D());
    }
    else if (d->histoOp_->isHisto2D())
    {
        d->plotStackWidget_->setCurrentWidget(d->histo2DWidget_);
        d->histo2DWidget_->setHistogram(d->histoOp_->getResultHisto2D());
    }
    else
    {
        d->plotStackWidget_->setCurrentWidget(d->placeHolderWidget_);
    }

    if (d->histo1DWidget_->isVisible())
        d->histo1DWidget_->replot();
    else if (d->histo2DWidget_->isVisible())
        d->histo2DWidget_->replot();

    // now update the editor dialog
    if (d->editDialog_)
    {
        d->editDialog_->refresh();
    }
}

void HistogramOperationsWidget::dragEnterEvent(QDragEnterEvent *ev)
{
    auto analysis = d->asp_->getAnalysis();

    if (!analysis || !ev->mimeData()->hasFormat(SinkObjectRefMimeType))
    {
        QWidget::dragEnterEvent(ev);
        return;
    }

    auto curEntryType = d->histoOp_->getEntryType();

    auto objectRefs = decode_object_ref_list(ev->mimeData()->data(SinkObjectRefMimeType));

    for (const auto &ref: objectRefs)
    {
        if (auto sink = analysis->getObject<analysis::Histo1DSink>(ref.id))
        {
            if (curEntryType.has_value() && *curEntryType != analysis::HistogramOperation::EntryType::Histo1D)
            {
                QWidget::dragEnterEvent(ev);
                return;
            }
        }
        else if (auto sink = analysis->getObject<analysis::Histo2DSink>(ref.id))
        {
            if (curEntryType.has_value() && *curEntryType != analysis::HistogramOperation::EntryType::Histo2D)
            {
                QWidget::dragEnterEvent(ev);
                return;
            }
        }
    }

    ev->acceptProposedAction();
}

void HistogramOperationsWidget::dropEvent(QDropEvent *ev)
{
    auto analysis = d->asp_->getAnalysis();

    if (!d->histoOp_ || !analysis || !ev->mimeData()->hasFormat(SinkObjectRefMimeType))
    {
        QWidget::dropEvent(ev);
        return;
    }

    auto objectRefs = decode_object_ref_list(ev->mimeData()->data(SinkObjectRefMimeType));

    for (const auto &ref: objectRefs)
    {
        analysis::HistogramOperation::Entry entry{ref.id, ref.index};
        // try to add the entries. if it does not match the current type it's
        // not going to be added.
        d->histoOp_->addEntry(entry);
    }

    // Note: deliberately not accepting the drop event here. Otherwise it will
    // _move_ nodes from the analysis trees onto this.
    replot();
}
