/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVME_HISTO1D_UTIL_H__
#define __MVME_HISTO1D_UTIL_H__

#include <QTextStream>

#include "histo1d.h"
#include "libmvme_export.h"

namespace mvme
{

struct HistolistStatsOptions
{
    bool printGaussStats = false;
};

// Outputs a formatted table containing statistics of all the histograms in the
// given histos vector.
// This overload uses the full histograms x-axis range for stat calculations.
LIBMVME_EXPORT QTextStream &print_histolist_stats(
    QTextStream &out,
    const QVector<std::shared_ptr<Histo1D>> &histos,
    u32 resolutionReductionFactor = AxisBinning::NoResolutionReduction,
    const QString &title = {},
    const HistolistStatsOptions &opts = {});

// Outputs a formatted table containing statistics of all the histograms in the
// given histos vector.
// This overload limits the stats calculations to the interval given by the
// xMin and xMax values. If any of the values is NaN the respective value is
// taken from the histograms x-axis binning.
LIBMVME_EXPORT QTextStream &print_histolist_stats(
    QTextStream &out,
    const QVector<std::shared_ptr<Histo1D>> &histos,
    double xMin, double xMax,
    u32 resolutionReductionFactor = AxisBinning::NoResolutionReduction,
    const QString &title = {},
    const HistolistStatsOptions &opts = {});

}

#endif /* __MVME_HISTO1D_UTIL_H__ */
