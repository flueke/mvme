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
#ifndef __HISTO1D_WIDGET_H__
#define __HISTO1D_WIDGET_H__

#include "analysis_service_provider.h"
#include "histo1d.h"
#include "histo_ui.h"
#include "libmvme_export.h"

#include <QWidget>
#include <qwt_plot_picker.h>

class QwtPlotPicker;

namespace analysis
{
    class CalibrationMinMax;
    class Histo1DSink;
}

struct Histo1DWidgetPrivate;

class LIBMVME_EXPORT Histo1DWidget: public histo_ui::IPlotWidget
{
    Q_OBJECT

    signals:
        void histogramSelected(int histoIndex);
        void zoomerZoomed(const QRectF &zoomRect);

    public:
        using SinkPtr = std::shared_ptr<analysis::Histo1DSink>;
        using HistoSinkCallback = std::function<void (const SinkPtr &)>;
        using HistoList = QVector<std::shared_ptr<Histo1D>>;

        // single histo
        Histo1DWidget(const Histo1DPtr &histo, QWidget *parent = nullptr);

        // list of histos
        Histo1DWidget(const HistoList &histos, QWidget *parent = nullptr);

        virtual ~Histo1DWidget();

        HistoList getHistograms() const;

        void setHistogram(const Histo1DPtr &histo);

        virtual bool eventFilter(QObject *watched, QEvent *e) override;
        virtual bool event(QEvent *event) override;

        friend class Histo1DListWidget;

        void setServiceProvider(AnalysisServiceProvider *asp);
        AnalysisServiceProvider *getServiceProvider() const;
        void setCalibration(const std::shared_ptr<analysis::CalibrationMinMax> &calib);
        void setSink(const SinkPtr &sink, HistoSinkCallback sinkModifiedCallback);
        SinkPtr getSink() const;
        void selectHistogram(int histoIndex);

        void setResolutionReductionFactor(u32 rrf);
        void setResolutionReductionSliderEnabled(bool b);

        QwtPlot *getPlot() override;
        const QwtPlot *getPlot() const override;

        s32 currentHistoIndex() const;

    public slots:
        void replot() override;

    private slots:
        /* IMPORTANT: leave slots invoked by qwt here for now. do not use lambdas!
         * Reason: there is/was a bug where qwt signals could only be succesfully
         * connected using the old SIGNAL/SLOT macros. Newer function pointer based
         * connections did not work. */
        // TODO 10/2018: recheck this. It might have just been an issue with
        // missing casts of overloaded signals.
        void onZoomerZoomed(const QRectF &);
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();

        void on_tb_subRange_clicked();
        void on_tb_rate_toggled(bool checked);
        void on_tb_gauss_toggled(bool checked);
        void on_tb_test_clicked();
        void on_ratePointerPicker_selected(const QPointF &);
        void onHistoSpinBoxValueChanged(int index);

    private:
        std::unique_ptr<Histo1DWidgetPrivate> m_d;
        friend struct Histo1DWidgetPrivate;
};

#if 0

class Histo1DSinkWidget: public histo_ui::PlotWidget
{
    Q_OBJECT
    public:
        const Histo1DSinkPtr getSink() const { return m_sink; }

        int selectedHistogramIndex() const;

    public slots:
        void selectHistogram(int histoIndex);

    private:
        Histo1DSinkWidget(const Histo1DSinkPtr &sink, QWidget *parent = nullptr);

        friend Histo1DSinkWidget *make_h1dsink_widget(
            const Histo1DSinkPtr &histoSink, QWidget *parent);

        Histo1DSinkPtr m_sink;
};

Histo1DSinkWidget *make_h1dsink_widget(
    const Histo1DSinkPtr &histoSink, QWidget *parent=nullptr);

histo_ui::PlotWidget *make_h1d_widget(
    const Histo1DPtr &histo, QWidget *parent=nullptr);
#endif


#endif /* __HISTO1D_WIDGET_H__ */
