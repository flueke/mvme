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

class ChannelSpectro
{
public:
    ChannelSpectro(uint32_t xResolution = 8192, uint32_t yResolution = 8192);
    void setXAxisChannel(int32_t channel);
    void setYAxisChannel(int32_t channel);

    int32_t getXAxisChannel() const { return m_xAxisChannel; }
    int32_t getYAxisChannel() const { return m_yAxisChannel; }
    void setValue(uint32_t channel, uint32_t value);
    QwtPlotSpectrogram *getPlotItem() const { return m_plotItem; }
    ChannelSpectroData *getSpectroData() const { return m_data; }
    void clear();

    QwtLinearColorMap *getColorMap() const;

private:
    QwtPlotSpectrogram *m_plotItem;
    ChannelSpectroData *m_data;

    int32_t m_xAxisChannel;
    int32_t m_yAxisChannel;

    int64_t m_xValue;
    int64_t m_yValue;
};


class ChannelSpectroWidget: public QWidget
{
    Q_OBJECT

public:
    explicit ChannelSpectroWidget(ChannelSpectro *channelSpectro, QWidget *parent=0);
    ~ChannelSpectroWidget();


public slots:
    void replot();

private slots:
    void setXAxisChannel(int channel);
    void setYAxisChannel(int channel);

    void addRandomValues();

private:
    Ui::ChannelSpectroWidget *ui;
    ChannelSpectro *m_channelSpectro;
    ScrollZoomer *m_zoomer;
    QTimer *m_replotTimer;
};

#endif
