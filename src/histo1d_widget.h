/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include "histo1d.h"
#include <QSpinBox>
#include <QWidget>

class QTextStream;
class QTimer;
class QwtPlotCurve;
class QwtPlotHistogram;
class QwtPlotPicker;
class QwtPlotTextLabel;
class QwtText;
class ScrollZoomer;
class MVMEContext;

namespace analysis
{
    class CalibrationMinMax;
    class Histo1DSink;
}

struct Histo1DWidgetPrivate;

class Histo1DWidget: public QWidget
{
    Q_OBJECT
    public:
        using SinkPtr = std::shared_ptr<analysis::Histo1DSink>;
        using HistoSinkCallback = std::function<void (const SinkPtr &)>;

        // Convenience constructor that enables the widget to keep the histo alive.
        Histo1DWidget(const Histo1DPtr &histo, QWidget *parent = 0);
        Histo1DWidget(Histo1D *histo, QWidget *parent = 0);
        ~Histo1DWidget();

        void setHistogram(const Histo1DPtr &histo);
        void setHistogram(Histo1D *histo);

        virtual bool eventFilter(QObject *watched, QEvent *e) override;
        virtual bool event(QEvent *event) override;

        friend class Histo1DListWidget;

        void setContext(MVMEContext *context) { m_context = context; }
        void setCalibrationInfo(const std::shared_ptr<analysis::CalibrationMinMax> &calib,
                                s32 histoAddress);
        void setSink(const SinkPtr &sink, HistoSinkCallback sinkModifiedCallback);

        //QwtPlotCurve *getPlotCurve() { return m_plotCurve; }

    public slots:
        void replot();

    private slots:
        void exportPlot();
        void exportPlotToClipboard();
        void saveHistogram();
        void zoomerZoomed(const QRectF &);
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();
        void updateStatistics();
        void displayChanged();
        void on_tb_subRange_clicked();
        void on_tb_rate_toggled(bool checked);
        void on_tb_gauss_toggled(bool checked);
        void on_tb_test_clicked();
        void on_ratePointerPicker_selected(const QPointF &);

    private:
        void updateAxisScales();
        bool yAxisIsLog();
        bool yAxisIsLin();
        void updateCursorInfoLabel();
        void calibApply();
        void calibFillMax();
        void calibResetToFilter();

        Histo1DWidgetPrivate *m_d;
        friend struct Histo1DWidgetPrivate;

        Histo1D *m_histo;
        Histo1DPtr m_histoPtr;
        //QwtPlotCurve *m_plotCurve;
        QwtPlotHistogram *m_plotHisto;

        ScrollZoomer *m_zoomer;
        QTimer *m_replotTimer;
        Histo1DStatistics m_stats;
        QwtPlotTextLabel *m_statsTextItem;
        QwtText *m_statsText;
        QPointF m_cursorPosition;

        std::shared_ptr<analysis::CalibrationMinMax> m_calib;
        s32 m_histoAddress;
        MVMEContext *m_context;

        SinkPtr m_sink;
        HistoSinkCallback m_sinkModifiedCallback;

};

class Histo1DListWidget: public QWidget
{
    Q_OBJECT
    public:
        using HistoList = QVector<std::shared_ptr<Histo1D>>;
        using SinkPtr = Histo1DWidget::SinkPtr;
        using HistoSinkCallback = Histo1DWidget::HistoSinkCallback;

        Histo1DListWidget(const HistoList &histos, QWidget *parent = 0);

        HistoList getHistograms() const { return m_histos; }

        void setContext(MVMEContext *context) { m_context = context; }
        void setCalibration(const std::shared_ptr<analysis::CalibrationMinMax> &calib);
        void setSink(const SinkPtr &sink, HistoSinkCallback sinkModifiedCallback);

        void selectHistogram(int histoIndex);

    private:
        void onHistoSpinBoxValueChanged(int index);

        HistoList m_histos;
        Histo1DWidget *m_histoWidget;
        QSpinBox *m_histoSpin;
        s32 m_currentIndex = 0;
        std::shared_ptr<analysis::CalibrationMinMax> m_calib;
        MVMEContext *m_context = nullptr;

        SinkPtr m_sink;
        HistoSinkCallback m_sinkModifiedCallback;
};

#endif /* __HISTO1D_WIDGET_H__ */
