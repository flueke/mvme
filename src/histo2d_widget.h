#ifndef __HISTO2D_WIDGET_H__
#define __HISTO2D_WIDGET_H__

#include "histo2d.h"
#include <QWidget>

class QTimer;
class QwtPlotSpectrogram;
class QwtLinearColorMap;
class QwtPlotHistogram;
class ScrollZoomer;

namespace Ui
{
    class Histo2DWidget;
}

class Histo2DWidget: public QWidget
{
    Q_OBJECT
    public:
        Histo2DWidget(const Histo2DPtr histoPtr, QWidget *parent = 0);
        Histo2DWidget(Histo2D *histo, QWidget *parent = 0);
        ~Histo2DWidget();

    private slots:
        void replot();
        void exportPlot();
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();
        void displayChanged();
        void zoomerZoomed(const QRectF &);
        void on_pb_subHisto_clicked();
        void on_tb_info_clicked();

    private:
        bool zAxisIsLog() const;
        bool zAxisIsLin() const;
        QwtLinearColorMap *getColorMap() const;
        void onHistoResized();
        void updateCursorInfoLabel();

        Ui::Histo2DWidget *ui;
        Histo2D *m_histo;
        Histo2DPtr m_histoPtr;
        QwtPlotSpectrogram *m_plotItem;
        ScrollZoomer *m_zoomer;
        QTimer *m_replotTimer;
        QPointF m_cursorPosition;
};

#endif /* __HISTO2D_WIDGET_H__ */
