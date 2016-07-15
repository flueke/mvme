#ifndef TWODIMWIDGET_H
#define TWODIMWIDGET_H

#include <QWidget>

class ScrollZoomer;
class QwtPlotTextLabel;
class QwtText;
class mvme;
class Histogram;
class QwtPlotCurve;

namespace Ui {
class TwoDimWidget;
}

class TwoDimWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TwoDimWidget(mvme *context, Histogram *histo, QWidget *parent = 0);
    ~TwoDimWidget();

    void setZoombase();
    quint32 getSelectedChannelIndex() const;
    void setSelectedChannelIndex(quint32 channelIndex);
    void exportPlot();

    void plot();
    void setMvme(mvme* m);
    void setHistogram(Histogram* h);
    void clearDisp(void);
    void updateStatistics();

public slots:
    void displaychanged(void);
    void clearHist(void);

signals:
    void modChanged(qint16 mod);

private slots:
    void zoomerZoomed(QRectF);
    void mouseCursorMovedToPlotCoord(QPointF);

private:
    void updateYAxisScale();
    bool yAxisIsLog();
    bool yAxisIsLin();


    Ui::TwoDimWidget *ui;

    QwtPlotCurve *m_curve;
    ScrollZoomer *m_plotZoomer;

    Histogram* m_pMyHist;
    quint32 m_currentModule;
    quint32 m_currentChannel;
    mvme* m_pMyMvme;

    QwtPlotTextLabel *m_statsTextItem;
    QwtText *m_statsText;
};

#endif // TWODIMWIDGET_H
