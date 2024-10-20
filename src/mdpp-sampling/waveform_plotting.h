#ifndef F66C9539_DA00_4A40_802A_F2101420A636
#define F66C9539_DA00_4A40_802A_F2101420A636

#include <mesytec-mvlc/cpp_compat.h>

#include "histo_ui.h"
#include "mvme_qwt.h"
#include "mdpp-sampling/waveform_interpolation.h"

namespace mesytec::mvme::waveforms
{

// Does not take ownership of any of the underlying data containers!
// Only works for increasing and non-negative x values for now.

// It must behave like an iterator over Sample instances.
template<typename It>
QRectF calculate_bounding_rect(const It &begin, const It &end)
{
    auto minMax = std::minmax_element(begin, end,
        [](const auto &a, const auto &b) { return a.second < b.second; });

    if (begin < end && minMax.first != end && minMax.second != end)
    {
        double minY = minMax.first->second;
        double maxY = minMax.second->second;
        double maxX = (end - 1)->first; // last sample must contain the largest x coordinate

        QPointF topLeft(0, maxY);
        QPointF bottomRight(maxX, minY);
        return QRectF(topLeft, bottomRight);
    }

    return {};
}

template<typename Waveform>
QRectF calculate_bounding_rect(const Waveform &waveform)
{
    return calculate_bounding_rect(std::begin(waveform), std::end(waveform));
}

class WaveformPlotData: public QwtSeriesData<QPointF>
{
    public:
        // Works for containers storing Samples in a contiguous memory block,
        // e.g. std::array, std::vector, etc.
        // No data is copied, the original data is referenced!
        template<typename SamplesContainer>
        void setData(const SamplesContainer &samples)
        {
            samples_ = {samples.data(), samples.size()};
            boundingRectCache_ = {};
        }

        const mvlc::util::span<const Sample> &getData() const
        {
            return samples_;
        }

        template<typename Container>
        Container copyData() const
        {
            return Container(samples_.begin(), samples_.end());
        }

        QRectF boundingRect() const override
        {
            if (!boundingRectCache_.isValid())
                boundingRectCache_ = calculateBoundingRect();

            return boundingRectCache_;
        }

        size_t size() const override
        {
            return samples_.size();
        }

        QPointF sample(size_t i) const override
        {
            if (i < samples_.size())
                return QPointF(samples_[i].first, samples_[i].second);

            return {};
        }

        QRectF calculateBoundingRect() const
        {
            return calculate_bounding_rect(std::begin(samples_), std::end(samples_));
        }

    private:
        mvlc::util::span<const Sample> samples_;
        mutable QRectF boundingRectCache_;
};

struct WaveformCurves
{
    std::unique_ptr<QwtPlotCurve> rawCurve;
    std::unique_ptr<QwtPlotCurve> interpolatedCurve;
};

class WaveformPlotWidget: public histo_ui::PlotWidget
{
    Q_OBJECT
    public:
        using Handle = size_t;

        WaveformPlotWidget(QWidget *parent = nullptr);
        ~WaveformPlotWidget() override;

        Handle addWaveform(WaveformCurves &&data);
        WaveformCurves takeWaveform(Handle handle);
        QwtPlotCurve *getRawCurve(Handle handle);
        QwtPlotCurve *getInterpolatedCurve(Handle handle);

    public slots:
        void replot() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif /* F66C9539_DA00_4A40_802A_F2101420A636 */
