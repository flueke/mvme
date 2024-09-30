#ifndef B775D3EE_03C4_46EA_96F8_DF6723990728
#define B775D3EE_03C4_46EA_96F8_DF6723990728

#include "mdpp_sampling.h"

#include "mvme_qwt.h"

#include <QDebug>

namespace mesytec::mvme
{

using TraceBuffer = QList<ChannelTrace>;
using ModuleTraceHistory = std::vector<TraceBuffer>; // indexed by the traces channel number
using TraceHistoryMap = QMap<QUuid, ModuleTraceHistory>;

struct MdppChannelTracePlotData: public QwtSeriesData<QPointF>
{
    const ChannelTrace *trace_ = nullptr;
    mutable QRectF boundingRectCache_;

    // Set the event data to plot
    void setTrace(const ChannelTrace *trace)
    {
         trace_ = trace;
         boundingRectCache_ = {};
    }

    const ChannelTrace *getTrace() const { return trace_; }

    QRectF boundingRect() const override
    {
        if (!trace_ || trace_->samples.empty())
            return {};

        if (!boundingRectCache_.isValid())
        {
            auto &samples = trace_->samples;
            auto minMax = std::minmax_element(std::begin(samples), std::end(samples));

            if (minMax.first != std::end(samples) && minMax.second != std::end(samples))
            {
                QPointF topLeft(0, *minMax.second);
                QPointF bottomRight(samples.size(), *minMax.first);
                boundingRectCache_ = QRectF(topLeft, bottomRight);
            }
        }

        return boundingRectCache_;
    }

    size_t size() const override
    {
        return trace_ ? trace_->samples.size() : 0;
    }

    QPointF sample(size_t i) const override
    {
        if (trace_ && i < static_cast<size_t>(trace_->samples.size()))
            return QPointF(i, trace_->samples[i]);

        return {};
    }
};

}

#endif /* B775D3EE_03C4_46EA_96F8_DF6723990728 */
