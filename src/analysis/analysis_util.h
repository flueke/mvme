#ifndef __ANALYSIS_UTIL_H__
#define __ANALYSIS_UTIL_H__

#include "analysis.h"
#include "libmvme_export.h"

namespace analysis
{

QVector<std::shared_ptr<Extractor>> LIBMVME_EXPORT
    get_default_data_extractors(const QString &moduleTypeName);

QSet<PipeSourceInterface *> LIBMVME_EXPORT
    collect_dependent_operators();


QSet<OperatorInterface *> LIBMVME_EXPORT
    collect_dependent_operators(PipeSourceInterface *startObject);

void LIBMVME_EXPORT
    collect_dependent_operators(PipeSourceInterface *startObject,
                                 QSet<OperatorInterface *> &dest);

QSet<PipeSourceInterface *> LIBMVME_EXPORT
    collect_dependent_objects(PipeSourceInterface *startObject);

void LIBMVME_EXPORT
    collect_dependent_objects(PipeSourceInterface *startObject,
                                 QSet<PipeSourceInterface *> &dest);


} // namespace analysis

#endif /* __ANALYSIS_UTIL_H__ */
