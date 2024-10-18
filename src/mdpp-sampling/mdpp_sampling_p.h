#ifndef B775D3EE_03C4_46EA_96F8_DF6723990728
#define B775D3EE_03C4_46EA_96F8_DF6723990728

#include "mdpp_sampling.h"

#include <QDebug>

#include "mvme_qwt.h"

namespace mesytec::mvme::mdpp_sampling
{

enum class PlotDataMode
{
    Raw,
    Interpolated
};

// InterpolatedSamples takes precendence. if empty raw samples are plotted.
struct MdppChannelTracePlotData: public QwtSeriesData<QPointF>
{
    const ChannelTrace *trace_ = nullptr;
    PlotDataMode mode_ = PlotDataMode::Raw;
    mutable QRectF boundingRectCache_;

    // Set the event data to plot
    void setTrace(const ChannelTrace *trace)
    {
         trace_ = trace;
         boundingRectCache_ = {};
    }

    const ChannelTrace *getTrace() const { return trace_; }

    void setMode(const PlotDataMode mode)
    {
        mode_ = mode;
        boundingRectCache_ = {};
    }

    PlotDataMode getMode() const { return mode_; }

    QRectF boundingRect() const override
    {
        if (!trace_ || (trace_->samples.empty() && trace_->interpolated.empty()))
            return {};

        if (!boundingRectCache_.isValid())
            boundingRectCache_ = calculateBoundingRect();

        return boundingRectCache_;
    }

    QRectF calculateBoundingRect() const
    {
        if (mode_ == PlotDataMode::Raw)
        {
            auto &samples = trace_->samples;
            auto minMax = std::minmax_element(std::begin(samples), std::end(samples));

            if (minMax.first != std::end(samples) && minMax.second != std::end(samples))
            {
                QPointF topLeft(0, *minMax.second);
                QPointF bottomRight(samples.size() * trace_->dtSample, *minMax.first);
                return QRectF(topLeft, bottomRight);
            }
        }
        else if (mode_ == PlotDataMode::Interpolated)
        {
            auto &samples = trace_->interpolated;
            auto minMax = std::minmax_element(std::begin(samples), std::end(samples),
                            [](const auto &a, const auto &b) { return a.second < b.second; });

            if (minMax.first != std::end(samples) && minMax.second != std::end(samples))
            {
                double minY = minMax.first->second;
                double maxY = minMax.second->second;
                double maxX = samples.back().first; // last sample must contain the largest x coordinate

                QPointF topLeft(0, maxY);
                QPointF bottomRight(maxX, minY);
                return QRectF(topLeft, bottomRight);
            }
        }

        return {};
    }

    size_t size() const override
    {
        if (trace_)
        {
            if (mode_ == PlotDataMode::Raw)
                return trace_->samples.size();
            else if (mode_ == PlotDataMode::Interpolated)
                return trace_->interpolated.size();
        }
        return 0;
    }

    QPointF sample(size_t i) const override
    {
        if (trace_)
        {
            if (mode_ == PlotDataMode::Raw && i < static_cast<size_t>(trace_->samples.size()))
                return QPointF(i * trace_->dtSample, trace_->samples[i]);
            else if (i < static_cast<size_t>(trace_->interpolated.size()))
                return QPointF(trace_->interpolated[i].first, trace_->interpolated[i].second);
        }

        return {};
    }
};

}

#endif /* B775D3EE_03C4_46EA_96F8_DF6723990728 */
