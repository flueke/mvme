#ifndef __HISTO1D_WIDGET_H__
#define __HISTO1D_WIDGET_H__

#include <QWidget>

class Histo1D;
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

        QPointF m_cursorPosition;
#ifdef ENABLE_CALIB_UI
        CalibUi *m_calibUi;
#endif
};

#if 0 
class Histo1DListWidget: public MVMEWidget
{
    Q_OBJECT
    public:
        Hist1DListWidget(MVMEContext *context, QList<Histo1D *> histos, QWidget *parent = 0);

        QList<Histo1D *> getHistograms() const { return m_histos; }

    private:
        void onHistSpinBoxValueChanged(int index);
        void onObjectAboutToBeRemoved(QObject *obj);

        MVMEContext *m_context;
        QList<Histo1D *> m_histos;
        Hist1DWidget *m_histoWidget;
        int m_currentIndex = 0;
};
#endif

#endif /* __HISTO1D_WIDGET_H__ */
