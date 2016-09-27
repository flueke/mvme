#ifndef UUID_bf5f9bfd_f2f3_4736_bf7e_f2a96176abe9
#define UUID_bf5f9bfd_f2f3_4736_bf7e_f2a96176abe9

#include <QWidget>
#include <qwt_raster_data.h>

namespace Ui
{
class Hist2DWidget;
}

class QTimer;
class QwtPlotSpectrogram;
class QwtLinearColorMap;
class Hist2DRasterData;
class ScrollZoomer;
class MVMEContext;

class Hist2D: public QObject
{
    Q_OBJECT
public:
    Hist2D(uint32_t xBits, uint32_t yBits, QObject *parent = 0);

    uint32_t getXResolution() const { return 1 << m_xBits; }
    uint32_t getYResolution() const { return 1 << m_yBits; }

    uint32_t getXBits() const { return m_xBits; }
    uint32_t getYBits() const { return m_yBits; }

    double value(double x, double y) const;
    void fill(uint32_t x, uint32_t y, uint32_t weight=1);
    void clear();

    Hist2DRasterData *makeRasterData();
    QwtLinearColorMap *getColorMap() const;

    const QwtInterval &interval(Qt::Axis axis) const
    {
        return m_intervals[axis];
    }

    uint32_t getMaxValue() const { return m_maxValue; }
    uint32_t getMaxX() const { return m_maxX; }
    uint32_t getMaxY() const { return m_maxY; }
    uint32_t getNumberOfEntries() const { return m_numberOfEntries; }

    int getXEventIndex() const;
    int getXModuleIndex() const;
    int getXAddressValue() const;

    int getYEventIndex() const;
    int getYModuleIndex() const;
    int getYAddressValue() const;


private:
    void setInterval(Qt::Axis axis, const QwtInterval &interval);

    uint32_t m_xBits;
    uint32_t m_yBits;
    uint32_t *m_data = 0;
    uint32_t m_maxValue = 0;
    uint32_t m_maxX = 0;
    uint32_t m_maxY = 0;
    uint32_t m_numberOfEntries = 0;
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
    explicit Hist2DWidget(MVMEContext *context, Hist2D *hist2d, QWidget *parent=0);
    ~Hist2DWidget();

    Hist2D *getHist2D() const { return m_hist2d; }

public slots:
    void replot();
    void exportPlot();

private slots:
    void addTestData();

private:
    Ui::Hist2DWidget *ui;
    MVMEContext *m_context;
    Hist2D *m_hist2d;
    QwtPlotSpectrogram *m_plotItem;
    ScrollZoomer *m_zoomer;
    QTimer *m_replotTimer;
};

#endif
