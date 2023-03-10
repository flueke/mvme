/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "rate_monitor_plot_widget.h"

#include <QBoxLayout>
#include <QDebug>

#include <cmath>
#include <qwt_curve_fitter.h>
#include <qwt_date_scale_draw.h>
#include <qwt_date_scale_engine.h>
#include <qwt_plot_curve.h>
#include <qwt_plot.h>
#include <qwt_plot_legenditem.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>

#include "analysis/a2/util/nan.h"
#include "histo_util.h"
#include "qt_util.h"
#include "scrollzoomer.h"
#include "util/assert.h"
#include "util/counters.h"
#include "util.h"

//
// RateMonitorPlotWidget
//

// Note: QwtDateScaleEngine expects values in milliseconds while the
// RateSamplers use seconds internally. That's why the factor 1000.0 is used
// frequently in the code. This is all pretty hacky: XScaleType was added later
// on.

using XScaleType = RateMonitorPlotWidget::XScaleType;

static inline QRectF make_bounding_rect(
    const RateSampler *sampler,
    XScaleType xScaleType)
{
    QRectF result;

    if (!sampler->rateHistory.empty())
    {
        double xMin = sampler->getFirstSampleTime();
        double xMax = sampler->getLastSampleTime();
        double yMax = get_max_value(sampler->rateHistory);

        if (xScaleType == XScaleType::Time)
        {
            xMin *= 1000.0;
            xMax *= 1000.0;
        }

        result = QRectF(xMin, 0.0,
                        xMax, yMax);
    }

    return result;
}

struct RateMonitorPlotData: public QwtSeriesData<QPointF>
{
    explicit RateMonitorPlotData(const RateSamplerPtr &sampler)
        : QwtSeriesData<QPointF>()
        , sampler_(sampler)
        , xScaleType_(XScaleType::Time)
    {
        beginDrawing();
    }

    size_t size() const override
    {
        return sampler_->historySize();
    }

    // Note: if getSample(i) returns a NaN value this method will search
    // backwards until it finds a non-NaN sample and return that value. This
    // fixes severe performance issues when plotting data which includes NaNs
    // while also being a visual improvement over replacing the NaN with 0.
    //
    // Note2: NaNs are frequently recorded when using the VMMR to read out MMR
    // monitoring data as that happens on a best-effort basis.
    //
    // Note3 (210310): Things are very slow when many NaNs are present.
    // Implemented an optimization by remembering the last valid sample value
    // and returning it in case of a NaN sample.
    virtual QPointF sample(size_t i) const override
    {
        double x = sampler_->getSampleTime(i);
        double y = sampler_->getSample(i);

        if (xScaleType_ == XScaleType::Time)
            x *= 1000.0;

        auto si = static_cast<ssize_t>(i);
        assert(si > prevSampleIndex_);

        if (std::isnan(y))
            y = prevSampleValue_;

        prevSampleIndex_ = si;
        prevSampleValue_ = y;

        return {x, y};
    }

    virtual QRectF boundingRect() const override
    {
        auto result = make_bounding_rect(sampler_.get(), xScaleType_);
        return result;
    }

    RateSamplerPtr sampler_;
    XScaleType xScaleType_;

    mutable ssize_t prevSampleIndex_;
    mutable double prevSampleValue_;

    void beginDrawing() const
    {
        prevSampleIndex_ = -1;
        prevSampleValue_ = 0.0;
    }

    void setXScaleType(XScaleType scaleType)
    {
        xScaleType_ = scaleType;
    }
};

class RateMonitorPlotCurve: public QwtPlotCurve
{
    public:
        using QwtPlotCurve::QwtPlotCurve;

    protected:
        void drawLines(QPainter *painter,
                       const QwtScaleMap &xMap, const QwtScaleMap &yMap,
                       const QRectF &canvasRect, int from, int to ) const override
        {
            auto rmpd = reinterpret_cast<const RateMonitorPlotData *>(data());
            rmpd->beginDrawing();

            auto tStart = std::chrono::steady_clock::now();

            QwtPlotCurve::drawLines(painter, xMap, yMap, canvasRect, from, to);

            auto dt = std::chrono::steady_clock::now() - tStart;

            //qDebug() << __PRETTY_FUNCTION__ << "dt =" <<
            //    std::chrono::duration_cast<std::chrono::milliseconds>(dt).count() << "ms";
        }
};


