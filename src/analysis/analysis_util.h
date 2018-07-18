#ifndef __ANALYSIS_UTIL_H__
#define __ANALYSIS_UTIL_H__

#include "analysis.h"
#include "libmvme_export.h"

namespace analysis
{

QVector<std::shared_ptr<Extractor>> LIBMVME_EXPORT
    get_default_data_extractors(const QString &moduleTypeName);

namespace CollectFlags
{
    using Flag = u8;

    static const Flag Operators = 1u << 0;
    static const Flag Sinks     = 1u << 1;
    static const Flag All       = Operators | Sinks;
};


//
// Dependencies returned as OperatorInterface*
//

QSet<OperatorInterface *> LIBMVME_EXPORT
collect_dependent_operators(PipeSourceInterface *startObject,
                            CollectFlags::Flag flags = CollectFlags::All);

QSet<OperatorInterface *> LIBMVME_EXPORT
collect_dependent_operators(const PipeSourcePtr &startObject,
                            CollectFlags::Flag flags = CollectFlags::All);

void LIBMVME_EXPORT
collect_dependent_operators(PipeSourceInterface *startObject,
                            QSet<OperatorInterface *> &dest,
                            CollectFlags::Flag flags = CollectFlags::All);

void LIBMVME_EXPORT
collect_dependent_operators(const PipeSourcePtr &startObject,
                            QSet<OperatorInterface *> &dest,
                            CollectFlags::Flag flags = CollectFlags::All);


//
// Dependencies returned as PipeSourceInterface*
//

QSet<PipeSourceInterface *> LIBMVME_EXPORT
collect_dependent_objects(PipeSourceInterface *startObject,
                          CollectFlags::Flag flags = CollectFlags::All);

QSet<PipeSourceInterface *> LIBMVME_EXPORT
collect_dependent_objects(const PipeSourcePtr &startObject,
                          CollectFlags::Flag flags = CollectFlags::All);


//
// object ids
//

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


//
// misc
//

AnalysisObjectSet to_set(const AnalysisObjectVector &objects);

inline bool is_sink(AnalysisObject *obj)
{
    return qobject_cast<SinkInterface *>(obj);
}

inline bool is_sink(const AnalysisObjectPtr &obj)
{
    return is_sink(obj.get());
}


using StringSet = QSet<QString>;
using NamesByMetaObject = QHash<const QMetaObject *, StringSet>;

StringSet get_object_names(const AnalysisObjectVector &objects);
NamesByMetaObject group_object_names_by_metatype(const AnalysisObjectVector &objects);

QString make_clone_name(const QString &currentName, const StringSet &allNames);

} // namespace analysis

#endif /* __ANALYSIS_UTIL_H__ */
