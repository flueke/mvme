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
    const QString &title)
{
    return print_histolist_stats(
        out, histos, make_quiet_nan(), make_quiet_nan(), rrf, title);
}

QTextStream &print_histolist_stats(
    QTextStream &out,
    const QVector<std::shared_ptr<Histo1D>> &histos,
    double xMin, double xMax,
    u32 rrf,
    const QString &title)
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

    out << "# Stats for histogram array '" << title << "'" << endl;
    out << "# Number of histos: " << histos.size()
        << ", bins: " << first->getAxisBinning(Qt::XAxis).getBinCount(rrf)
        << endl;

    out << endl;

    static const int FieldWidth = 14;
    out.setFieldAlignment(QTextStream::AlignLeft);

    out << qSetFieldWidth(0) << "# " << qSetFieldWidth(FieldWidth)
        << "HistoIndex" << "EntryCount" << "Max" << "Mean"
        << "RMS" << "Gauss Mean" << "FWHM"
        << "Histo_x1" << "Histo_x2" << "Bin Width"
        << qSetFieldWidth(0) << endl;

    using ValueAndIndex = std::pair<double, size_t>;

    ValueAndIndex minMean = { std::numeric_limits<double>::max(), 0 };
    ValueAndIndex maxMean = { std::numeric_limits<double>::lowest(), 0 };
    ValueAndIndex minFWHM = { std::numeric_limits<double>::max(), 0 };
    ValueAndIndex maxFWHM = { std::numeric_limits<double>::lowest(), 0 };
    ValueAndIndex minRMS  = { std::numeric_limits<double>::max(), 0 };
    ValueAndIndex maxRMS  = { std::numeric_limits<double>::lowest(), 0 };

    for (const auto &is: stats | indexed(0))
    {
        const auto &index = is.index();
        const auto &stats = is.value();
        const auto &histo = histos[index];

        if (stats.mean < minMean.first)
            minMean = { stats.mean, index };

        if (stats.mean > maxMean.first)
            maxMean = { stats.mean, index };

        if (stats.fwhm < minFWHM.first)
            minFWHM = { stats.fwhm, index };

        if (stats.fwhm > maxFWHM.first)
            maxFWHM = { stats.fwhm, index };

        if (stats.sigma < minRMS.first)
            minRMS = { stats.sigma, index };

        if (stats.sigma > maxRMS.first)
            maxRMS = { stats.sigma, index };

        out << qSetFieldWidth(0) << "  " << qSetFieldWidth(FieldWidth)
            << index << stats.entryCount << stats.maxValue << stats.mean
            << stats.sigma << stats.fwhmCenter << stats.fwhm
            << stats.statsRange.first << stats.statsRange.second << histo->getBinWidth(rrf)
            //<< histo->getXMin() << histo->getXMax() << histo->getBinWidth(rrf)
            << qSetFieldWidth(0) << endl;
    }

    out << qSetFieldWidth(0) << endl;
    out << "Mean min: " << minMean.first << " in histo " << minMean.second << endl;
    out << "Mean max: " << maxMean.first << " in histo " << maxMean.second << endl;

    out << endl;
    out << "RMS min: " << minRMS.first << " in histo " << minRMS.second << endl;
    out << "RMS max: " << maxRMS.first << " in histo " << maxRMS.second << endl;

    out << endl;
    out << "FWHM min: " << minFWHM.first << " in histo " << minFWHM.second << endl;
    out << "FWHM max: " << maxFWHM.first << " in histo " << maxFWHM.second << endl;


    return out;
}

}
