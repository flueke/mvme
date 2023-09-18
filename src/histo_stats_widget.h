#ifndef __MVME2_SRC_HISTO_STATS_WIDGET_H_
#define __MVME2_SRC_HISTO_STATS_WIDGET_H_

#include <QWidget>
#include <qwt_scale_div.h>
#include "analysis/analysis.h"

class HistoStatsWidget: public QWidget
{
    Q_OBJECT

    public:
        using SinkPtr = std::shared_ptr<analysis::Histo1DSink>;

        HistoStatsWidget(QWidget *parent = nullptr);
        ~HistoStatsWidget() override;

    public slots:
        void addSink(const SinkPtr &sink);
        void addHistograms(const Histo1DList &histos);
        void addHistogram(const std::shared_ptr<Histo1D> &histo);
        void setXScaleDiv(const QwtScaleDiv &scaleDiv);
        void setEffectiveResolution(s32 binCount);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif // __MVME2_SRC_HISTO_STATS_WIDGET_H_
