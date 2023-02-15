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
#ifndef __ANALYSIS_UTIL_H__
#define __ANALYSIS_UTIL_H__

#include <string>
#include <utility>
#include <vector>

#include "libmvme_export.h"
#include "analysis.h"

class QTreeWidgetItem;

namespace analysis
{

QVector<std::shared_ptr<SourceInterface>> LIBMVME_EXPORT
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
// These functions collect dependent objects of the specified startObject, i.e.
// all objects where a connection from the startObject to the specific objects
// exists.
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

// Get the set of objects forming the input-chain of the given operator.
// This is the inverse of collect_dependent_objects(), i.e. it walks the
// connection chain backwards, adding all traversed objects to the result set.
QSet<PipeSourceInterface *> LIBMVME_EXPORT
collect_input_set(OperatorInterface *op);

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

AnalysisObjectSet LIBMVME_EXPORT to_set(const AnalysisObjectVector &objects);

inline bool is_sink(AnalysisObject *obj)
{
    return qobject_cast<SinkInterface *>(obj);
}

inline bool is_sink(const AnalysisObjectPtr &obj)
{
    return is_sink(obj.get());
}

inline bool is_condition(const AnalysisObject *obj)
{
    return qobject_cast<const ConditionInterface *>(obj);
}

inline bool is_condition(const AnalysisObjectPtr &obj)
{
    return is_condition(obj.get());
}

using StringSet = QSet<QString>;
using NamesByMetaObject = QHash<const QMetaObject *, StringSet>;

StringSet LIBMVME_EXPORT get_object_names(const AnalysisObjectVector &objects);
NamesByMetaObject LIBMVME_EXPORT group_object_names_by_metatype(const AnalysisObjectVector &objects);

QString LIBMVME_EXPORT make_clone_name(const QString &currentName, const StringSet &allNames);

/* Helper class forwarding signals originating from a given Analysis instance.
 * This can be used to locally react to analysis signals but also be able to
 * block the signals temporarily without affecting other observers of the
 * analysis instance..
 */
class LIBMVME_EXPORT AnalysisSignalWrapper: public QObject
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

        void conditionLinkAdded(const OperatorPtr &op, const ConditionPtr &cond);
        void conditionLinkRemoved(const OperatorPtr &op, const ConditionPtr &cond);

        void objectAdded(const AnalysisObjectPtr &obj);
        void objectRemoved(const AnalysisObjectPtr &obj);

    public:
        explicit AnalysisSignalWrapper(QObject *parent = nullptr);
        explicit AnalysisSignalWrapper(Analysis *analysis, QObject *parent = nullptr);

        void setAnalysis(Analysis *analysis);
        Analysis *getAnalysis() const { return m_analysis; }

    private:
        Analysis *m_analysis = nullptr;
};

/* Returns all operators to which the given condition can be applied to and
 * which are contained in the given OperatorVector. */
OperatorVector LIBMVME_EXPORT
get_apply_condition_candidates(
    const ConditionPtr &cond, const OperatorVector &operators);

/* Returns all operators to which the given condition can be applied to. */
OperatorVector LIBMVME_EXPORT
get_apply_condition_candidates(
    const ConditionPtr &cond, const Analysis *analysis);

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

/* Filters sinks, returning the ones using all of the inputs that are used by
 * the Condition. */
SinkVector LIBMVME_EXPORT
    find_sinks_for_condition(const ConditionPtr &cond, const SinkVector &allSinks);

/* Filters conditions, returning the ones using the same inputs slots that are
 * used by the given sink. */
ConditionVector LIBMVME_EXPORT
    find_conditions_for_sink(const SinkPtr &sink, const ConditionVector &conditions);

SinkPtr LIBMVME_EXPORT
    create_edit_sink_for_condition(const ConditionPtr &cond);

// Disconnects the Slots connected to the outputs of the given
// PipeSourceInterface. Returns number of Slots that have been disconnected.
size_t LIBMVME_EXPORT disconnect_outputs(PipeSourceInterface *pipeSource);

bool LIBMVME_EXPORT uses_multi_event_splitting(const VMEConfig &vmeConfig, const Analysis &analysis);
bool LIBMVME_EXPORT uses_event_builder(const VMEConfig &vmeConfig, const Analysis &analysis);

std::vector<std::vector<std::string>> LIBMVME_EXPORT
collect_multi_event_splitter_filter_strings(
    const VMEConfig &vmeConfig, const Analysis &analysis);

void LIBMVME_EXPORT add_default_filters(Analysis *analysis, ModuleConfig *module);

std::pair<std::shared_ptr<Analysis>, std::error_code> LIBMVME_EXPORT read_analysis(const QJsonDocument &doc);

// Adds the condition to the analysis and places it in the common conditions
// directory for the conditions userlevel. The directory is created if it does
// not exist yet. Returns the destination directory where the condition was placed.
DirectoryPtr LIBMVME_EXPORT add_condition_to_analysis(Analysis *analysis, const ConditionPtr &cond);

} // namespace analysis


#endif /* __ANALYSIS_UTIL_H__ */
