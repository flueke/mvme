#ifndef E23CBE69_DF0C_4A71_AF2C_3D8137587C23
#define E23CBE69_DF0C_4A71_AF2C_3D8137587C23

// These are not in histo_util.h. because they depend on Histo1D and Histo2D.
// histo_util.h only contains a forward declaration.

// TODO: also move projections and other algos into this file

#include "histo1d.h"
#include "histo2d.h"

namespace mesytec::mvme
{

enum class HistoOpsBinningMode
{
    MinimumBins, // the resulting histogram has the smallest number of bins of the input histos
    MaximumBins  // the resulting histogram has the largest number of bins of the input histos
};

// Add the histograms a and b. The number of bins in the resulting histogram
// depends on the binning mode. Sampling the source histograms makes use of bin
// subsampling/multi bin sampling if needed.
//
// This is different from ROOT TH1 and boost::histogram where the binnings of
// the operands have to match exactly.
Histo1DPtr add(const Histo1D &a, const Histo1D &b,
               const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins);

// Same as above but add all histograms in the list.
Histo1DPtr add(const Histo1DList &histos,
               const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins);

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

AxisBinning
calculate_addition_dest_binning(const std::initializer_list<const Histo1D *> &histos,
                                const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    return calculate_addition_dest_binning(std::begin(histos), std::end(histos), bm);
}

AxisBinning
calculate_addition_dest_binning(const Histo1D &a, const Histo1D &b,
                                const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    return calculate_addition_dest_binning({&a, &b}, bm);
}

AxisBinning
calculate_addition_dest_binning(const Histo1DList &histos,
                                const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    return calculate_addition_dest_binning(std::begin(histos), std::end(histos), bm);
}

AxisBinning
calculate_addition_dest_binning(mesytec::mvlc::util::span<const Histo1D *> histos,
                                const HistoOpsBinningMode &bm = HistoOpsBinningMode::MinimumBins)
{
    return calculate_addition_dest_binning(std::begin(histos), std::end(histos), bm);
}

} // namespace mesytec::mvme

#endif /* E23CBE69_DF0C_4A71_AF2C_3D8137587C23 */
