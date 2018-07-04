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

void LIBMVME_EXPORT
    generate_new_object_ids(const AnalysisObjectVector &objects);

/** Generate new unique IDs for all objects in the analysis.
 * IMPORTANT: Does not update the ModuleProperties information! It will still contain the
 * old IDs. */
void LIBMVME_EXPORT
    generate_new_object_ids(Analysis *analysis);

template<typename It>
    void generate_new_object_ids(It begin_, It end_)
{
    while (begin_ != end_)
    {
        (*begin_++)->setId(QUuid::createUuid());
    }
}

AnalysisObjectSet to_set(const AnalysisObjectVector &objects);


} // namespace analysis

#endif /* __ANALYSIS_UTIL_H__ */
