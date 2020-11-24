#ifndef __MVME_ANALYSIS_BENCH_H__
#define __MVME_ANALYSIS_BENCH_H__

#include <QJsonObject>
#include "libmvme_export.h"
#include "mvme_context.h"

QJsonObject LIBMVME_EXPORT make_analysis_benchmark_info(const MVMEContext &context);

#endif /* __MVME_ANALYSIS_BENCH_H__ */
