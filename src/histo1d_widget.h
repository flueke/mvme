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

namespace Ui
{
    class Histo1DWidget;
}

class Histo1DWidget: public QWidget
{
    Q_OBJECT
    public:
        Histo1DWidget(Histo1D *histo, QWidget *parent = 0);
        ~Histo1DWidget();

        void setHistogram(Histo1D *histo);

        virtual bool eventFilter(QObject *watched, QEvent *event) override;

        friend class Histo1DListWidget;

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
        QwtPlotCurve *m_plotCurve;

        ScrollZoomer *m_zoomer;
        QTimer *m_replotTimer;
        Histo1DStatistics m_stats;
        QwtPlotTextLabel *m_statsTextItem;
        QwtText *m_statsText;
        QPointF m_cursorPosition;
#ifdef ENABLE_CALIB_UI
        CalibUi *m_calibUi;
#endif
};

class Histo1DListWidget: public QWidget
{
    Q_OBJECT
    public:
        using HistoList = QVector<std::shared_ptr<Histo1D>>;

        Histo1DListWidget(const HistoList &histos, QWidget *parent = 0);

        HistoList getHistograms() const { return m_histos; }

    private:
        void onHistSpinBoxValueChanged(int index);

        HistoList m_histos;
        Histo1DWidget *m_histoWidget;
        int m_currentIndex = 0;
};

#endif /* __HISTO1D_WIDGET_H__ */
