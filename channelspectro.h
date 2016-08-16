#ifndef UUID_bf5f9bfd_f2f3_4736_bf7e_f2a96176abe9
#define UUID_bf5f9bfd_f2f3_4736_bf7e_f2a96176abe9

#include <QWidget>

namespace Ui
{
class ChannelSpectroWidget;
}

class QTimer;
class QwtPlotSpectrogram;
class QwtLinearColorMap;
class ChannelSpectroData;
class ScrollZoomer;

class ChannelSpectro: public QObject
{
    Q_OBJECT
public:
    ChannelSpectro(uint32_t xResolution = 1024, uint32_t yResolution = 1024, QObject *parent = 0);
    void setXAxisChannel(int32_t channel);
    void setYAxisChannel(int32_t channel);

    int32_t getXAxisChannel() const { return m_xAxisChannel; }
    int32_t getYAxisChannel() const { return m_yAxisChannel; }
    QwtPlotSpectrogram *getPlotItem() const { return m_plotItem; }
    ChannelSpectroData *getSpectroData() const { return m_data; }
    void clear();

    uint32_t xAxisResolution() const;
    uint32_t yAxisResolution() const;

    void fill(uint32_t x, uint32_t y);

    QwtLinearColorMap *getColorMap() const;

private:
    QwtPlotSpectrogram *m_plotItem;
    ChannelSpectroData *m_data;

    int32_t m_xAxisChannel;
    int32_t m_yAxisChannel;
};


class ChannelSpectroWidget: public QWidget
{
    Q_OBJECT

public:
    explicit ChannelSpectroWidget(ChannelSpectro *channelSpectro, QWidget *parent=0);
    ~ChannelSpectroWidget();

    ChannelSpectro *getHist2D() const { return m_channelSpectro; }

public slots:
    void replot();
    void exportPlot();

private slots:
    void setXAxisChannel(int channel);
    void setYAxisChannel(int channel);

    void addTestData();

private:
    Ui::ChannelSpectroWidget *ui;
    ChannelSpectro *m_channelSpectro;
    ScrollZoomer *m_zoomer;
    QTimer *m_replotTimer;
};

#endif
