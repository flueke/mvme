#ifndef __ANALYSIS_UTIL_H__
#define __ANALYSIS_UTIL_H__

#include <string>
#include <vector>

#include "libmvme_export.h"
#include "analysis.h"

class QTreeWidgetItem;

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

/* Helper class forwarding signals originating from a given Analysis instance.
 * This can be used to locally react to analysis signals but also be able to
 * block the signals temporarily without affecting other observers of the
 * analysis instance..
 */
class AnalysisSignalWrapper: public QObject
{
    Q_OBJECT
    signals:
        void modified(bool);
        void modifiedChanged(bool);

        void dataSourceAdded(const SourcePtr &src);
        void dataSourceRemoved(const SourcePtr &src);
        void dataSourceEdited(const SourcePtr &src);

        void operatorAdded(const OperatorPtr &op);
        void operatorRemoved(const OperatorPtr &op);
        void operatorEdited(const OperatorPtr &op);

        void directoryAdded(const DirectoryPtr &ptr);
        void directoryRemoved(const DirectoryPtr &ptr);

        void conditionLinkApplied(const OperatorPtr &op, const ConditionLink &cl);
        void conditionLinkCleared(const OperatorPtr &op, const ConditionLink &cl);

    public:
        AnalysisSignalWrapper(QObject *parent = nullptr);
        AnalysisSignalWrapper(Analysis *analysis, QObject *parent = nullptr);

        void setAnalysis(Analysis *analysis);
        Analysis *getAnalysis() const { return m_analysis; }

    private:
        Analysis *m_analysis = nullptr;
};

/* Returns all operators to which the given condition can be applied to and
 * which are contained in the given OperatorVector. */
OperatorVector get_apply_condition_candidates(const ConditionPtr &cond,
                                              const OperatorVector &operators);

/* Returns all operators to which the given condition can be applied to. */
OperatorVector get_apply_condition_candidates(const ConditionPtr &cond,
                                              const Analysis *analysis);

// https://en.cppreference.com/w/cpp/memory/owner_less
// std::map<std::weak_ptr<T>, U, std::owner_less<std::weak_ptr<T>>>
using WeakAnalysisObject = std::weak_ptr<AnalysisObject>;

template <typename MappedType>
using ObjectMap = std::map<WeakAnalysisObject, MappedType, std::owner_less<WeakAnalysisObject>>;

using Node = QTreeWidgetItem *;
using NodeSet = QSet<Node>;

using ObjectToNode = ObjectMap<Node>;
using ObjectToNodes = ObjectMap<NodeSet>;

QDebug &operator<<(QDebug &dbg, const AnalysisObjectPtr &obj);

SinkVector get_sinks_for_conditionlink(const ConditionLink &cl, const SinkVector &sinks);

// Disconnects the Slots connected to the outputs of the given
// PipeSourceInterface. Returns number of Slots that have been disconnected.
size_t disconnect_outputs(PipeSourceInterface *pipeSource);

bool uses_multi_event_splitting(const VMEConfig &vmeConfig, const Analysis &analysis);

std::vector<std::vector<std::string>> collect_multi_event_splitter_filter_strings(
    const VMEConfig &vmeConfig, const Analysis &analysis);

} // namespace analysis


#endif /* __ANALYSIS_UTIL_H__ */
