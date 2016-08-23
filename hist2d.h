#ifndef UUID_bf5f9bfd_f2f3_4736_bf7e_f2a96176abe9
#define UUID_bf5f9bfd_f2f3_4736_bf7e_f2a96176abe9

#include <QWidget>
#include <qwt_raster_data.h>
//#include <qwt_interval.h>

namespace Ui
{
class Hist2DWidget;
}

class QTimer;
class QwtPlotSpectrogram;
class QwtLinearColorMap;
class Hist2DRasterData;
class ScrollZoomer;

class Hist2D: public QObject
{
    Q_OBJECT
public:
    Hist2D(uint32_t xResolution, uint32_t yResolution, QObject *parent = 0);
    void setXAxisChannel(int32_t channel);
    void setYAxisChannel(int32_t channel);

    int32_t getXAxisChannel() const { return m_xAxisChannel; }
    int32_t getYAxisChannel() const { return m_yAxisChannel; }

    uint32_t xAxisResolution() const { return m_xResolution; }
    uint32_t yAxisResolution() const { return m_yResolution; }

    double value(double x, double y) const;
    void fill(uint32_t x, uint32_t y, uint32_t weight=1);
    void clear();

    Hist2DRasterData *makeRasterData();
    QwtLinearColorMap *getColorMap() const;

    const QwtInterval &interval(Qt::Axis axis) const
    {
        return m_intervals[axis];
    }

private:
    void setInterval(Qt::Axis axis, const QwtInterval &interval);

    uint32_t m_xResolution;
    uint32_t m_yResolution;
    uint32_t *m_data;
    uint32_t m_maxValue;
    int32_t m_xAxisChannel;
    int32_t m_yAxisChannel;
    QwtInterval m_intervals[3];
};

class Hist2DRasterData: public QwtRasterData
{
public:

    Hist2DRasterData(Hist2D *hist2d)
        : m_hist2d(hist2d)
    {
        updateIntervals();
    }

    virtual double value(double x, double y) const
    {
        return m_hist2d->value(x, y);
    }

    void updateIntervals()
    {
        for (int axis=0; axis<3; ++axis)
        {
            setInterval(static_cast<Qt::Axis>(axis), m_hist2d->interval(static_cast<Qt::Axis>(axis)));
        }
    }

private:
    Hist2D *m_hist2d;
};

class Hist2DWidget: public QWidget
{
    Q_OBJECT

public:
    explicit Hist2DWidget(Hist2D *hist2d, QWidget *parent=0);
    ~Hist2DWidget();

    Hist2D *getHist2D() const { return m_hist2d; }

public slots:
    void replot();
    void exportPlot();

private slots:
    void setXAxisChannel(int channel);
    void setYAxisChannel(int channel);

    void addTestData();

private:
    Ui::Hist2DWidget *ui;
    Hist2D *m_hist2d;
    QwtPlotSpectrogram *m_plotItem;
    ScrollZoomer *m_zoomer;
    QTimer *m_replotTimer;
};

#endif
