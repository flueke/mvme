#ifndef __MVME_HISTO1D_UTIL_H__
#define __MVME_HISTO1D_UTIL_H__

#include <QTextStream>

#include "histo1d.h"
#include "libmvme_export.h"

namespace mvme
{

// Outputs a formatted table containing statistics of all the histograms in the
// given histos vector.
// This overload uses the full histograms x-axis range for stat calculations.
LIBMVME_EXPORT QTextStream &print_histolist_stats(
    QTextStream &out,
    const QVector<std::shared_ptr<Histo1D>> &histos,
    u32 resolutionReductionFactor = AxisBinning::NoResolutionReduction,
    const QString &title = {});

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
    const QString &title = {});

}

#endif /* __MVME_HISTO1D_UTIL_H__ */
