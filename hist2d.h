#ifndef UUID_bf5f9bfd_f2f3_4736_bf7e_f2a96176abe9
#define UUID_bf5f9bfd_f2f3_4736_bf7e_f2a96176abe9

#include <QWidget>

namespace Ui
{
class Hist2DWidget;
}

class QTimer;
class QwtPlotSpectrogram;
class QwtLinearColorMap;
class Hist2DData;
class ScrollZoomer;

class Hist2D: public QObject
{
    Q_OBJECT
public:
    Hist2D(uint32_t xResolution = 1024, uint32_t yResolution = 1024, QObject *parent = 0);
    void setXAxisChannel(int32_t channel);
    void setYAxisChannel(int32_t channel);

    int32_t getXAxisChannel() const { return m_xAxisChannel; }
    int32_t getYAxisChannel() const { return m_yAxisChannel; }
    Hist2DData *getData() const { return m_data; }
    void clear();

    uint32_t xAxisResolution() const;
    uint32_t yAxisResolution() const;

    void fill(uint32_t x, uint32_t y);

    QwtLinearColorMap *getColorMap() const;

private:
    Hist2DData *m_data;
    int32_t m_xAxisChannel;
    int32_t m_yAxisChannel;
};


class Hist2DWidget: public QWidget
{
    Q_OBJECT

public:
    explicit Hist2DWidget(Hist2D *channelSpectro, QWidget *parent=0);
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
