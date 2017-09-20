#ifndef __ANALYSIS_UTIL_H__
#define __ANALYSIS_UTIL_H__

#include "analysis.h"
#include "libmvme_export.h"

namespace analysis
{

QVector<std::shared_ptr<Extractor>> LIBMVME_EXPORT get_default_data_extractors(const QString &moduleTypeName);

} // namespace analysis

#endif /* __ANALYSIS_UTIL_H__ */
