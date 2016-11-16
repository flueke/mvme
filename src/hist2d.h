#ifndef UUID_bf5f9bfd_f2f3_4736_bf7e_f2a96176abe9
#define UUID_bf5f9bfd_f2f3_4736_bf7e_f2a96176abe9

#include "util.h"
#include <qwt_interval.h>

namespace Ui
{
class Hist2DWidget;
}

class QTimer;
class QwtPlotSpectrogram;
class QwtLinearColorMap;
class ScrollZoomer;
class MVMEContext;

struct Hist2DStatistics
{
    u32 maxX = 0;
    u32 maxY = 0;
    u32 maxValue = 0;
    s64 entryCount = 0;
};

class Hist2D: public QObject
{
    Q_OBJECT
signals:
    void resized(u32 xBits, u32 yBits);

public:
    Hist2D(uint32_t xBits, uint32_t yBits, QObject *parent = 0);
    ~Hist2D();

    void resize(uint32_t xBits, uint32_t yBits);

    uint32_t getXResolution() const { return 1 << m_xBits; }
    uint32_t getYResolution() const { return 1 << m_yBits; }

    uint32_t getXBits() const { return m_xBits; }
    uint32_t getYBits() const { return m_yBits; }

    double value(double x, double y) const;
    void fill(uint32_t x, uint32_t y, uint32_t weight=1);
    void clear();

    const QwtInterval &interval(Qt::Axis axis) const
    {
        return m_intervals[axis];
    }

    Hist2DStatistics calcStatistics(QwtInterval xInterval, QwtInterval yInterval) const;

private:
    void setInterval(Qt::Axis axis, const QwtInterval &interval);

    uint32_t m_xBits = 0;
    uint32_t m_yBits = 0;
    uint32_t *m_data = nullptr;
    Hist2DStatistics m_stats;
    QwtInterval m_intervals[3];
};

class Hist2DWidget: public MVMEWidget
{
    Q_OBJECT

public:
    explicit Hist2DWidget(MVMEContext *context, Hist2D *hist2d, QWidget *parent=0);
    ~Hist2DWidget();

    Hist2D *getHist2D() const { return m_hist2d; }

private slots:
    void replot();
    void exportPlot();
    void mouseCursorMovedToPlotCoord(QPointF);
    void mouseCursorLeftPlot();
    void displayChanged();

private:
    bool zAxisIsLog() const;
    bool zAxisIsLin() const;
    QwtLinearColorMap *getColorMap() const;
    void onHistoResized();

    Ui::Hist2DWidget *ui;
    MVMEContext *m_context;
    Hist2D *m_hist2d;
    QwtPlotSpectrogram *m_plotItem;
    ScrollZoomer *m_zoomer;
    QTimer *m_replotTimer;
};

#endif
