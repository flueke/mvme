#ifndef __HISTO1D_WIDGET_H__
#define __HISTO1D_WIDGET_H__

#include "histo1d.h"
#include <QWidget>

class QTimer;
class QTextStream;
class QwtPlotCurve;
class QwtPlotHistogram;
class QwtPlotTextLabel;
class QwtText;
class ScrollZoomer;
class CalibUi;
class MVMEContext;

namespace Ui
{
    class Histo1DWidget;
}

namespace analysis
{
    class CalibrationMinMax;
}

class Histo1DWidget: public QWidget
{
    Q_OBJECT
    public:
        // Convenience constructor that enables the widget to keep the histo alive.
        Histo1DWidget(const Histo1DPtr &histo, QWidget *parent = 0);
        Histo1DWidget(Histo1D *histo, QWidget *parent = 0);
        ~Histo1DWidget();

        void setHistogram(Histo1D *histo);

        virtual bool eventFilter(QObject *watched, QEvent *event) override;

        friend class Histo1DListWidget;

        void setCalibrationInfo(const std::shared_ptr<analysis::CalibrationMinMax> &calib, s32 histoAddress, MVMEContext *context);

    private slots:
        void replot();
        void exportPlot();
        void saveHistogram();
        void zoomerZoomed(const QRectF &);
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();
        void updateStatistics();
        void displayChanged();

    private:
        void updateAxisScales();
        bool yAxisIsLog();
        bool yAxisIsLin();
        void updateCursorInfoLabel();
        void calibApply();
        void calibFillMax();
        void calibResetToFilter();

        Ui::Histo1DWidget *ui;

        Histo1D *m_histo;
        Histo1DPtr m_histoPtr;
        QwtPlotCurve *m_plotCurve;

        ScrollZoomer *m_zoomer;
        QTimer *m_replotTimer;
        Histo1DStatistics m_stats;
        QwtPlotTextLabel *m_statsTextItem;
        QwtText *m_statsText;
        QPointF m_cursorPosition;

        CalibUi *m_calibUi;
        std::shared_ptr<analysis::CalibrationMinMax> m_calib;
        s32 m_histoAddress;
        MVMEContext *m_context;
};

class Histo1DListWidget: public QWidget
{
    Q_OBJECT
    public:
        using HistoList = QVector<std::shared_ptr<Histo1D>>;

        Histo1DListWidget(const HistoList &histos, QWidget *parent = 0);

        HistoList getHistograms() const { return m_histos; }

        // XXX: MVMEContext is passed so that the analysis can be paused from within the widget.
        void setCalibration(const std::shared_ptr<analysis::CalibrationMinMax> &calib, MVMEContext *context);

    private:
        void onHistoSpinBoxValueChanged(int index);

        HistoList m_histos;
        Histo1DWidget *m_histoWidget;
        s32 m_currentIndex = 0;
        std::shared_ptr<analysis::CalibrationMinMax> m_calib;
        MVMEContext *m_context;
};

#endif /* __HISTO1D_WIDGET_H__ */