struct RateMonitorPlotWidgetPrivate
{
    QVector<RateSamplerPtr> m_samplers;

    QwtPlot *m_plot;
    ScrollZoomer *m_zoomer;
    QVector<QwtPlotCurve *> m_curves;
    QwtPlotLegendItem m_plotLegendItem;
    RateMonitorPlotWidget::XScaleType xScaleType;
};

struct DateScaleFormat
{
    QwtDate::IntervalType interval;
    QString format;
};

static const DateScaleFormat DateScaleFormatTable[] =
{
    { QwtDate::Millisecond,     QSL("H'h' m'm' s's' zzz'ms'") },
    { QwtDate::Second,          QSL("H'h' m'm' s's'") },
    { QwtDate::Minute,          QSL("H'h' m'm'") },
    { QwtDate::Hour,            QSL("H'h' m'm'") },
    { QwtDate::Day,             QSL("d 'd'") },
};

RateMonitorPlotWidget::RateMonitorPlotWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<RateMonitorPlotWidgetPrivate>())
{
    // plot and curve
    d->m_plot = new QwtPlot(this);
    d->m_plot->canvas()->setMouseTracking(true);
    d->m_plot->axisWidget(QwtPlot::yLeft)->setTitle("Rate");

    d->m_plotLegendItem.attach(d->m_plot);

    // zoomer
    d->m_zoomer = new ScrollZoomer(d->m_plot->canvas());

    /* NOTE: using connect with the c++ pointer-to-member syntax with these qwt signals does
     * not work for some reason. */
    // TODO: use casts to select specific overloads of these signals
    DO_AND_ASSERT(connect(d->m_zoomer, SIGNAL(zoomed(const QRectF &)),
                       this, SLOT(zoomerZoomed(const QRectF &))));
    DO_AND_ASSERT(connect(d->m_zoomer, &ScrollZoomer::mouseCursorMovedTo,
                       this, &RateMonitorPlotWidget::mouseCursorMovedToPlotCoord));
    DO_AND_ASSERT(connect(d->m_zoomer, &ScrollZoomer::mouseCursorLeftPlot,
                       this, &RateMonitorPlotWidget::mouseCursorLeftPlot));

    // layout
    auto widgetLayout = new QHBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);
    widgetLayout->setSpacing(0);
    widgetLayout->addWidget(d->m_plot);

    setXScaleType(XScaleType::Samples);
    setYAxisScale(AxisScale::Linear);
}

RateMonitorPlotWidget::~RateMonitorPlotWidget()
{
    qDeleteAll(d->m_curves);
}

void RateMonitorPlotWidget::addRateSampler(const RateSamplerPtr &sampler,
                                           const QString &title,
                                           const QColor &color)
{
    assert(sampler);
    assert(d->m_samplers.size() == d->m_curves.size());

    auto curve = std::make_unique<RateMonitorPlotCurve>(title);

    auto rmpd = new RateMonitorPlotData(sampler);
    rmpd->setXScaleType(getXScaleType());
    curve->setData(rmpd);
    curve->setPen(color);
    curve->setStyle(QwtPlotCurve::Lines);
    //curve->setPaintAttribute(QwtPlotCurve::FilterPoints);
    curve->setRenderHint(QwtPlotItem::RenderAntialiased);

    curve->attach(d->m_plot);

    d->m_curves.push_back(curve.release());
    d->m_samplers.push_back(sampler);

#if 0
    qDebug() << __PRETTY_FUNCTION__ << "added rate. title =" << title << ", color =" << color
        << ", capacity =" << sampler->historyCapacity()
        << ", new plot count =" << d->m_curves.size();
#endif

    assert(d->m_samplers.size() == d->m_curves.size());
}

void RateMonitorPlotWidget::removeRateSampler(const RateSamplerPtr &sampler)
{
    removeRateSampler(d->m_samplers.indexOf(sampler));
}

