/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include "../template_system.h"

namespace analysis
{

QVector<std::shared_ptr<SourceInterface>> get_default_data_extractors(const QString &moduleTypeName)
{
    QVector<std::shared_ptr<SourceInterface>> result;

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
                if (auto extractor = std::dynamic_pointer_cast<Extractor>(source))
                    result.push_back(extractor);
                else if (auto extractor = std::dynamic_pointer_cast<ListFilterExtractor>(source))
                    result.push_back(extractor);
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

    std::sort(condInputSlots.begin(), condInputSlots.end(), slot_lessThan);

    SinkVector result;
    result.reserve(sinks.size());

    for (const auto &sink: sinks)
    {
        if (sink->getEventId() != cl.condition->getEventId())
            continue;

        if (sink->getNumberOfSlots() != condInputSlots.size())
            continue;

        auto sinkSlots = sink->getSlots();

        std::sort(sinkSlots.begin(), sinkSlots.end(), slot_lessThan);

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

std::vector<std::vector<std::string>> collect_multi_event_splitter_filter_strings(
    const VMEConfig &vmeConfig, const Analysis &analysis)
{
    const auto &eventConfigs = vmeConfig.getEventConfigs();

    std::vector<std::vector<std::string>> splitterFilters;

    for (const auto &eventConfig: eventConfigs)
    {
        std::vector<std::string> moduleSplitterFilters;
        auto eventSettings = analysis.getVMEObjectSettings(eventConfig->getId());
        bool enabledForEvent = eventSettings["MultiEventProcessing"].toBool();

        for (const auto &moduleConfig: eventConfig->getModuleConfigs())
        {
            auto moduleSettings = analysis.getVMEObjectSettings(moduleConfig->getId());
            auto filterString = moduleSettings.value("MultiEventHeaderFilter").toString();

            if (filterString.isEmpty())
                filterString = moduleConfig->getModuleMeta().eventHeaderFilter;

            if (enabledForEvent)
                moduleSplitterFilters.emplace_back(filterString.toStdString());
            else
                moduleSplitterFilters.emplace_back(std::string{});
        }

        splitterFilters.emplace_back(moduleSplitterFilters);
    }

    return splitterFilters;
}

void add_default_filters(Analysis *analysis, ModuleConfig *module)
{
    auto dataSources = get_default_data_extractors(module->getModuleMeta().typeName);

    QVector<std::shared_ptr<Extractor>> defaultFilters;
    QVector<std::shared_ptr<ListFilterExtractor>> defaultListFilters;

    for (auto source: dataSources)
    {
        if (auto extractor = std::dynamic_pointer_cast<Extractor>(source))
            defaultFilters.push_back(extractor);
        else if (auto listFilter = std::dynamic_pointer_cast<ListFilterExtractor>(source))
            defaultListFilters.push_back(listFilter);
    }

    auto add_missing_directory = [module, analysis] (
        const QString &name, const DisplayLocation &displayLocation, s32 userLevel)
        -> std::shared_ptr<Directory>
    {
        auto dir = analysis->getDirectory(module->getEventId(), displayLocation, name);

        if (!dir)
        {
            dir = std::make_shared<Directory>();
            dir->setEventId(module->getEventId());
            dir->setDisplayLocation(displayLocation);
            dir->setObjectName(name);
            dir->setUserLevel(userLevel);
            analysis->addDirectory(dir);
        }

        return dir;
    };

    const auto eventId = module->getEventId();

    // Directory for raw histograms
    auto dirRawHistos = add_missing_directory("Raw Histos " + module->objectName(), DisplayLocation::Sink, 0);
    // Directory for calibration operators for this module
    auto dirCalOperators = add_missing_directory("Cal " + module->objectName(), DisplayLocation::Operator, 1);
    // Directory for calibrated data histograms
    auto dirCalHistos = add_missing_directory("Cal Histos " + module->objectName(), DisplayLocation::Sink, 1);

    for (auto &ex: defaultFilters)
    {
        auto dataFilter = ex->getFilter();
        double unitMin = 0.0;
        double unitMax = std::pow(2.0, dataFilter.getDataBits());
        QString name = ex->objectName().section('.', 0, -1);

        RawDataDisplay rawDataDisplay = make_raw_data_display(
            dataFilter, unitMin, unitMax,
            name,
            ex->objectName().section('.', 0, -1),
            QString());

        // First add the operators using hardcoded userlevels.
        add_raw_data_display(analysis, eventId, module->getId(), rawDataDisplay);

        // Now add the operators to directories which sets the correct userlevel.
        dirRawHistos->push_back(rawDataDisplay.rawHistoSink);
        dirCalOperators->push_back(rawDataDisplay.calibration);
        dirCalHistos->push_back(rawDataDisplay.calibratedHistoSink);
    }

    for (auto &ex: defaultListFilters)
    {
        auto clone = std::dynamic_pointer_cast<ListFilterExtractor>(
            std::shared_ptr<AnalysisObject>(ex->clone()));

        if (!clone)
            continue;

        static const double MaxRawHistoBins = (1 << 16);
        const u32 addressBits = clone->getAddressBits();
        const u32 dataBits = clone->getDataBits();
        const double unitMin = 0.0;
        const double unitMax = std::pow(2.0, dataBits);
        QString name = ex->objectName().section('.', 0, -1);
        const u32 histoBins = static_cast<u32>(std::min(unitMax, MaxRawHistoBins));
        const u32 addressCount = 1u << addressBits;

        auto rawHistoSink = std::make_shared<Histo1DSink>();
        rawHistoSink->setObjectName(QString("%1_raw").arg(name));
        rawHistoSink->m_bins = histoBins;

        auto calibration = std::make_shared<CalibrationMinMax>();
        calibration->setObjectName(name);

        for (u32 addr = 0; addr < addressCount; ++addr)
            calibration->setCalibration(addr, unitMin, unitMax);

        auto calHistoSink = std::make_shared<Histo1DSink>();
        calHistoSink->setObjectName(QString("%1").arg(name));
        calHistoSink->m_bins = histoBins;

        rawHistoSink->connectArrayToInputSlot(0, clone->getOutput(0));
        calibration->connectArrayToInputSlot(0, clone->getOutput(0));
        calHistoSink->connectArrayToInputSlot(0, calibration->getOutput(0));

        // First add the operators using hardcoded userlevels.
        analysis->addSource(eventId, module->getId(), clone);
        analysis->addOperator(eventId, 0, rawHistoSink);
        analysis->addOperator(eventId, 1, calibration);
        analysis->addOperator(eventId, 1, calHistoSink);

        // Now add the operators to directories which sets the correct userlevel.
        dirRawHistos->push_back(rawHistoSink);
        dirCalOperators->push_back(calibration);
        dirCalHistos->push_back(calHistoSink);
    }

    analysis->beginRun(Analysis::KeepState);
}

} // namespace analysis

