#ifndef TWODIMWIDGET_H
#define TWODIMWIDGET_H

#include <QWidget>

class ScrollZoomer;
class QwtPlotTextLabel;
class QwtText;
class Histogram;
class QwtPlotCurve;
class MVMEContext;

namespace Ui {
class TwoDimWidget;
}

class TwoDimWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TwoDimWidget(MVMEContext *context, Histogram *histo, QWidget *parent = 0);
    ~TwoDimWidget();

    void setZoombase();
    quint32 getSelectedChannelIndex() const;
    void setSelectedChannelIndex(quint32 channelIndex);
    void exportPlot();

    void plot();
    void setHistogram(Histogram* h);
    Histogram *getHistogram() const { return m_hist; }
    void clearDisp(void);
    void updateStatistics();

public slots:
    void displayChanged(void);
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

    MVMEContext *m_context;
    QwtPlotCurve *m_curve;
    ScrollZoomer *m_plotZoomer;

    Histogram* m_hist;
    quint32 m_currentModule;
    quint32 m_currentChannel;

    QwtPlotTextLabel *m_statsTextItem;
    QwtText *m_statsText;
};

#endif // TWODIMWIDGET_H