void RateMonitorPlotWidget::removeRateSampler(int index)
{
    assert(d->m_samplers.size() == d->m_curves.size());

    if (0 <= index && index < d->m_samplers.size())
    {
        assert(index < d->m_curves.size());

#if 0
        qDebug() << __PRETTY_FUNCTION__ << "removing plot with index" << index;
#endif
        auto curve = std::unique_ptr<QwtPlotCurve>(d->m_curves.at(index));
        curve->detach();
        d->m_curves.remove(index);
        d->m_samplers.remove(index);
    }

    assert(d->m_samplers.size() == d->m_curves.size());
}

void RateMonitorPlotWidget::removeAllRateSamplers()
{
    while (rateCount())
    {
        removeRateSampler(0);
    }
}

int RateMonitorPlotWidget::rateCount() const
{
    assert(d->m_samplers.size() == d->m_curves.size());
    return d->m_samplers.size();
}

QVector<RateSamplerPtr> RateMonitorPlotWidget::getRateSamplers() const
{
    assert(d->m_samplers.size() == d->m_curves.size());
    return d->m_samplers;
}

RateSamplerPtr RateMonitorPlotWidget::getRateSampler(int index) const
{
    return d->m_samplers.value(index);
}

bool RateMonitorPlotWidget::isInternalLegendVisible() const
{
    return d->m_plotLegendItem.plot();
}

void RateMonitorPlotWidget::setInternalLegendVisible(bool visible)
{
    if (visible && !d->m_plotLegendItem.plot())
        d->m_plotLegendItem.attach(d->m_plot);
    else if (!visible && d->m_plotLegendItem.plot())
        d->m_plotLegendItem.detach();
}

static bool axis_is_lin(QwtPlot *plot, QwtPlot::Axis axis)
{
    return dynamic_cast<QwtLinearScaleEngine *>(plot->axisScaleEngine(axis));
}

static bool axis_is_log(QwtPlot *plot, QwtPlot::Axis axis)
{
    return dynamic_cast<QwtLogScaleEngine *>(plot->axisScaleEngine(axis));
}

void RateMonitorPlotWidget::replot()
{
    // Determine x-axis range by looking at the first and last sample time of each rate sampler.
    double xMin = std::numeric_limits<double>::max();
    double xMax = std::numeric_limits<double>::lowest();

    if (d->m_zoomer->zoomRectIndex() == 0) // fully zoomed out
    {
        bool hasSamples = false;

        for (auto &sampler: d->m_samplers)
        {
            if (!sampler->rateHistory.empty())
            {
                xMin = std::min(xMin, sampler->getFirstSampleTime());
                xMax = std::max(xMax, sampler->getLastSampleTime());
                hasSamples = true;
            }
        }

        if (!hasSamples)
        {
            xMin = 0.0;
            xMax = 60.0;
        }
    }
    else // zoomed in
    {
        auto scaleDiv = d->m_plot->axisScaleDiv(QwtPlot::xBottom);
        xMin = scaleDiv.lowerBound() / 1000.0;
        xMax = scaleDiv.upperBound() / 1000.0;
    }

    switch (getXScaleType())
    {
        case XScaleType::Time:
            {
                // scale the x-values to milliseconds
                double xMin_ms = xMin * 1000.0;
                double xMax_ms = xMax * 1000.0;

                // if fully zoomed out set the x-interval to the min and max x-value over
                // all the samplers
                if (d->m_zoomer->zoomRectIndex() == 0)
                    d->m_plot->setAxisScale(QwtPlot::xBottom, xMin_ms, xMax_ms);
            }
            break;
        case XScaleType::Samples:
            {
                // if fully zoomed out set the x-interval to the min and max x-value over
                // all the samplers
                if (d->m_zoomer->zoomRectIndex() == 0)
                    d->m_plot->setAxisScale(QwtPlot::xBottom, xMin, xMax);
            }
            break;
    }


    AxisInterval visibleXInterval = { xMin, xMax };

    // y-axis range

    double yMin = 0.0;
    double yMax = 1.0;

    for (auto &sampler: d->m_samplers)
    {
        if (!sampler->rateHistory.empty())
        {
            auto stats = calc_rate_sampler_stats(*sampler, visibleXInterval);
            auto yInterval = stats.intervals[Qt::YAxis];

            if (!std::isnan(yInterval.minValue))
                yMin = std::min(yMin, yInterval.minValue);

            if (!std::isnan(yInterval.maxValue))
                yMax = std::max(yMax, yInterval.maxValue);
        }
    }

    if (axis_is_log(d->m_plot, QwtPlot::yLeft))
    {
        yMin = 1.0;
        yMax = std::pow(yMax, 1.2);
    }
    else
    {
        yMax = yMax * 1.2;
    }

    d->m_plot->setAxisScale(QwtPlot::yLeft, yMin, yMax);

    // Tell Qwt to update and render
    d->m_plot->updateAxes();
    d->m_plot->replot();
}

