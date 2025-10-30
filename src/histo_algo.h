#ifndef E23CBE69_DF0C_4A71_AF2C_3D8137587C23
#define E23CBE69_DF0C_4A71_AF2C_3D8137587C23

// Histogram addition and maybe more in the future.
// Not in histo_util.h like the reset because of a circular dependency between
// h1d/h2d and histo_util.h

#include <optional>
#include <mesytec-mvlc/util/stopwatch.h>

#include "histo1d.h"
#include "histo2d.h"

namespace mesytec::mvme
{

enum class HistoOpsBinningMode
{
    MinimumBins, // the resulting histogram has the smallest number of bins of the input histos
    MaximumBins  // the resulting histogram has the largest number of bins of the input histos
};

std::string to_string(const HistoOpsBinningMode &mode);
std::optional<HistoOpsBinningMode> histo_ops_binning_mode_from_string(const std::string &mode);

// Calculates the binning for the resulting sum histogram.
// begin and end must designate a valid range over pointers to Histo1D instances.
template <typename It>
AxisBinning
calculate_addition_dest_binning(It begin, It end,
                                const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    if (begin == end)
    {
        return AxisBinning(); // empty binning
    }

    AxisBinning result((*begin)->getNumberOfBins(), (*begin)->getXMin(), (*begin)->getXMax());

    return std::accumulate(
        begin, end, result,
        [bm](AxisBinning &result, const auto &histo)
        {
            switch (bm)
            {
            case HistoOpsBinningMode::MinimumBins:
                result.setBins(std::min(result.getBins(), histo->getNumberOfBins()));
                break;
            case HistoOpsBinningMode::MaximumBins:
                result.setBins(std::max(result.getBins(), histo->getNumberOfBins()));
                break;
            }
            result.setMin(std::min(result.getMin(), histo->getXMin()));
            result.setMax(std::max(result.getMax(), histo->getXMax()));
            return result;
        });
}

inline AxisBinning
calculate_addition_dest_binning(const std::initializer_list<const Histo1D *> &histos,
                                const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    return calculate_addition_dest_binning(std::begin(histos), std::end(histos), bm);
}

inline AxisBinning
calculate_addition_dest_binning(const Histo1D &a, const Histo1D &b,
                                const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    return calculate_addition_dest_binning({&a, &b}, bm);
}

inline AxisBinning
calculate_addition_dest_binning(const Histo1DList &histos,
                                const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    return calculate_addition_dest_binning(std::begin(histos), std::end(histos), bm);
}

inline AxisBinning
calculate_addition_dest_binning(mesytec::mvlc::util::span<const Histo1D *> histos,
                                const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    return calculate_addition_dest_binning(std::begin(histos), std::end(histos), bm);
}

// Add the histograms a and b. The number of bins in the resulting histogram
// depends on the binning mode. Sampling the source histograms makes use of bin
// subsampling/multi bin sampling if needed. This is different from ROOT TH1 and
// boost::histogram where the binnings of the operands have to match exactly.

template <typename It>
void add(Histo1DPtr &dest, It begin, It end, const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    if (begin == end)
        return;

    auto binning = calculate_addition_dest_binning(begin, end, bm);
    auto nBins = binning.getBins();

    if (dest->getAxisBinning(Qt::XAxis) != binning)
    {
        dest->setAxisBinning(Qt::XAxis, binning);
        dest->resize(binning.getBins());
    }

    mesytec::mvlc::util::Stopwatch swTotal;

    for (auto it = begin; it != end; ++it)
    {
        mesytec::mvlc::util::Stopwatch swHisto;
        const auto &histo = *it;
        auto idx = std::distance(begin, it);
        assert(histo);
        for (size_t destbin = 0; destbin < nBins; ++destbin)
        {
            double xLow = dest->getBinLowEdge(destbin);
            double xHigh = xLow + dest->getBinWidth();
            auto newCounts = dest->getBinContent(destbin) + histo->getCounts(xLow, xHigh);
            dest->setBinContent(destbin, newCounts, newCounts);
        }

        auto histoElapsed = swHisto.get_elapsed();
        spdlog::debug("add(Histo1DPtr, begin, end): histo #{} took {} us to sample {} times",
                      idx, histoElapsed.count(), nBins);
    }

    auto totalElapsed = swTotal.get_elapsed();
    spdlog::debug("add(Histo1DPtr, begin, end): total time for {} histos: {} us",
                  std::distance(begin, end), totalElapsed.count());
}

template <typename It>
Histo1DPtr add(It begin, It end, const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    if (begin == end)
        return {};

    auto result = std::make_shared<Histo1D>(calculate_addition_dest_binning(begin, end, bm));
    add(result, begin, end, bm);
    return result;
}

inline Histo1DPtr add(const std::initializer_list<const Histo1D *> &histos,
               const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    return add(std::begin(histos), std::end(histos), bm);
}

inline Histo1DPtr add(const Histo1D &a, const Histo1D &b,
               const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    return add({&a, &b}, bm);
}

// Same as above but add all histograms in the list.
inline Histo1DPtr add(const Histo1DList &histos,
               const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    return add(std::begin(histos), std::end(histos), bm);
}

} // namespace mesytec::mvme

#endif /* E23CBE69_DF0C_4A71_AF2C_3D8137587C23 */
