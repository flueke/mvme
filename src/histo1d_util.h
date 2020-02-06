#ifndef __MVME_HISTO1D_UTIL_H__
#define __MVME_HISTO1D_UTIL_H__

#include <QTextStream>

#include "histo1d.h"
#include "libmvme_export.h"

namespace mvme
{

LIBMVME_EXPORT QTextStream &print_histolist_stats(
    QTextStream &out,
    const QVector<std::shared_ptr<Histo1D>> &histos,
    u32 resolutionReductionFactor = AxisBinning::NoResolutionReduction,
    const QString &title = {});

}

#endif /* __MVME_HISTO1D_UTIL_H__ */
