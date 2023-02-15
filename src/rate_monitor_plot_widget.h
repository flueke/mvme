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
#ifndef __RATE_MONITOR_PLOT_WIDGET_H__
#define __RATE_MONITOR_PLOT_WIDGET_H__

#include <QWidget>

#include "libmvme_export.h"
#include "rate_monitor_base.h"
#include "util/plot.h"

class QwtPlot;
class QwtPlotCurve;
class ScrollZoomer;

struct RateMonitorPlotWidgetPrivate;

class LIBMVME_EXPORT RateMonitorPlotWidget: public QWidget
{
    Q_OBJECT

    public:
        using XScaleType = RateMonitorXScaleType;

        explicit RateMonitorPlotWidget(QWidget *parent = nullptr);
        ~RateMonitorPlotWidget();

        void addRateSampler(const RateSamplerPtr &sampler, const QString &title = QString(),
                            const QColor &color = Qt::black);
        void removeRateSampler(const RateSamplerPtr &sampler);
        void removeRateSampler(int index);
        void removeAllRateSamplers();
        int rateCount() const;
        QVector<RateSamplerPtr> getRateSamplers() const;
        RateSamplerPtr getRateSampler(int index) const;

        /* Log or lin scaling for the Y-Axis. */
        AxisScale getYAxisScale() const;
        void setYAxisScale(AxisScale scaling);

        XScaleType getXScaleType() const;
        void setXScaleType(const XScaleType &axisType);

        bool isInternalLegendVisible() const;
        void setInternalLegendVisible(bool b);

        // internal qwt objects
        QwtPlot *getPlot();

        QwtPlotCurve *getPlotCurve(const RateSamplerPtr &rate) const;
        QwtPlotCurve *getPlotCurve(int index) const;
        QVector<QwtPlotCurve *> getPlotCurves() const;
        ScrollZoomer *getZoomer() const;

    public slots:
        void replot();

    private slots:
        void zoomerZoomed(const QRectF &);
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();

    private:
        std::unique_ptr<RateMonitorPlotWidgetPrivate> d;
};

/* Returns a bounding rect for the use with qwt components.
 * This version uses the capacity of the buffer for the x-axis max value. */
inline QRectF get_qwt_bounding_rect_capacity(const RateHistoryBuffer &rh)
{

    double max_value = get_max_value(rh);
    auto result = QRectF(0.0, 0.0, rh.capacity() * 1000.0, max_value);
    return result;
}

/* Returns a bounding rect for the use with qwt components.
 * This version uses the current size of buffer for the x-axis max value. */
inline QRectF get_qwt_bounding_rect_size(const RateHistoryBuffer &rh)
{

    double max_value = get_max_value(rh);
    auto result = QRectF(0.0, 0.0, rh.size() * 1000.0, max_value);
    return result;
}

#endif /* __RATE_MONITOR_PLOT_WIDGET_H__ */
