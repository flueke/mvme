#include "histo_ui.h"

#include <QBoxLayout>
#include <QEvent>
#include <QMouseEvent>
#include <QToolBar>
#include <QStatusBar>
#include <QwtScaleMap>
#include <spdlog/spdlog.h>

namespace histo_ui
{

QRectF canvas_to_scale(const QwtPlot *plot, const QRect &rect)
{
    const QwtScaleMap xMap = plot->canvasMap( QwtPlot::xBottom);
    const QwtScaleMap yMap = plot->canvasMap( QwtPlot::yLeft);

    return QwtScaleMap::invTransform( xMap, yMap, rect );
}

QPointF canvas_to_scale(const QwtPlot *plot, const QPoint &pos)
{
    const QwtScaleMap xMap = plot->canvasMap( QwtPlot::xBottom);
    const QwtScaleMap yMap = plot->canvasMap( QwtPlot::yLeft);

    return QPointF(
        xMap.invTransform( pos.x() ),
        yMap.invTransform( pos.y() )
        );
}

struct PlotWidget::Private
{
    Private(PlotWidget *q_)
        : q(q_)
    {
    }

    PlotWidget *q;
    QToolBar *toolbar;
    QwtPlot *plot;
    QStatusBar *statusbar;
    QVBoxLayout *layout;
};

PlotWidget::PlotWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>(this))
{
    d->toolbar = new QToolBar;
    d->plot = new QwtPlot;
    d->statusbar = new QStatusBar;

    //setMouseTracking(true);
    //installEventFilter(this);

    d->plot->setMouseTracking(true);
    d->plot->installEventFilter(this);

    d->plot->canvas()->setMouseTracking(true);
    d->plot->installEventFilter(this);

    auto toolbarFrame = new QFrame;
    toolbarFrame->setFrameStyle(QFrame::StyledPanel);
    {
        auto layout = new QHBoxLayout(toolbarFrame);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(d->toolbar);
    }

    d->layout = new QVBoxLayout(this);
    d->layout->setContentsMargins(2, 2, 2, 2);
    d->layout->setSpacing(2);
    d->layout->addWidget(toolbarFrame);
    d->layout->addWidget(d->plot);
    d->layout->addWidget(d->statusbar);
    d->layout->setStretch(1, 1);

    resize(600, 400);
}

PlotWidget::~PlotWidget()
{
}

QwtPlot *PlotWidget::getPlot()
{
    return d->plot;
}

const QwtPlot *PlotWidget::getPlot() const
{
    return d->plot;
}

QToolBar *PlotWidget::getToolBar()
{
    return d->toolbar;
}

QStatusBar *PlotWidget::getStatusBar()
{
    return d->statusbar;
}

bool PlotWidget::eventFilter(QObject *object, QEvent *event)
{
    if (object && object == getPlot())
    {
        switch (event->type())
        {
            case QEvent::Enter:
                emit mouseEnteredPlot();
                break;

            case QEvent::Leave:
                emit mouseLeftPlot();
                break;

            case QEvent::MouseMove:
                {
                    auto me = static_cast<const QMouseEvent *>(event);
                    auto pos = getPlot()->canvas()->mapFromGlobal(me->globalPos());
                    emit mouseMoveOnPlot(canvas_to_scale(getPlot(), pos));
                }
                break;

            default:
                break;
        }
    }
#if 0
    else if (object && object == this)
    {
        switch (event->type())
        {
            case QEvent::MouseMove:
                {
                    auto me = static_cast<const QMouseEvent *>(event);
                    auto pos = me->pos();
                    spdlog::info("mouse move on this");
                    emit mouseMoveOnPlot(canvas_to_scale(getPlot(), pos));
                }
                break;

            default:
                break;
        }
    }
    else if (object && object == getPlot()->canvas())
    {
        int x = 42;
    }
#endif

    return QWidget::eventFilter(object, event);
}


}