void RateMonitorPlotWidget::setYAxisScale(AxisScale scaleType)
{

    if (scaleType == AxisScale::Linear && !axis_is_lin(d->m_plot, QwtPlot::yLeft))
    {
        d->m_plot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
        d->m_plot->setAxisAutoScale(QwtPlot::yLeft, true);
    }
    else if (scaleType == AxisScale::Logarithmic && !axis_is_log(d->m_plot, QwtPlot::yLeft))
    {
        auto scaleEngine = new QwtLogScaleEngine;
        scaleEngine->setTransformation(new MinBoundLogTransform);
        d->m_plot->setAxisScaleEngine(QwtPlot::yLeft, scaleEngine);
    }

    replot();
}

AxisScale RateMonitorPlotWidget::getYAxisScale() const
{
    if (axis_is_lin(d->m_plot, QwtPlot::yLeft))
        return AxisScale::Linear;

    assert(axis_is_log(d->m_plot, QwtPlot::yLeft));

    return AxisScale::Logarithmic;
}

void RateMonitorPlotWidget::setXScaleType(const XScaleType &scaleType)
{
    d->xScaleType = scaleType;

    switch (scaleType)
    {
        case XScaleType::Time:
            {
                auto engine = new QwtDateScaleEngine(Qt::UTC);
                d->m_plot->setAxisScaleEngine(QwtPlot::xBottom, engine);

                auto draw = new QwtDateScaleDraw(Qt::UTC);

                for (size_t i = 0; i < ArrayCount(DateScaleFormatTable); i++)
                {
                    draw->setDateFormat(
                        DateScaleFormatTable[i].interval,
                        DateScaleFormatTable[i].format);
                }

                d->m_plot->setAxisScaleDraw(QwtPlot::xBottom, draw);
            }
            break;
        case XScaleType::Samples:
            {
                auto engine = new QwtLinearScaleEngine();
                d->m_plot->setAxisScaleEngine(QwtPlot::xBottom, engine);
                auto draw = new QwtScaleDraw();
                d->m_plot->setAxisScaleDraw(QwtPlot::xBottom, draw);
            }
            break;
    }

    for (auto curve: d->m_curves)
    {
        auto rmpd = reinterpret_cast<RateMonitorPlotData *>(curve->data());
        rmpd->setXScaleType(scaleType);
    }

    replot();
}

RateMonitorPlotWidget::XScaleType RateMonitorPlotWidget::getXScaleType() const
{
    return d->xScaleType;
}

void RateMonitorPlotWidget::zoomerZoomed(const QRectF &)
{
    replot();
}

void RateMonitorPlotWidget::mouseCursorMovedToPlotCoord(QPointF)
{
}

void RateMonitorPlotWidget::mouseCursorLeftPlot()
{
}

QwtPlot *RateMonitorPlotWidget::getPlot()
{
    return d->m_plot;
}

QwtPlotCurve *RateMonitorPlotWidget::getPlotCurve(const RateSamplerPtr &sampler) const
{
    int index = d->m_samplers.indexOf(sampler);

    if (0 < index && index < d->m_samplers.size())
    {
        assert(index < d->m_curves.size());
        return d->m_curves.at(index);
    }

    return nullptr;
}

QwtPlotCurve *RateMonitorPlotWidget::getPlotCurve(int index) const
{
    if (0 < index && index < d->m_samplers.size())
    {
        assert(index < d->m_curves.size());
        return d->m_curves.at(index);
    }

    return nullptr;
}

QVector<QwtPlotCurve *> RateMonitorPlotWidget::getPlotCurves() const
{
    return d->m_curves;
}

ScrollZoomer *RateMonitorPlotWidget::getZoomer() const
{
    return d->m_zoomer;
}
