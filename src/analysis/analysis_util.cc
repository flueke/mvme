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
#include "analysis_util.h"

#include <algorithm>
#include <QDir>
#include <QJsonDocument>
#include <QMessageBox>
#include <QRegularExpression>
#include <memory>
#include <qnamespace.h>

#include "analysis_serialization.h"
#include "analysis/a2_adapter.h"

namespace analysis
{

QVector<std::shared_ptr<SourceInterface>> get_data_extractor_templates(const QString &moduleTypeName)
{
    QVector<std::shared_ptr<SourceInterface>> result;

    QDir moduleDir(vats::get_module_path(moduleTypeName));
    moduleDir.cd("analysis");

    for (auto filtersFilename: moduleDir.entryList({ "*.analysis" }))
    {

        QFile filtersFile(moduleDir.filePath(filtersFilename));

        if (filtersFile.open(QIODevice::ReadOnly))
        {
            auto doc = QJsonDocument::fromJson(filtersFile.readAll());
            auto filterAnalysis = std::make_shared<Analysis>();
            /* Note: This does not do proper config conversion as no VMEConfig is
            * passed to Analysis::read().  It is assumed that the default filters
            * shipped with mvme are in the latest format (or a format that does
            * not need a VMEConfig to be upconverted). */

            if (auto ec = filterAnalysis->read(doc.object()[QSL("AnalysisNG")].toObject()))
            {
                QMessageBox::critical(nullptr,
                                    QSL("Error loading default filters"),
                                    ec.message().c_str());
            }
            else
            {
                for (auto source: filterAnalysis->getSources())
                {
                    if (auto extractor = std::dynamic_pointer_cast<Extractor>(source))
                        result.push_back(extractor);
                    else if (auto extractor = std::dynamic_pointer_cast<ListFilterExtractor>(source))
                        result.push_back(extractor);
                }
            }
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

namespace
{

void collect_input_set(OperatorInterface *op, QSet<PipeSourceInterface *> &dest)
{
    for (s32 slotIndex=0; slotIndex<op->getNumberOfSlots(); ++slotIndex)
    {
        auto slot = op->getSlot(slotIndex);

        if (slot->isConnected())
        {
            assert(slot->inputPipe->source);
            auto source = slot->inputPipe->source;
            dest.insert(source);
            if (auto sourceOp = qobject_cast<OperatorInterface *>(source))
                collect_input_set(sourceOp, dest);
        }
    }
}

}

QSet<PipeSourceInterface *> collect_input_set(OperatorInterface *op)
{
    assert(op);
    QSet<PipeSourceInterface *> result;
    collect_input_set(op, result);
    return result;
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

    QObject::connect(analysis, &Analysis::objectAdded,
                     this, &AnalysisSignalWrapper::objectAdded);

    QObject::connect(analysis, &Analysis::objectRemoved,
                     this, &AnalysisSignalWrapper::objectRemoved);

    QObject::connect(analysis, &Analysis::conditionLinkAdded,
                     this, &AnalysisSignalWrapper::conditionLinkAdded);

    QObject::connect(analysis, &Analysis::conditionLinkRemoved,
                     this, &AnalysisSignalWrapper::conditionLinkRemoved);
}

OperatorVector get_apply_condition_candidates(const ConditionPtr &cond,
                                              const OperatorVector &operators)
{
    auto inputSet = collect_input_set(cond.get());

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

#if 0
        /* Use input ranks to determine if the condition has been evaluated at
         * the point the operator will be executed. Input ranks are used
         * instead of the calculated ranks (getRank()) because the latter will
         * be adjusted if an operator does currently make use of a condition.
         * Using the max input rank gives the unadjusted rank as if the
         * operator did not use a condition.  */
        if (cond->getMaximumInputRank() > op->getMaximumInputRank())
            continue;
#endif

        if (inputSet.contains(op.get()))
        {
            qDebug() << "operator" << op->objectName()
                << "is part of the input set of condition" << cond->objectName();
            continue;
        }

        //qDebug() << "condition " << cond->objectName()
        //    << "can be applied to" << op->objectName();

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

bool slot_lessThan(const Slot *slotA, const Slot *slotB)
{
    if (slotA->inputPipe == slotB->inputPipe)
        return slotA->paramIndex < slotB->paramIndex;

    return slotA->inputPipe < slotB->inputPipe;
};

}

SinkVector find_sinks_for_condition(const ConditionPtr &cond, const SinkVector &sinks)
{
    /* Get the input slots of the condition and of each sink.
     * Sort each of the slots vectors by input pipe and param index.
     * Then compare the pairs of condition and sink slots.
     * Add the sink to the result if all of the slots compare equal. */

    auto condInputSlots = cond->getSlots();

    std::sort(condInputSlots.begin(), condInputSlots.end(), slot_lessThan);

    SinkVector result;
    result.reserve(sinks.size());

    for (const auto &sink: sinks)
    {
        if (sink->getEventId() != cond->getEventId())
            continue;

        if (sink->getNumberOfSlots() != condInputSlots.size())
            continue;

        auto sinkSlots = sink->getSlots();

        std::sort(sinkSlots.begin(), sinkSlots.end(), slot_lessThan);

        if (slots_match(condInputSlots, sinkSlots))
            result.push_back(sink);
    }

    return result;
}

ConditionVector find_conditions_for_sink(const SinkPtr &sink, const ConditionVector &conditions)
{
    /* Idea is the same as for find_sinks_for_condition():
     * Create a defined order of slots for the sink and each condition, then
     * compare these slot lists.  */

    auto sinkSlots = sink->getSlots();

    std::sort(std::begin(sinkSlots), std::end(sinkSlots), slot_lessThan);

    ConditionVector result;
    result.reserve(conditions.size());

    for (const auto &cond: conditions)
    {
        if (sink->getEventId() != cond->getEventId())
            continue;

        if (cond->getNumberOfSlots() != sinkSlots.size())
            continue;

        auto condSlots = cond->getSlots();

        std::sort(std::begin(condSlots), std::end(condSlots), slot_lessThan);

        if (slots_match(sinkSlots, condSlots))
            result.push_back(cond);
    }

    return result;
}

/* Purpose is to create a sink for graphically editing the given condition.
   The sink type depends on the type of the condition:
   - polygon cond    -> 2d histogram
   - interval cond   -> 1d histogram
   - expression cond -> no sink, is edited via its own widget
*/
SinkPtr create_edit_sink_for_condition(const ConditionPtr &condPtr)
{
    assert(condPtr);

    SinkPtr result;

    if (auto cond = std::dynamic_pointer_cast<IntervalCondition>(condPtr))
    {
        assert(cond->getSlot(0));

        if (cond->getSlot(0)->inputPipe && cond->getSlot(0)->inputPipe->source)
        {
            auto sink = std::make_shared<Histo1DSink>();
            // Connect the histo sink to input array, not to a specific index.
            // This is the same way interval conditions are created from the UI.
            sink->connectInputSlot(0, cond->getSlot(0)->inputPipe, -1);
            sink->setObjectName(cond->getSlot(0)->inputPipe->source->objectName());
            result = sink;
        }
    }
    else if (auto cond = std::dynamic_pointer_cast<PolygonCondition>(condPtr))
    {
        auto slotX = cond->getSlot(0);
        auto slotY = cond->getSlot(1);

        if (slotX && slotY
            && slotX->inputPipe && slotY->inputPipe
            && slotX->inputPipe->source && slotY->inputPipe->source)
        {
            // Same as for the 1d case but connect the x and y slots.
            auto sink = std::make_shared<Histo2DSink>();
            sink->connectInputSlot(0, slotX->inputPipe, slotX->paramIndex);
            sink->connectInputSlot(1, slotY->inputPipe, slotY->paramIndex);
            sink->setObjectName(QSL("%1 vs %2").arg(
                slotX->inputPipe->source->objectName(),
                slotY->inputPipe->source->objectName()));
            result = sink;
        }
    }

    result->setUserLevel(condPtr->getUserLevel());
    result->setEventId(condPtr->getEventId());
    result->beginRun({}, {});

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

bool uses_multi_event_splitting(const VMEConfig &vmeConfig, const Analysis &analysis)
{
    const auto &eventConfigs = vmeConfig.getEventConfigs();

    bool useMultiEventSplitting = std::any_of(
        eventConfigs.begin(), eventConfigs.end(),
        [&analysis] (const EventConfig *eventConfig)
        {
            auto analysisEventSettings = analysis.getVMEObjectSettings(
                eventConfig->getId());

            return analysisEventSettings["MultiEventProcessing"].toBool();
        });

    return useMultiEventSplitting;
}

bool LIBMVME_EXPORT uses_event_builder(const VMEConfig &vmeConfig, const Analysis &analysis)
{
    const auto &eventConfigs = vmeConfig.getEventConfigs();

    bool usesEventBuilder = std::any_of(
        eventConfigs.begin(), eventConfigs.end(),
        [&analysis] (const EventConfig *eventConfig)
        {
            auto analysisEventSettings = analysis.getVMEObjectSettings(
                eventConfig->getId());

            return analysisEventSettings["EventBuilderEnabled"].toBool();
        });

    return usesEventBuilder;
}

std::vector<std::vector<std::vector<vats::VMEModuleEventHeaderFilter>>>
collect_multi_event_splitter_filters(
    const VMEConfig &vmeConfig, const Analysis &analysis)
{
    const auto &eventConfigs = vmeConfig.getEventConfigs();

    std::vector<std::vector<std::vector<vats::VMEModuleEventHeaderFilter>>> result;

    for (const auto &eventConfig: eventConfigs)
    {
        std::vector<std::vector<vats::VMEModuleEventHeaderFilter>> eventFilters;
        auto eventSettings = analysis.getVMEObjectSettings(eventConfig->getId());
        bool enabledForEvent = eventSettings["MultiEventProcessing"].toBool();

        for (const auto &moduleConfig: eventConfig->getModuleConfigs())
        {
            if (!moduleConfig->isEnabled())
                continue;

            if (enabledForEvent)
            {
                auto moduleFilters = moduleConfig->getModuleMeta().eventHeaderFilters;
                eventFilters.emplace_back(moduleFilters);
            }
            else
            {
                // add an empty filter list for this event
                eventFilters.emplace_back(std::vector<vats::VMEModuleEventHeaderFilter>());
            }
        }

        result.emplace_back(eventFilters);
    }

    return result;
}

std::vector<std::vector<std::vector<std::string>>>
multi_event_splitter_filters_to_strings(
    const std::vector<std::vector<std::vector<vats::VMEModuleEventHeaderFilter>>> &filterDefs)
{
    std::vector<std::vector<std::vector<std::string>>> result;
    for (const auto &eventFilterDefs: filterDefs)
    {
        std::vector<std::vector<std::string>> eventFilterStrings;
        for (const auto &moduleFilterDefs: eventFilterDefs)
        {
            std::vector<std::string> moduleFilterStrings;
            for (const auto &filterDef: moduleFilterDefs)
                moduleFilterStrings.emplace_back(filterDef.filterString.constData());
            eventFilterStrings.emplace_back(moduleFilterStrings);
        }
        result.emplace_back(eventFilterStrings);
    }
    return result;
}

void add_default_filters(Analysis *analysis, ModuleConfig *module)
{
    // Read and deserialize the default_filters file
    QDir moduleDir(vats::get_module_path(module->getModuleMeta().typeName));
    QFile filtersFile(moduleDir.filePath("analysis/default_filters.analysis"));

    if (!filtersFile.open(QIODevice::ReadOnly))
        return;

    auto doc = QJsonDocument::fromJson(filtersFile.readAll());
    auto json = doc.object()["AnalysisNG"].toObject();
    json = convert_to_current_version(json, nullptr);
    auto objectStore = deserialize_objects(json, Analysis().getObjectFactory());

    // Prepare the analysis objects
    establish_connections(objectStore);
    generate_new_object_ids(objectStore.allObjects());

#if 0
    // Remove objects that have IsDefaultFilter=false set.
    AnalysisObjectVector objectsToRemove;

    for (auto &obj: objectStore.allObjects())
    {
        if (auto prop = obj->property("IsDefaultFilter");
            prop.isValid() && !prop.toBool())
        {
            objectsToRemove.push_back(obj);
        }
    }

    for (const auto &obj: objectsToRemove)
    {
        remove_object(objectStore, obj);
    }
#endif

    for (auto &obj: objectStore.allObjects())
        obj->setEventId(module->getEventId());

    for (auto &src: objectStore.sources)
        src->setModuleId(module->getId());

    // Replace occurences of the module typeName in object names with the name
    // of the concrete module we're generating filters for.
    for (auto &obj: objectStore.allObjects())
    {
        obj->setObjectName(obj->objectName().replace(
                module->getModuleMeta().typeName, module->objectName(),
                Qt::CaseInsensitive));
    }

    // Add the loaded objects to the target analysis.
    analysis->addObjects(objectStore);
}

std::pair<std::shared_ptr<Analysis>, std::error_code> read_analysis(const QJsonDocument &doc)
{
    auto json = doc.object();

    if (json.contains("AnalysisNG"))
        json = json["AnalysisNG"].toObject();

    std::pair<std::shared_ptr<Analysis>, std::error_code> ret;

    ret.first = std::shared_ptr<Analysis>(new Analysis, [] (auto a) { a->deleteLater(); });
    ret.second = ret.first->read(json);

    if (ret.second)
        ret.first = {};

    return ret;
}

DirectoryPtr add_condition_to_analysis(Analysis *analysis, const ConditionPtr &cond)
{
    assert(analysis);
    assert(cond);

    // Place the condition in the common conditions directory. Create the
    // directory if it does not exist yet.
    auto dirs = analysis->getDirectories(cond->getUserLevel(), DisplayLocation::Operator);

    auto it = std::find_if(
        std::begin(dirs), std::end(dirs),
        [](const auto &dir)
        {
            return dir->property("isConditionsDirectory").toBool();
        });

    DirectoryPtr destDir = {};

    if (it == std::end(dirs))
    {
        destDir = std::make_shared<Directory>();
        destDir->setObjectName("Conditions");
        destDir->setProperty("isConditionsDirectory", true);
        destDir->setProperty("icon", ":/scissors.png");
        destDir->setUserLevel(cond->getUserLevel());
        destDir->setDisplayLocation(DisplayLocation::Operator);
        analysis->addDirectory(destDir);
    }
    else
        destDir = *it;

    assert(destDir);

    destDir->push_back(cond);

    analysis->addOperator(cond);

    return destDir;
}

QJsonObject to_qjson(const mesytec::mvlc::readout_parser::ReadoutParserCounters &counters)
{
    using namespace mesytec::mvlc;

    QJsonObject ret;

    ret["internalBufferLoss"] = static_cast<qint64>(counters.internalBufferLoss);
    ret["buffersProcessed"] = static_cast<qint64>(counters.buffersProcessed);
    ret["bytesProcessed"] = static_cast<qint64>(counters.bytesProcessed);
    ret["unusedBytes"] = static_cast<qint64>(counters.unusedBytes);
    ret["ethPacketsProcessed"] = static_cast<qint64>(counters.ethPacketsProcessed);
    ret["ethPacketLoss"] = static_cast<qint64>(counters.ethPacketLoss);
    ret["parserExceptions"] = static_cast<qint64>(counters.parserExceptions);
    ret["emptyStackFrames"] = static_cast<qint64>(counters.emptyStackFrames);

    QJsonObject systemEvents;
    for (size_t sysEvent = 0; sysEvent < counters.systemEvents.size(); ++sysEvent)
    {
        if (counters.systemEvents[sysEvent])
        {
            auto sysEventName = system_event_type_to_string(sysEvent);
            systemEvents[QString::fromStdString(sysEventName)] = static_cast<qint64>(counters.systemEvents[sysEvent]);
        }
    }
    ret["systemEvents"] = systemEvents;

    QJsonObject parseResults;
    for (size_t pr=0; pr < counters.parseResults.size(); ++pr)
    {
        if (counters.parseResults[pr])
        {
            auto name = get_parse_result_name(static_cast<readout_parser::ParseResult>(pr));
            parseResults[QString::fromStdString(name)] = static_cast<qint64>(counters.parseResults[pr]);
        }
    }
    ret["parseResults"] = parseResults;

    QJsonArray eventHits;
    for (const auto &[ei, hits]: counters.eventHits)
    {
        QJsonObject entry;
        entry["eventIndex"] = static_cast<qint64>(ei);
        entry["hits"] = static_cast<qint64>(hits);
        eventHits.push_back(entry);
    }
    ret["eventHits"] = eventHits;

    QJsonArray moduleHits;
    for (const auto &[indexes, hits]: counters.groupHits)
    {
        const auto &[eventIndex, moduleIndex ] = indexes;
        const auto &sizeInfo = counters.groupSizes.at(indexes);

        QJsonObject entry;
        entry["eventIndex"] = static_cast<qint64>(eventIndex);
        entry["moduleIndex"] = static_cast<qint64>(moduleIndex);
        entry["hits"] = static_cast<qint64>(hits);

        QJsonObject sizes;
        sizes["min_words"] = static_cast<qint64>(sizeInfo.min);
        sizes["max_words"] = static_cast<qint64>(sizeInfo.max);
        sizes["avg_words"] = static_cast<double>(sizeInfo.sum) / static_cast<double>(counters.groupHits.at(indexes));
        entry["eventSizes"] = sizes;

        moduleHits.push_back(entry);
    }
    ret["moduleHits"] = moduleHits;

    return ret;
}

QJsonObject analysis_statistics_to_json(const Analysis &ana)
{
    QJsonObject ret;

    // module vme <-> ana infos
    // h1d stats from level 0 (raw histos)
    auto as = ana.getA2AdapterState();

    if (!as)
        return ret;

    auto id2indexes = ana.getVMEIdToIndexMapping();

    {
        QJsonArray destArray;

        for (auto src: ana.getSources())
        {
            if (auto ds = as->sourceMap.value(src.get(), nullptr);
                ds && ds->hitCounts)
            {
                auto &hitCounts = *ds->hitCounts;
                auto indexes = id2indexes.value(src->getModuleId());

                QJsonArray hitsArray;
                std::copy(std::begin(hitCounts), std::end(hitCounts), std::back_inserter(hitsArray));

                QJsonObject o;
                o["name"] = make_object_name_with_path(src);
                o["eventIndex"] = static_cast<qint64>(indexes.eventIndex);
                o["moduleIndex"] = static_cast<qint64>(indexes.moduleIndex);
                o["hits"] = hitsArray;
                destArray.push_back(o);
            }
        }

        ret["data_sources"] = destArray;
    }

    {
        QJsonArray destArray;

        auto sinks = ana.getSinkOperators();

        std::sort(std::begin(sinks), std::end(sinks),
            [] (const auto &s1, const auto &s2)
            {
                auto n1 = analysis::make_object_name_with_path(s1);
                auto n2 = analysis::make_object_name_with_path(s2);
                if (n1 == n2)
                    return s1->getRank() < s2->getRank();
                return n1 < n2;
            });

        for (auto sink_: sinks)
        {
            if (auto sink = qobject_cast<Histo1DSink *>(sink_.get());
                sink && sink->getUserLevel() == 0)
            {
                auto histos = sink->getHistos();

                auto f = [] (auto &accu, const auto &histo) { accu.emplace_back(histo->calcStatistics()); return accu; };

                auto histoStats = std::accumulate(
                    std::begin(histos), std::end(histos),
                    std::vector<Histo1DStatistics>{}, f);

                QJsonArray histoArray;
                for (const auto &stats: histoStats)
                {
                    QJsonObject o;
                    o["entryCount"] = stats.entryCount;
                    o["mean"] = stats.mean;
                    o["sigma"] = stats.sigma;
                    o["fwhmCenter"] = stats.fwhmCenter;
                    o["fwhm"] = stats.fwhm;
                    histoArray.append(o);
                }

                QJsonObject histoObj;
                histoObj["name"] = make_object_name_with_path(sink_);
                histoObj["histos"] = histoArray;

                destArray.push_back(histoObj);
            }
        }

        ret["raw_histograms"] = destArray;
    }

    return ret;
}

QJsonObject run_statistics_to_json(
    const mesytec::mvlc::readout_parser::ReadoutParserCounters &counters,
    const std::shared_ptr<analysis::Analysis> &analysis)
{
    QJsonObject crateJson;
    crateJson["readout_parser"] = to_qjson(counters);

    if (analysis)
        crateJson["analysis"] = analysis_statistics_to_json(*analysis);

    return crateJson;
}

std::pair<bool, QString> save_run_statistics_to_json(
    const RunInfo &runInfo,
    const QString &filename,
    const std::map<u8, mesytec::mvlc::readout_parser::ReadoutParserCounters> &parserCounters,
    const std::map<u8, std::shared_ptr<analysis::Analysis>> &analyses
    )
{
    QJsonArray crateStats;

    for (const auto &[crateId, counters]: parserCounters)
    {
        auto crateJson = run_statistics_to_json(counters, analyses.at(crateId));
        crateJson["crateId"] = static_cast<qint64>(crateId);
        crateStats.push_back(crateJson);
    }

    QJsonObject runInfoJson;
    runInfoJson["runId"] = runInfo.runId;
    runInfoJson["isReplay"] = runInfo.isReplay;
    runInfoJson["replaySourceFile"] = runInfo.infoDict["replaySourceFile"].toString();

    QJsonObject outerJson;

    outerJson["crateStats"] = crateStats;
    outerJson["runInfo"] = runInfoJson;

    QJsonDocument doc(outerJson);
    auto bytes = doc.toJson();

    QFile file(filename);

    if (!file.open(QIODevice::WriteOnly))
        return std::make_pair(false, file.errorString());

    if (file.write(bytes) != bytes.size())
        return std::make_pair(false, file.errorString());

    return std::make_pair(true, QString());
}

} // namespace analysis
