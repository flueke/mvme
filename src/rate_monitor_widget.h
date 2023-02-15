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
#ifndef __RATE_MONITOR_WIDGET_H__
#define __RATE_MONITOR_WIDGET_H__

#include <memory>
#include <QDir>
#include <QWidget>
#include "analysis/analysis.h"

/* Similar design to Histo1DWidget:
 * Must be able to display a plain RateHistoryBuffer, a RateSampler and a list
 * of RateSamplers. The list part could be moved into an extra
 * RateMonitorListWidget.
 *
 * Note: limited to displaying a list of RateSamplerPtr, a single
 * RateSamplerPtr or a single raw RateSampler *. The reason is that RateSampler
 * should carry all required information for the widget (except an
 * objectName()).
 *
 * Note: most functionality is only available if in addition to the rates a
 * sink has also been set using setSink(). Not the best design right now but
 * I'm still figuring out what is needed and what isn't.
 * */

struct RateMonitorWidgetPrivate;

class LIBMVME_EXPORT RateMonitorWidget: public QWidget
{
    Q_OBJECT
    public:
        using SinkPtr = std::shared_ptr<analysis::RateMonitorSink>;
        using SinkModifiedCallback = std::function<void (const SinkPtr &)>;

        explicit RateMonitorWidget(
            const SinkPtr &rms,
            SinkModifiedCallback sinkModifiedCallback = {},
            QWidget *parent = nullptr);

        explicit RateMonitorWidget(const a2::RateSamplerPtr &sampler, QWidget *parent = nullptr);
        explicit RateMonitorWidget(const QVector<a2::RateSamplerPtr> &samplers, QWidget *parent = nullptr);

        virtual ~RateMonitorWidget();

        void setSink(const SinkPtr &sink, SinkModifiedCallback sinkModifiedCallback);
        void setPlotExportDirectory(const QDir &dir);

        QVector<a2::RateSamplerPtr> getRateSamplers() const;

    public slots:
        void sinkModified();

    private slots:
        void replot();
        void zoomerZoomed(const QRectF &);
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();
        void updateStatistics();
        void yAxisScalingChanged();

    private:
        explicit RateMonitorWidget(QWidget *parent = nullptr);

        friend struct RateMonitorWidgetPrivate;
        std::unique_ptr<RateMonitorWidgetPrivate> m_d;
};

#endif /* __RATE_MONITOR_WIDGET_H__ */
