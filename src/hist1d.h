#ifndef __HIST1D_H__
#define __HIST1D_H__

#include "util.h"

class QTimer;
class QTextStream;
class QwtPlotCurve;
class QwtPlotHistogram;
class QwtPlotTextLabel;
class QwtText;
class ScrollZoomer;

struct Hist1DStatistics
{
    u32 maxChannel = 0;
    u32 maxValue = 0;
    double mean = 0.0;
    double sigma = 0.0;
    u64 entryCount = 0;
};

class Hist1D: public QObject
{
    Q_OBJECT
    public:
        Hist1D(u32 bits, QObject *parent = 0);
        ~Hist1D();

        inline u32 getResolution() const { return 1 << m_bits; }
        inline u32 getBits() const { return m_bits; }

        double value(u32 x) const;
        void fill(u32 x, u32 weight=1);
        void clear();

        inline u64 getEntryCount() const { return m_count; }
        inline u32 getMaxValue() const { return m_maxValue; }
        inline u32 getMaxChannel() const { return m_maxChannel; }

        Hist1DStatistics calcStatistics(u32 startChannel, u32 onePastEndChannel) const;

    private:
        u32 m_bits;
        u32 *m_data = nullptr;
        u64 m_count = 0;
        u32 m_maxValue = 0;
        u32 m_maxChannel = 0;
};

QTextStream &writeHistogram(QTextStream &out, Hist1D *histo);
Hist1D *readHistogram(QTextStream &in);

namespace Ui
{
    class Hist1DWidget;
}

class Hist1DConfig;
class MVMEContext;

class Hist1DWidget: public MVMEWidget
{
    Q_OBJECT
    public:
        Hist1DWidget(MVMEContext *context, Hist1D *histo, QWidget *parent = 0);
        Hist1DWidget(MVMEContext *context, Hist1D *histo, Hist1DConfig *histoConfig, QWidget *parent = 0);
        ~Hist1DWidget();

        Hist1D *getHist1D() const { return m_histo; }
        Hist1DConfig *getHist1DConfig() const { return m_histoConfig; }

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

        Ui::Hist1DWidget *ui;
        MVMEContext *m_context;
        Hist1D *m_histo;
        Hist1DConfig *m_histoConfig;
        //QwtPlotHistogram *m_plotHisto;
        QwtPlotCurve *m_plotCurve;
        ScrollZoomer *m_zoomer;
        QTimer *m_replotTimer;
        Hist1DStatistics m_stats;
        QwtPlotTextLabel *m_statsTextItem;
        QwtText *m_statsText;
};

#endif /* __HIST1D_H__ */
