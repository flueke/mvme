#include "histo_algo.h"

namespace mesytec::mvme
{

Histo1DPtr add(const Histo1D &a, const Histo1D &b, const HistoOpsBinningMode &bm)
{
    auto result = std::make_shared<Histo1D>(calculate_addition_dest_binning(a, b, bm));
    auto nBins = result->getNumberOfBins();

    for (size_t destbin = 0; destbin < nBins; ++destbin)
    {
        double xLow = result->getBinLowEdge(destbin);
        double xHigh = xLow + result->getBinWidth();

        auto countsA = a.getCounts(xLow, xHigh);
        auto countsB = b.getCounts(xLow, xHigh);

        result->setBinContent(destbin, countsA + countsB, countsA + countsB);
    }

    return result;
}

Histo1DPtr add(const Histo1DList &histos, const HistoOpsBinningMode &bm)
{
    auto result = std::make_shared<Histo1D>(calculate_addition_dest_binning(histos, bm));
    auto nBins = result->getNumberOfBins();

    for (const auto &histo: histos)
    {
        for (size_t destbin = 0; destbin < nBins; ++destbin)
        {
            double xLow = result->getBinLowEdge(destbin);
            double xHigh = xLow + result->getBinWidth();
            auto counts = histo->getCounts(xLow, xHigh);
            auto newCounts = result->getBinContent(destbin) + counts;
            result->setBinContent(destbin, newCounts, newCounts);
        }
    }

    return result;
}

} // namespace mesytec::mvme
