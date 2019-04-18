#include "analysis_util.h"

#include <QDir>
#include <QJsonDocument>
#include <QMessageBox>
#include <QRegularExpression>

#include "../template_system.h"

namespace analysis
{

QVector<std::shared_ptr<Extractor>> get_default_data_extractors(const QString &moduleTypeName)
{
    QVector<std::shared_ptr<Extractor>> result;

    QDir moduleDir(vats::get_module_path(moduleTypeName));
    QFile filtersFile(moduleDir.filePath("analysis/default_filters.analysis"));

    if (filtersFile.open(QIODevice::ReadOnly))
    {
        auto doc = QJsonDocument::fromJson(filtersFile.readAll());
        Analysis filterAnalysis;
        /* Note: This does not do proper config conversion as no VMEConfig is
         * passed to Analysis::read().  It is assumed that the default filters
         * shipped with mvme are in the latest format (or a format that does
         * not need a VMEConfig to be upconverted). */

        if (auto ec = filterAnalysis.read(doc.object()[QSL("AnalysisNG")].toObject()))
        {
            QMessageBox::critical(nullptr,
                                  QSL("Error loading default filters"),
                                  ec.message().c_str());
        }
        else
        {
            for (auto source: filterAnalysis.getSources())
            {
                auto extractor = std::dynamic_pointer_cast<Extractor>(source);
                if (extractor)
                {
                    result.push_back(extractor);
                }
            }

            qSort(result.begin(), result.end(), [](const auto &a, const auto &b) {
                return a->objectName() < b->objectName();
            });
        }
    }

    return result;
}

//
// Dependencies returned as OperatorInterface*
//

QSet<OperatorInterface *> collect_dependent_operators(PipeSourceInterface *startObject,
                                                      CollectFlags::Flag flags)
{
    assert(startObject);

    QSet<OperatorInterface *> result;

    collect_dependent_operators(startObject, result, flags);

    return result;
}

QSet<OperatorInterface *> collect_dependent_operators(const PipeSourcePtr &startObject,
                                                      CollectFlags::Flag flags)
{
    return collect_dependent_operators(startObject.get(), flags);
}

void collect_dependent_operators(PipeSourceInterface *startObject,
                                 QSet<OperatorInterface *> &result,
                                 CollectFlags::Flag flags)
{
    for (s32 oi = 0; oi < startObject->getNumberOfOutputs(); oi++)
    {
        auto outPipe = startObject->getOutput(oi);

        for (auto destSlot: outPipe->getDestinations())
        {
            if (auto op = destSlot->parentOperator)
            {
                auto test = flags & CollectFlags::All;

                if (test == CollectFlags::All
                    || (test == CollectFlags::Operators && !is_sink(op))
                    || (test == CollectFlags::Sinks && is_sink(op)))
                {
                    result.insert(op);
                    collect_dependent_operators(op, result, flags);
                }
            }
        }
    }
}

void collect_dependent_operators(const PipeSourcePtr &startObject,
                                 QSet<OperatorInterface *> &result,
                                 CollectFlags::Flag flags)
{
    return collect_dependent_operators(startObject.get(), result, flags);
}

//
// Dependencies returned as PipeSourceInterface*
//

QSet<PipeSourceInterface *> collect_dependent_objects(PipeSourceInterface *startObject,
                                                      CollectFlags::Flag flags)
{
    assert(startObject);

    QSet<PipeSourceInterface *> result;

    for (auto &op: collect_dependent_operators(startObject, flags))
    {
        result.insert(qobject_cast<PipeSourceInterface *>(op));
    }

    return result;
}

QSet<PipeSourceInterface *> collect_dependent_objects(const PipeSourcePtr &startObject,
                                                      CollectFlags::Flag flags)
{
    return collect_dependent_objects(startObject.get(), flags);
}

void generate_new_object_ids(const AnalysisObjectVector &objects)
{
    QHash<QUuid, QUuid> oldToNewIds;

    for (auto &obj: objects)
    {
        auto oldId = obj->getId();
        auto newId = QUuid::createUuid();
        obj->setId(newId);
        oldToNewIds.insert(oldId, newId);
    }

    /* Rewrite directory member lists. */
    for (auto &obj: objects)
    {
        if (auto dir = std::dynamic_pointer_cast<Directory>(obj))
        {
            auto oldMembers = dir->getMembers();
            Directory::MemberContainer newMembers;
            newMembers.reserve(oldMembers.size());

            for (const auto &oldId: oldMembers)
            {
                if (!oldToNewIds.value(oldId).isNull())
                {
                    newMembers.push_back(oldToNewIds.value(oldId));
                }
            }

            dir->setMembers(newMembers);
        }
    }
}

void generate_new_object_ids(Analysis *analysis)
{
    generate_new_object_ids(analysis->getAllObjects());
}

QSet<QString> get_object_names(const AnalysisObjectVector &objects)
{
    QSet<QString> result;

    for (const auto &obj: objects)
    {
        result.insert(obj->objectName());
    }

    return result;
}

NamesByMetaObject group_object_names_by_metatype(const AnalysisObjectVector &objects)
{
    NamesByMetaObject result;

    for (auto &obj: objects)
    {
        result[obj->metaObject()].insert(obj->objectName());
    }

    return result;
}

// Note: the first '+?' is the ungreedy version of '+'
// A great regex debugging helper can be found here: https://regex101.com/
static const char *CloneNameRegexp = "^.+?(?<suffix>\\ Copy(?<counter>\\d+)?)$";

QString make_clone_name(const QString &currentName, const StringSet &allNames)
{
    if (currentName.isEmpty()) return currentName;

    QString result = currentName;

    while (allNames.contains(result))
    {
        static const QRegularExpression re(CloneNameRegexp);
        auto match = re.match(result);

        if (!match.hasMatch() || match.capturedRef(QSL("suffix")).isNull())
        {
            result += QSL(" Copy");
        }
        else if (match.capturedRef(QSL("counter")).isNull())
        {
            result += QString::number(1);
        }
        else
        {
            auto sref = match.capturedRef(QSL("counter"));
            ulong counter = sref.toULong() + 1;

            result.replace(match.capturedStart(QSL("counter")),
                           match.capturedLength(QSL("counter")),
                           QString::number(counter));

            if (counter == std::numeric_limits<u32>::max())
                break;
        }
    }

    return result;
}

//
// AnalysisSignalWrapper
//

AnalysisSignalWrapper::AnalysisSignalWrapper(QObject *parent)
    : QObject(parent)
{ }

AnalysisSignalWrapper::AnalysisSignalWrapper(Analysis *analysis,
                                                 QObject *parent)
    : QObject(parent)
{
    setAnalysis(analysis);
}

void AnalysisSignalWrapper::setAnalysis(Analysis *analysis)
{
    QObject::connect(analysis, &Analysis::modified,
                     this, &AnalysisSignalWrapper::modified);

    QObject::connect(analysis, &Analysis::modifiedChanged,
                     this, &AnalysisSignalWrapper::modifiedChanged);

    QObject::connect(analysis, &Analysis::dataSourceAdded,
                     this, &AnalysisSignalWrapper::dataSourceAdded);

    QObject::connect(analysis, &Analysis::dataSourceRemoved,
                     this, &AnalysisSignalWrapper::dataSourceRemoved);

    QObject::connect(analysis, &Analysis::operatorAdded,
                     this, &AnalysisSignalWrapper::operatorAdded);

    QObject::connect(analysis, &Analysis::operatorRemoved,
                     this, &AnalysisSignalWrapper::operatorRemoved);

    QObject::connect(analysis, &Analysis::directoryAdded,
                     this, &AnalysisSignalWrapper::directoryAdded);

    QObject::connect(analysis, &Analysis::directoryRemoved,
                     this, &AnalysisSignalWrapper::directoryRemoved);

    QObject::connect(analysis, &Analysis::conditionLinkApplied,
                     this, &AnalysisSignalWrapper::conditionLinkApplied);

    QObject::connect(analysis, &Analysis::conditionLinkCleared,
                     this, &AnalysisSignalWrapper::conditionLinkCleared);
}

OperatorVector get_apply_condition_candidates(const ConditionPtr &cond,
                                              const OperatorVector &operators)
{
    OperatorVector result;

    result.reserve(operators.size());

    for (const auto &op: operators)
    {
        /* Cannot apply a condition to itself. */
        if (op == cond)
            continue;

        /* Both objects have to reside in the same vme event. */
        if (op->getEventId() != cond->getEventId())
            continue;

        /* Use input ranks to determine if the condition has been evaluated at
         * the point the operator will be executed. Input ranks are used
         * instead of the calculated ranks (getRank()) because the latter will
         * be adjusted if an operator does currently make use of a condition.
         * Using the max input rank gives the unadjusted rank as if the
         * operator did not use a condition.  */
        if (cond->getMaximumInputRank() > op->getMaximumInputRank())
            continue;

        result.push_back(op);
    }

    return result;
}

OperatorVector get_apply_condition_candidates(const ConditionPtr &cond,
                                              const Analysis *analysis)
{
    return get_apply_condition_candidates(cond, analysis->getOperators());
}

QDebug &operator<<(QDebug &dbg, const AnalysisObjectPtr &obj)
{
    dbg << obj.get() << ", id =" << (obj ? obj->getId() : QSL(""));
    return dbg;
}

namespace
{

bool slots_match(const QVector<Slot *> &slotsA, const QVector<Slot *> &slotsB)
{
    assert(slotsA.size() == slotsB.size());

    auto slot_input_equal = [] (const Slot *slotA, const Slot *slotB) -> bool
    {
        return (slotA->inputPipe == slotB->inputPipe
                && slotA->paramIndex == slotB->paramIndex);
    };

    for (int si = 0; si < slotsA.size(); si++)
    {
        if (!slot_input_equal(slotsA[si], slotsB[si]))
        {
            return false;
        }
    }

    return true;
}

}

/* Filters sinks, returning the ones using all of the inputs that are used by
 * the ConditionLink. */
SinkVector get_sinks_for_conditionlink(const ConditionLink &cl, const SinkVector &sinks)
{
    /* Get the input slots of the condition and of each sink.
     * Sort each of the slots vectors by input pipe and param index.
     * Then compare the pairs of condition and sink slots.
     * Add the sink to the result if all of the slots compare equal. */

    auto slot_lessThan = [] (const Slot *slotA, const Slot *slotB) -> bool
    {
        if (slotA->inputPipe == slotB->inputPipe)
            return slotA->paramIndex < slotB->paramIndex;

        return slotA->inputPipe < slotB->inputPipe;
    };

    auto condInputSlots = cl.condition->getSlots();

    qSort(condInputSlots.begin(), condInputSlots.end(), slot_lessThan);

    SinkVector result;
    result.reserve(sinks.size());

    for (const auto &sink: sinks)
    {
        if (sink->getEventId() != cl.condition->getEventId())
            continue;

        if (sink->getNumberOfSlots() != condInputSlots.size())
            continue;

        auto sinkSlots = sink->getSlots();

        qSort(sinkSlots.begin(), sinkSlots.end(), slot_lessThan);

        if (slots_match(condInputSlots, sinkSlots))
        {
            result.push_back(sink);
        }
    }

    return result;
}

size_t disconnect_outputs(PipeSourceInterface *pipeSource)
{
    size_t result = 0u;

    for (s32 oi = 0; oi < pipeSource->getNumberOfOutputs(); oi++)
    {
        Pipe *outPipe = pipeSource->getOutput(oi);

        for (Slot *destSlot: outPipe->getDestinations())
        {
            destSlot->disconnectPipe();
            result++;
        }
        assert(outPipe->getDestinations().isEmpty());
    }

    return result;
}

} // namespace analysis

