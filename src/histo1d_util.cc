/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "histo1d_util.h"

#include <boost/range/adaptor/indexed.hpp>
#include "util/math.h"

using boost::adaptors::indexed;

namespace mvme
{

using util::make_quiet_nan;

QTextStream &print_histolist_stats(
    QTextStream &out,
    const QVector<std::shared_ptr<Histo1D>> &histos,
    u32 rrf,
    const QString &title,
    const HistolistStatsOptions &opts)
{
    return print_histolist_stats(
        out, histos, make_quiet_nan(), make_quiet_nan(), rrf, title, opts);
}

QTextStream &print_histolist_stats(
    QTextStream &out,
    const QVector<std::shared_ptr<Histo1D>> &histos,
    double xMin, double xMax,
    u32 rrf,
    const QString &title,
    const HistolistStatsOptions &opts)
{
    if (histos.isEmpty())
        return out;

    QVector<Histo1DStatistics> stats;
    stats.reserve(histos.size());

    for (const auto &histo: histos)
    {
        double hMin = std::isnan(xMin) ? histo->getXMin() : xMin;
        double hMax = std::isnan(xMax) ? histo->getXMax() : xMax;

        stats.push_back(histo->calcStatistics(hMin, hMax, rrf));
    }

    out.setFieldWidth(0);

    const auto &first = histos.at(0);
    const s64 reducedBinCount = first->getAxisBinning(Qt::XAxis).getBinCount(rrf);

    out << "# Stats for histogram array '" << title << "'" << endl;
    out << "# Number of histos: " << histos.size()
        << ", bins: " << reducedBinCount
        << endl;

    out << endl;

    static const int FieldWidth = 14;
    out.setFieldAlignment(QTextStream::AlignLeft);

    out << qSetFieldWidth(0) << "# " << qSetFieldWidth(FieldWidth)
        << "HistoIndex" << "EntryCount" << "Mean" << "RMS";

    if (opts.printGaussStats)
        out << "Gauss Mean" << "FWHM";

    out << qSetFieldWidth(0) << endl;

    using ValueAndIndex = std::pair<double, int>;

    ValueAndIndex minValue = { make_quiet_nan(), -1 };
    ValueAndIndex maxValue = { make_quiet_nan(), -1 };

    double sumWeightedRms = 0.0;
    double sumWeightedMean = 0.0;
    double sumEntryCounts = 0.0;

    auto make_min_max = [] ()
    {
        return std::make_pair<double, double>(std::numeric_limits<double>::max(), 0);
    };

    auto update_min_max = [] (auto &minmax, double value)
    {
        minmax.first = std::min(minmax.first, value);
        minmax.second = std::max(minmax.second, value);
    };

    auto minmaxEntries = make_min_max();
    auto minmaxMean = make_min_max();
    auto minmaxRMS = make_min_max();
    auto minmaxGauss = make_min_max();
    auto minmaxFWHM = make_min_max();


    for (const auto &is: stats | indexed(0))
    {
        const auto &index = is.index();
        const auto &stats = is.value();
        const auto &histo = histos[index];

        if (stats.entryCount > 0)
        {
            for (s64 bin=0; bin<reducedBinCount; ++bin)
            {
                if (histo->getBinContent(bin, rrf) > 0.0)
                {
                    if (std::isnan(minValue.first) || histo->getBinLowEdge(bin, rrf) < minValue.first)
                        minValue = { histo->getBinLowEdge(bin, rrf), index };
                    break;
                }
            }

            for (s64 bin=reducedBinCount-1; bin >= 0; --bin)
            {
                if (histo->getBinContent(bin, rrf) > 0.0)
                {
                    if (std::isnan(maxValue.first) || histo->getBinLowEdge(bin, rrf) > maxValue.first)
                        maxValue = { histo->getBinLowEdge(bin, rrf), index };
                    break;
                }
            }
        }

        sumWeightedRms  += stats.sigma * stats.entryCount;
        sumWeightedMean += stats.mean * stats.entryCount;
        sumEntryCounts += stats.entryCount;

        out << qSetFieldWidth(0) << "  " << qSetFieldWidth(FieldWidth)
            << index << stats.entryCount << stats.mean << stats.sigma;

        if (opts.printGaussStats)
            out << stats.fwhmCenter << stats.fwhm;

        out << qSetFieldWidth(0) << endl;

        update_min_max(minmaxEntries, stats.entryCount);
        update_min_max(minmaxMean, stats.mean);
        update_min_max(minmaxRMS, stats.sigma);
        update_min_max(minmaxGauss, stats.fwhmCenter);
        update_min_max(minmaxFWHM, stats.fwhm);
    }

    out << endl;

    // minimum column values
    out << qSetFieldWidth(0) << "  " << qSetFieldWidth(FieldWidth)
        << "min" << minmaxEntries.first << minmaxMean.first <<  minmaxRMS.first;

    if (opts.printGaussStats)
        out << minmaxGauss.first << minmaxFWHM.first;

    out << qSetFieldWidth(0) << endl;

    // maximum column values
    out << qSetFieldWidth(0) << "  " << qSetFieldWidth(FieldWidth)
        << "max" << minmaxEntries.second << minmaxMean.second <<  minmaxRMS.second;

    if (opts.printGaussStats)
        out << minmaxGauss.second << minmaxFWHM.second;

    out << qSetFieldWidth(0) << endl;

    //out << "min: " << minValue.first << " in histo " << minValue.second << endl;
    //out << "max: " << maxValue.first << " in histo " << maxValue.second << endl;

    out << endl;

    double weightedMean = sumWeightedMean / sumEntryCounts;
    double weightedRms = sumWeightedRms / sumEntryCounts;

    out << "mean: " << weightedMean << endl;
    out << "rms: " << weightedRms << endl;

    return out;
}

}
