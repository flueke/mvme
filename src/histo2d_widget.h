/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __HISTO2D_WIDGET_H__
#define __HISTO2D_WIDGET_H__

#include "analysis/analysis_fwd.h"
#include "analysis_service_provider.h"
#include "histo2d.h"
#include "histo_ui.h"
#include "libmvme_export.h"

class QwtPlot;
class QwtLinearColorMap;

namespace analysis
{
    class Histo1DSink;
    class Histo2DSink;
};

struct Histo2DWidgetPrivate;

class LIBMVME_EXPORT Histo2DWidget: public histo_ui::IPlotWidget
{
    Q_OBJECT

    public:
        using SinkPtr = std::shared_ptr<analysis::Histo2DSink>;
        using HistoSinkCallback = std::function<void (const SinkPtr &)>;
        using MakeUniqueOperatorNameFunction = std::function<QString (const QString &name)>;
        using Histo1DSinkPtr = std::shared_ptr<analysis::Histo1DSink>;

        Histo2DWidget(const Histo2DPtr histoPtr, QWidget *parent = 0);
        Histo2DWidget(Histo2D *histo, QWidget *parent = 0);
        Histo2DWidget(const Histo1DSinkPtr &histo1DSink, AnalysisServiceProvider *asp, QWidget *parent = 0);
        ~Histo2DWidget();

        void setServiceProvider(AnalysisServiceProvider *asp);
        AnalysisServiceProvider *getServiceProvider() const;
        void setSink(const SinkPtr &sink,
                     HistoSinkCallback addSinkCallback,
                     HistoSinkCallback sinkModifiedCallback,
                     MakeUniqueOperatorNameFunction makeUniqueOperatorNameFunction);

        SinkPtr getSink() const;

        bool event(QEvent *event) override;

        QwtPlot *getQwtPlot();

        void setLinZ();
        void setLogZ();

        QwtPlot *getPlot() override;
        const QwtPlot *getPlot() const override;

    public slots:
        void replot();

    private slots:
        void exportPlot();
        void exportPlotToClipboard();
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();
        void displayChanged();
        void zoomerZoomed(const QRectF &);
        void on_tb_info_clicked();
        void on_tb_subRange_clicked();
        void on_tb_projX_clicked();
        void on_tb_projY_clicked();

    private:
        explicit Histo2DWidget(QWidget *parent = 0);

        bool zAxisIsLog() const;
        bool zAxisIsLin() const;
        QwtLinearColorMap *getColorMap() const;
        void updateCursorInfoLabel();
        void doXProjection();
        void doYProjection();

        std::unique_ptr<Histo2DWidgetPrivate> m_d;
        friend struct Histo2DWidgetPrivate;
};

#endif /* __HISTO2D_WIDGET_H__ */
