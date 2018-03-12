/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "analysis.h"
#include <QJsonArray>
#include <QJsonObject>

#include <random>

#include "a2_adapter.h"
#include "a2/multiword_datafilter.h"
#include "../vme_config.h"

#define ENABLE_ANALYSIS_DEBUG 0

#define ANALYSIS_USE_SHARED_HISTO1D_MEM 1

template<typename T>
QDebug &operator<< (QDebug &dbg, const std::shared_ptr<T> &ptr)
{
    dbg << ptr.get();
    return dbg;
}

template<>
const QMap<analysis::Analysis::ReadResultCodes, const char *> analysis::Analysis::ReadResult::ErrorCodeStrings =
{
    { analysis::Analysis::NoError, "No Error" },
    { analysis::Analysis::VersionTooNew, "Version too new" },
};

namespace
{
using ArenaPtr = std::unique_ptr<memory::Arena>;

using analysis::A2AdapterState;
using analysis::Analysis;

/* Performs a2_adapter_build(), growing the arenas until the build succeeds. */
A2AdapterState a2_adapter_build_memory_wrapper(
    ArenaPtr &arena,
    ArenaPtr &workArena,
    const QVector<Analysis::SourceEntry> &sources,
    const QVector<Analysis::OperatorEntry> &operators,
    const vme_analysis_common::VMEIdToIndex &vmeMap)
{
    A2AdapterState result;

    while (true)
    {
        try
        {
            result = a2_adapter_build(
                arena.get(),
                workArena.get(),
                sources,
                operators,
                vmeMap);

            break;
        }
        catch (const memory::out_of_memory &)
        {
            /* Double the size and try again. std::bad_alloc() will be thrown
             * if we run OOM. This will be handled further up. */
            auto newSize = 2 * arena->size;
            arena = std::make_unique<memory::Arena>(newSize);
            workArena = std::make_unique<memory::Arena>(newSize);
        }
    }

    qDebug("%s a2: mem=%u sz=%u, start@%p",
           __FUNCTION__, (u32)arena->used(), (u32)arena->size, arena->mem);

    return result;
}

QJsonObject to_json(const a2::data_filter::DataFilter &filter)
{
    QJsonObject result;

    result["filterString"] = QString::fromStdString(to_string(filter));
    result["wordIndex"] = filter.matchWordIndex;

    return result;
}

a2::data_filter::DataFilter a2_dataFilter_from_json(const QJsonObject &json)
{
    return a2::data_filter::make_filter(
        json["filterString"].toString().toStdString(),
        json["wordIndex"].toInt());
}

QJsonObject to_json(const a2::data_filter::MultiWordFilter &filter)
{
    QJsonObject result;

    QJsonArray subFilterArray;

    for (s32 i = 0; i < filter.filterCount; i++)
    {
        const auto &subfilter = filter.filters[i];
        QJsonObject filterJson;
        filterJson["filterString"] = QString::fromStdString(to_string(subfilter));
        filterJson["wordIndex"] = subfilter.matchWordIndex;
        subFilterArray.append(filterJson);
    }

    result["subFilters"] = subFilterArray;


    return result;
}

a2::data_filter::MultiWordFilter a2_multiWordFilter_from_json(const QJsonObject &json)
{
    a2::data_filter::MultiWordFilter result = {};

    auto subFilterArray = json["subFilters"].toArray();

    for (auto it = subFilterArray.begin();
         it != subFilterArray.end();
         it++)
    {
        add_subfilter(&result, a2_dataFilter_from_json(it->toObject()));
    }

    return result;
}

QJsonObject to_json(const a2::data_filter::ListFilter &filter)
{
    QJsonObject result;

    result["extractionFilter"] = to_json(filter.extractionFilter);
    result["flags"] = static_cast<qint64>(filter.flags);
    result["wordCount"] = static_cast<qint64>(filter.wordCount);

    return result;
}

a2::data_filter::ListFilter a2_listfilter_from_json(const QJsonObject &json)
{
    using a2::data_filter::ListFilter;

    ListFilter result = {};

    result.extractionFilter = a2_multiWordFilter_from_json(json["extractionFilter"].toObject());
    result.flags = static_cast<ListFilter::Flag>(json["flags"].toInt());
    result.wordCount = static_cast<u8>(json["wordCount"].toInt());

    return result;
}

} // end anon namespace

namespace analysis
{
/* File versioning. If the format changes this version needs to be incremented
 * and a conversion routine has to be implemented.
 */
static const int CurrentAnalysisVersion = 2;

/* This function converts from analysis config versions prior to V2, which
 * stored eventIndex and moduleIndex instead of eventId and moduleId.
 */
static QJsonObject v1_to_v2(QJsonObject json, VMEConfig *vmeConfig)
{
    bool couldConvert = true;

    if (!vmeConfig)
    {
        // TODO: report error here
        return json;
    }

    // sources
    auto array = json["sources"].toArray();

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        auto objectJson = it->toObject();
        int eventIndex = objectJson["eventIndex"].toInt();
        int moduleIndex = objectJson["moduleIndex"].toInt();

        auto eventConfig = vmeConfig->getEventConfig(eventIndex);
        auto moduleConfig = vmeConfig->getModuleConfig(eventIndex, moduleIndex);

        if (eventConfig && moduleConfig)
        {
            objectJson["eventId"] = eventConfig->getId().toString();
            objectJson["moduleId"] = moduleConfig->getId().toString();
            *it = objectJson;
        }
        else
        {
            couldConvert = false;
        }
    }
    json["sources"] = array;

    // operators
    array = json["operators"].toArray();

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        auto objectJson = it->toObject();
        int eventIndex = objectJson["eventIndex"].toInt();

        auto eventConfig = vmeConfig->getEventConfig(eventIndex);

        if (eventConfig)
        {
            objectJson["eventId"] = eventConfig->getId().toString();
            *it = objectJson;
        }
        else
        {
            couldConvert = false;
        }
    }
    json["operators"] = array;

    if (!couldConvert)
    {
        // TODO: report this
        qDebug() << "Error converting to v2!!!================================================";
    }

    return json;
}

using VersionConverter = std::function<QJsonObject (QJsonObject, VMEConfig *)>;

static QVector<VersionConverter> VersionConverters =
{
    nullptr,
    v1_to_v2
};

static int get_version(const QJsonObject &json)
{
    return json[QSL("MVMEAnalysisVersion")].toInt(1);
};

static QJsonObject convert_to_current_version(QJsonObject json, VMEConfig *vmeConfig)
{
    int version;

    while ((version = get_version(json)) < CurrentAnalysisVersion)
    {
        auto converter = VersionConverters.value(version);

        if (!converter)
            break;

        json = converter(json, vmeConfig);
        json[QSL("MVMEAnalysisVersion")] = version + 1;

        qDebug() << __PRETTY_FUNCTION__ << "converted Analysis from version" << version << "to version" << version+1;
    }

    return json;
}

template<typename T>
QString getClassName(T *obj)
{
    return obj->metaObject()->className();
}

//
// Slot
//

void Slot::connectPipe(Pipe *newInput, s32 newParamIndex)
{
    disconnectPipe();
    if (newInput)
    {
        inputPipe = newInput;
        paramIndex = newParamIndex;
        inputPipe->addDestination(this);
        Q_ASSERT(parentOperator);
        parentOperator->slotConnected(this);
    }
}

void Slot::disconnectPipe()
{
    if (inputPipe)
    {
        inputPipe->removeDestination(this);
        inputPipe = nullptr;
        paramIndex = Slot::NoParamIndex;
        Q_ASSERT(parentOperator);
        parentOperator->slotDisconnected(this);
    }
}

//
// OperatorInterface
//
// FIXME: does not perform acceptedInputTypes validity test atm!
// FIXME: does not return a value atm!
void OperatorInterface::connectInputSlot(s32 slotIndex, Pipe *inputPipe, s32 paramIndex)
{
    Slot *slot = getSlot(slotIndex);

    if (slot)
    {
        slot->connectPipe(inputPipe, paramIndex);
    }
}

s32 OperatorInterface::getMaximumInputRank()
{
    s32 result = 0;

    for (s32 slotIndex = 0;
         slotIndex < getNumberOfSlots();
         ++slotIndex)
    {
        if (Slot *slot = getSlot(slotIndex))
        {
            if (slot->inputPipe)
                result = std::max(result, slot->inputPipe->getRank());
        }
    }

    return result;
}

s32 OperatorInterface::getMaximumOutputRank()
{
    s32 result = 0;

    for (s32 outputIndex = 0;
         outputIndex < getNumberOfOutputs();
         ++outputIndex)
    {
        if (Pipe *output = getOutput(outputIndex))
        {
            result = std::max(result, output->getRank());
        }
    }

    return result;
}

//
// Extractor
//

static std::uniform_real_distribution<double> RealDist01(0.0, 1.0);

Extractor::Extractor(QObject *parent)
    : SourceInterface(parent)
    , m_options(Options::NoOption)
{
    m_output.setSource(this);

    // Generate a random seed for the rng. This seed will be written out in
    // write() and restored in read().
    std::random_device rd;
    std::uniform_int_distribution<u64> dist;
    m_rngSeed = dist(rd);
}

void Extractor::beginRun(const RunInfo &runInfo)
{
    m_currentCompletionCount = 0;

    m_fastFilter = {};
    for (auto slowFilter: m_filter.getSubFilters())
    {
        auto subfilter = a2::data_filter::make_filter(slowFilter.getFilter().toStdString(), slowFilter.getWordIndex());
        add_subfilter(&m_fastFilter, subfilter);
    }

    u32 addressCount = 1u << get_extract_bits(&m_fastFilter, a2::data_filter::MultiWordFilter::CacheA);

    qDebug() << __PRETTY_FUNCTION__ << this << "addressCount" << addressCount;

    // The highest value the filter will yield is ((2^bits) - 1) but we're
    // adding a random in [0.0, 1.0) so the actual exclusive upper limit is
    // (2^bits).

    double upperLimit = std::pow(2.0, m_filter.getDataBits());

    auto &params(m_output.getParameters());
    params.resize(addressCount);

    for (s32 i=0; i<params.size(); ++i)
    {
        auto &param(params[i]);
        param.lowerLimit = 0.0;
        param.upperLimit = upperLimit;
    }

    params.name = this->objectName();

    m_rng.seed(m_rngSeed);

    {
        QMutexLocker lock(&m_hitCountsMutex);
        m_hitCounts.resize(addressCount);

        if (!runInfo.keepAnalysisState)
        {
            for (s32 i=0; i<m_hitCounts.size(); ++i)
            {
                m_hitCounts[i] = 0.0;
            }
        }
    }
}

void Extractor::beginEvent()
{
#if ENABLE_ANALYSIS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << this << objectName();
#endif

    clear_completion(&m_fastFilter);
    m_currentCompletionCount = 0;
    m_output.getParameters().invalidateAll();
}

s32 Extractor::getNumberOfOutputs() const
{
    return 1;
}

QString Extractor::getOutputName(s32 outputIndex) const
{
    return QSL("Extracted data array");
}

Pipe *Extractor::getOutput(s32 index)
{
    Pipe *result = nullptr;

    if (index == 0)
    {
        result = &m_output;
    }

    return result;
}

void Extractor::read(const QJsonObject &json)
{
    // If a seed was stored reuse it, otherwise stick with the one generated in
    // the constructor.
    if (json.contains("rngSeed"))
    {
        // Need the full 64-bits which QJsonValue::toInt() does not provide.
        // Storing as double will lead to loss of precision which effectively
        // truncates the seed.
        // Instead the seed is stored as a string and then parsed back to u64.
        QString sSeed = json["rngSeed"].toString();
        m_rngSeed = sSeed.toULongLong(nullptr, 16);

        // convert back and compare the strings
        Q_ASSERT(sSeed == QString::number(m_rngSeed, 16));
    }

    m_filter = MultiWordDataFilter();

    auto filterArray = json["subFilters"].toArray();

    for (auto it=filterArray.begin();
         it != filterArray.end();
         ++it)
    {
        auto filterJson = it->toObject();
        auto filterString = filterJson["filterString"].toString().toLocal8Bit();
        auto wordIndex    = filterJson["wordIndex"].toInt(-1);
        DataFilter filter = makeFilterFromBytes(filterString, wordIndex);
        m_filter.addSubFilter(filter);
    }

    setRequiredCompletionCount(static_cast<u32>(json["requiredCompletionCount"].toInt()));
    m_options = static_cast<Options::opt_t>(json["options"].toInt());
}

void Extractor::write(QJsonObject &json) const
{
    json["rngSeed"] = QString::number(m_rngSeed, 16);

    QJsonArray filterArray;
    const auto &subFilters(m_filter.getSubFilters());
    for (const auto &dataFilter: subFilters)
    {
        QJsonObject filterJson;
        filterJson["filterString"] = QString::fromLocal8Bit(dataFilter.getFilter());
        filterJson["wordIndex"] = dataFilter.getWordIndex();
        filterArray.append(filterJson);
    }

    json["subFilters"] = filterArray;
    json["requiredCompletionCount"] = static_cast<qint64>(m_requiredCompletionCount);
    json["options"] = static_cast<s32>(m_options);
}

QVector<double> Extractor::getHitCounts() const
{
    QMutexLocker lock(&m_hitCountsMutex);
    return m_hitCounts;
}

//
// ListFilterExtractor
//
ListFilterExtractor::ListFilterExtractor(QObject *parent)
    : SourceInterface(parent)
{
    m_output.setSource(this);
    m_a2Extractor = {};
    m_a2Extractor.options = a2::DataSourceOptions::NoAddedRandom;
    std::random_device rd;
    std::uniform_int_distribution<u64> dist;
    m_rngSeed = dist(rd);
}

void ListFilterExtractor::beginRun(const RunInfo &runInfo)
{
    u32 addressCount = get_address_count(&m_a2Extractor);
    u32 dataBits = get_extract_bits(&m_a2Extractor.listFilter, a2::data_filter::MultiWordFilter::CacheD);
    double upperLimit = std::pow(2.0, dataBits);

    auto &params(m_output.getParameters());
    params.resize(addressCount);

    for (s32 i=0; i<params.size(); ++i)
    {
        auto &param(params[i]);
        param.lowerLimit = 0.0;
        param.upperLimit = upperLimit;
    }

    params.name = this->objectName();
}

void ListFilterExtractor::beginEvent()
{
    m_output.getParameters().invalidateAll();
}

void ListFilterExtractor::write(QJsonObject &json) const
{
    json["listFilter"] = to_json(m_a2Extractor.listFilter);
    json["repetitionAddressFilter"] = to_json(m_a2Extractor.repetitionAddressFilter);
    json["repetitions"] = static_cast<qint64>(m_a2Extractor.repetitions);
    json["rngSeed"] = QString::number(m_rngSeed, 16);
    json["options"] = static_cast<s32>(m_a2Extractor.options);
}

void ListFilterExtractor::read(const QJsonObject &json)
{
    m_a2Extractor = {};
    m_a2Extractor.listFilter = a2_listfilter_from_json(json["listFilter"].toObject());
    m_a2Extractor.repetitionAddressFilter = a2_dataFilter_from_json(json["repetitionAddressFilter"].toObject());
    m_a2Extractor.repetitionAddressCache = make_cache_entry(m_a2Extractor.repetitionAddressFilter, 'A');
    m_a2Extractor.repetitions = static_cast<u8>(json["repetitions"].toInt());
    QString sSeed = json["rngSeed"].toString();
    m_rngSeed = sSeed.toULongLong(nullptr, 16);
    m_a2Extractor.options = static_cast<Options::opt_t>(json["options"].toInt());
}

//
// BasicOperator
//
BasicOperator::BasicOperator(QObject *parent)
    : OperatorInterface(parent)
    , m_inputSlot(this, 0, QSL("Input"))
{
    m_output.setSource(this);
}

BasicOperator::~BasicOperator()
{
}

s32 BasicOperator::getNumberOfSlots() const
{
    return 1;
}

Slot *BasicOperator::getSlot(s32 slotIndex)
{
    Slot *result = nullptr;
    if (slotIndex == 0)
    {
        result = &m_inputSlot;
    }
    return result;
}

s32 BasicOperator::getNumberOfOutputs() const
{
    return 1;
}

QString BasicOperator::getOutputName(s32 outputIndex) const
{
    if (outputIndex == 0)
    {
        return QSL("Output");
    }
    return QString();

}

Pipe *BasicOperator::getOutput(s32 index)
{
    Pipe *result = nullptr;
    if (index == 0)
    {
        result = &m_output;
    }

    return result;
}

//
// BasicSink
//
BasicSink::BasicSink(QObject *parent)
    : SinkInterface(parent)
    , m_inputSlot(this, 0, QSL("Input"))
{
}

BasicSink::~BasicSink()
{
}

s32 BasicSink::getNumberOfSlots() const
{
    return 1;
}

Slot *BasicSink::getSlot(s32 slotIndex)
{
    Slot *result = nullptr;
    if (slotIndex == 0)
    {
        result = &m_inputSlot;
    }
    return result;
}

//
// CalibrationMinMax
//
CalibrationMinMax::CalibrationMinMax(QObject *parent)
    : BasicOperator(parent)
{
}

void CalibrationMinMax::beginRun(const RunInfo &)
{
    auto &out(m_output.getParameters());

    out.name = objectName();
    out.unit = getUnitLabel();

    if (m_inputSlot.inputPipe)
    {
        const auto &in(m_inputSlot.inputPipe->getParameters());

        s32 idxMin = 0;
        s32 idxMax = in.size();

        if (m_inputSlot.paramIndex != Slot::NoParamIndex)
        {
            out.resize(1);
            idxMin = m_inputSlot.paramIndex;
            idxMax = idxMin + 1;
        }
        else
        {
            out.resize(in.size());
        }

        // shrink
        if (m_calibrations.size() > out.size())
        {
            m_calibrations.resize(out.size());
        }

        s32 outIdx = 0;
        for (s32 idx = idxMin; idx < idxMax; ++idx)
        {
            Parameter &outParam(out[outIdx++]);

            // Hack to make things compatible with old configs. This forces
            // expanding the calibrations array if it is too small. This
            // way it will be written to file the next time the config is
            // saved.
            if (idx >= m_calibrations.size())
            {
                setCalibration(idx, {m_oldGlobalUnitMin, m_oldGlobalUnitMax});
            }

            // assign output limits
            auto calib = getCalibration(idx);

            outParam.lowerLimit = calib.unitMin;
            outParam.upperLimit = calib.unitMax;
        }
    }
    else
    {
        out.resize(0);
    }
}

void CalibrationMinMax::step()
{
    auto calibOneParam = [](const Parameter &inParam, Parameter &outParam, const CalibrationMinMaxParameters &calib)
    {
        outParam.valid = inParam.valid;
        if (inParam.valid)
        {
            double a1 = inParam.lowerLimit;
            double a2 = inParam.upperLimit;
            double t1 = calib.unitMin;
            double t2 = calib.unitMax;

            Q_ASSERT(a1 - a2 != 0.0);
            Q_ASSERT(t1 - t2 != 0.0);

            //if (std::abs(a1) > std::abs(a2))
            //    std::swap(a1, a2);

            //if (std::abs(t1) > std::abs(t2))
            //    std::swap(t1, t2);

            outParam.value = (inParam.value - a1) * (t2 - t1) / (a2 - a1) + t1;
        }
    };

    if (m_inputSlot.inputPipe)
    {
        auto &out(m_output.getParameters());
        const auto &in(m_inputSlot.inputPipe->getParameters());
        const s32 inSize = in.size();

        s32 idxMin = 0;
        s32 idxMax = in.size();

        if (m_inputSlot.paramIndex != Slot::NoParamIndex)
        {
            Q_ASSERT(out.size() == 1);
            idxMin = m_inputSlot.paramIndex;
            idxMax = idxMin + 1;
        }
        else
        {
            Q_ASSERT(out.size() == in.size());
        }

        for (s32 idx = idxMin, outIdx = 0;
             idx < idxMax;
             ++idx, ++outIdx)
        {
            auto &outParam(out[outIdx]);

            if (idx < inSize)
            {
                const auto &inParam(in[idx]);
                calibOneParam(inParam, outParam, getCalibration(idx));
            }
            else
            {
                outParam.valid = false;
            }
        }
    }
}

void CalibrationMinMax::setCalibration(s32 address, const CalibrationMinMaxParameters &params)
{
    m_calibrations.resize(std::max(m_calibrations.size(), address+1));
    m_calibrations[address] = params;
}

CalibrationMinMaxParameters CalibrationMinMax::getCalibration(s32 address) const
{
    if (address < m_calibrations.size())
        return m_calibrations.at(address);

    // Support loading of legacy configs which had a calibrations array of zero
    // length but the global values set in case of inputs with size 1.
    return { m_oldGlobalUnitMin, m_oldGlobalUnitMax };
}

void CalibrationMinMax::read(const QJsonObject &json)
{
    m_unit = json["unitLabel"].toString();

    // Read the old global calbration and use it if an element of the
    // calibration array is invalid.
    m_oldGlobalUnitMin = json["globalUnitMin"].toDouble(make_quiet_nan());
    m_oldGlobalUnitMax = json["globalUnitMax"].toDouble(make_quiet_nan());

    m_calibrations.clear();
    QJsonArray calibArray = json["calibrations"].toArray();

    for (auto it=calibArray.begin();
         it != calibArray.end();
         ++it)
    {
        auto paramJson = it->toObject();


        /* TODO: There's a bug in the write code and/or the code that should
         * resize m_calibrations on changes to the input: Empty entries appear
         * at the end of the list of calibration parameters inside the
         * generated json. The test here skips those. */
        if (paramJson.contains("unitMin"))
        {
            CalibrationMinMaxParameters param;

            param.unitMin = paramJson["unitMin"].toDouble(make_quiet_nan());
            param.unitMax = paramJson["unitMax"].toDouble(make_quiet_nan());

            if (!param.isValid())
            {
                param.unitMin = m_oldGlobalUnitMin;
                param.unitMax = m_oldGlobalUnitMax;
            }

            m_calibrations.push_back(param);
        }
    }
}

void CalibrationMinMax::write(QJsonObject &json) const
{
    json["unitLabel"] = m_unit;

    QJsonArray calibArray;

    for (auto &param: m_calibrations)
    {
        QJsonObject paramJson;
        if (param.isValid())
        {
            paramJson["unitMin"] = param.unitMin;
            paramJson["unitMax"] = param.unitMax;
        }
        calibArray.append(paramJson);
    }

    json["calibrations"] = calibArray;
}
//
// IndexSelector
//
IndexSelector::IndexSelector(QObject *parent)
    : BasicOperator(parent)
{
    m_inputSlot.acceptedInputTypes = InputType::Array;
}

void IndexSelector::beginRun(const RunInfo &)
{
    auto &out(m_output.getParameters());


    if (m_inputSlot.inputPipe)
    {
        const auto &in(m_inputSlot.inputPipe->getParameters());

        out.resize(1);
        out.name = objectName();// in.name;
        out.unit = in.unit;
    }
    else
    {
        out.resize(0);
        out.name = QString();
        out.unit = QString();
    }
}

void IndexSelector::step()
{
    if (m_inputSlot.inputPipe)
    {
        auto &out(m_output.getParameters());
        const auto &in(m_inputSlot.inputPipe->getParameters());

        out[0].valid = false;

        if (m_index < in.size())
        {
            out[0] = in[m_index];
        }
    }
}

void IndexSelector::read(const QJsonObject &json)
{
    m_index = json["index"].toInt();
}

void IndexSelector::write(QJsonObject &json) const
{
    json["index"] = m_index;
}

//
// PreviousValue
//
PreviousValue::PreviousValue(QObject *parent)
    : BasicOperator(parent)
{
    m_inputSlot.acceptedInputTypes = InputType::Both;
}

void PreviousValue::beginRun(const RunInfo &)
{
    auto &out(m_output.getParameters());

    if (m_inputSlot.inputPipe)
    {
        const auto &in(m_inputSlot.inputPipe->getParameters());

        s32 idxMin = 0;
        s32 idxMax = in.size();

        if (m_inputSlot.paramIndex != Slot::NoParamIndex)
        {
            idxMin = m_inputSlot.paramIndex;
            idxMax = idxMin + 1;
        }

        out.resize(idxMax - idxMin);
        out.invalidateAll();
        out.name = objectName();// in.name;
        out.unit = in.unit;

        if (idxMin >= in.size())
        {
            return;
        }

        m_previousInput.resize(idxMax - idxMin);
        m_previousInput.invalidateAll();

        for (s32 idx = idxMin, outIdx = 0;
             idx < idxMax;
             ++idx, ++outIdx)
        {
            const Parameter &inParam(in[idx]);
            Parameter &outParam(out[outIdx]);

            outParam.lowerLimit = inParam.lowerLimit;
            outParam.upperLimit = inParam.upperLimit;
        }
    }
    else
    {
        out.resize(0);
        out.name = QString();
        out.unit = QString();
    }
}

void PreviousValue::step()
{
    if (m_inputSlot.inputPipe)
    {
        auto &out(m_output.getParameters());
        const auto &in(m_inputSlot.inputPipe->getParameters());

        s32 minIdx = 0;
        s32 maxIdx = out.size();
        s32 paramIndex = (m_inputSlot.paramIndex == Slot::NoParamIndex ? 0 : m_inputSlot.paramIndex);

        // Copy elements instead of assigning the vector directly as others
        // (e.g. the PipeDisplay widget) may keep temporary references to our
        // output vector and those would be invalidated on assignment!
        for (s32 idx = minIdx; idx < maxIdx; ++idx, ++paramIndex)
        {
            out[idx] = m_previousInput[paramIndex];
        }

        paramIndex = (m_inputSlot.paramIndex == Slot::NoParamIndex ? 0 : m_inputSlot.paramIndex);

        for (s32 idx = minIdx; idx < maxIdx; ++idx, ++paramIndex)
        {
            const Parameter &inParam(in[paramIndex]);

            if (!m_keepValid || inParam.valid)
            {
                m_previousInput[idx] = inParam;
            }
        }
    }
}

void PreviousValue::read(const QJsonObject &json)
{
    m_keepValid = json["keepValid"].toBool();
}

void PreviousValue::write(QJsonObject &json) const
{
    json["keepValid"] = m_keepValid;
}

//
// RetainValid
//
RetainValid::RetainValid(QObject *parent)
    : BasicOperator(parent)
{
    m_inputSlot.acceptedInputTypes = InputType::Both;
}

void RetainValid::beginRun(const RunInfo &)
{
    auto &out(m_output.getParameters());

    if (m_inputSlot.inputPipe)
    {
        const auto &in(m_inputSlot.inputPipe->getParameters());

        s32 idxMin = 0;
        s32 idxMax = in.size();

        if (m_inputSlot.paramIndex != Slot::NoParamIndex)
        {
            out.resize(1);
            idxMin = m_inputSlot.paramIndex;
            idxMax = idxMin + 1;
        }
        else
        {
            out.resize(in.size());
        }

        out.invalidateAll();
        out.name = objectName();// in.name;
        out.unit = in.unit;

        for (s32 idx = idxMin, outIdx = 0;
             idx < idxMax;
             ++idx, ++outIdx)
        {
            const Parameter &inParam(in[idx]);
            Parameter &outParam(out[outIdx]);

            outParam.lowerLimit = inParam.lowerLimit;
            outParam.upperLimit = inParam.upperLimit;
        }
    }
    else
    {
        out.resize(0);
        out.name = QString();
        out.unit = QString();
    }
}

void RetainValid::step()
{
    if (m_inputSlot.inputPipe)
    {
        auto &out(m_output.getParameters());
        const auto &in(m_inputSlot.inputPipe->getParameters());

        s32 paramIndex = m_inputSlot.paramIndex;

        if (paramIndex != Slot::NoParamIndex)
        {
            Q_ASSERT(paramIndex >= 0 && paramIndex < in.size());

            if (in[paramIndex].valid)
            {
                out[0] = in[paramIndex];
            }
        }
        else
        {
            const s32 size = in.size();

            for (s32 address = 0; address < size; ++address)
            {
                auto &outParam(out[address]);
                const auto &inParam(in[address]);

                if (inParam.valid)
                {
                    outParam = inParam;
                }
            }
        }
    }
}

void RetainValid::read(const QJsonObject &json)
{
}

void RetainValid::write(QJsonObject &json) const
{
}

//
// Difference
//
Difference::Difference(QObject *parent)
    : OperatorInterface(parent)
    , m_inputA(this, 0, QSL(" A"))
    , m_inputB(this, 1, QSL("-B"))
{
    m_output.setSource(this);
}

void Difference::slotConnected(Slot *slot)
{
    Q_ASSERT(slot == &m_inputA || slot == &m_inputB);

    if (slot->paramIndex != Slot::NoParamIndex)
    {
        m_inputA.acceptedInputTypes = InputType::Value;
        m_inputB.acceptedInputTypes = InputType::Value;
    }
    else
    {
        m_inputA.acceptedInputTypes = InputType::Array;
        m_inputB.acceptedInputTypes = InputType::Array;
    }
}

void Difference::slotDisconnected(Slot *slot)
{
    Q_ASSERT(slot == &m_inputA || slot == &m_inputB);

    if (!m_inputA.isConnected() && !m_inputB.isConnected())
    {
        m_inputA.acceptedInputTypes = InputType::Both;
        m_inputB.acceptedInputTypes = InputType::Both;
    }
}

void Difference::beginRun(const RunInfo &)
{
    m_output.parameters.name = objectName(); // QSL("A-B");
    m_output.parameters.unit = QString();

    if (!m_inputA.isParamIndexInRange() || !m_inputB.isParamIndexInRange())
    {
        m_output.parameters.resize(0);
        return;
    }

    // Either both inputs are arrays or both are single values
    Q_ASSERT((m_inputA.paramIndex == Slot::NoParamIndex && m_inputB.paramIndex == Slot::NoParamIndex)
             || (m_inputA.paramIndex != Slot::NoParamIndex && m_inputB.paramIndex != Slot::NoParamIndex));

    m_output.parameters.unit = m_inputA.inputPipe->parameters.unit;

    if (m_inputA.paramIndex != Slot::NoParamIndex && m_inputB.paramIndex != Slot::NoParamIndex)
    {
        // Both inputs are single values
        m_output.parameters.resize(1);
        auto &out(m_output.parameters[0]);
        const auto &inA(m_inputA.inputPipe->parameters[m_inputA.paramIndex]);
        const auto &inB(m_inputB.inputPipe->parameters[m_inputB.paramIndex]);
        out.lowerLimit = inA.lowerLimit - inB.upperLimit;
        out.upperLimit = inA.upperLimit - inB.lowerLimit;
    }
    else if (m_inputA.paramIndex == Slot::NoParamIndex && m_inputB.paramIndex == Slot::NoParamIndex)
    {
        // Both inputs are arrays
        s32 minSize = std::numeric_limits<s32>::max();
        minSize = std::min(minSize, m_inputA.inputPipe->parameters.size());
        minSize = std::min(minSize, m_inputB.inputPipe->parameters.size());
        m_output.parameters.resize(minSize);
        for (s32 idx = 0; idx < minSize; ++idx)
        {
            auto &out(m_output.parameters[idx]);
            const auto &inA(m_inputA.inputPipe->parameters[idx]);
            const auto &inB(m_inputB.inputPipe->parameters[idx]);

            out.lowerLimit = inA.lowerLimit - inB.upperLimit;
            out.upperLimit = inA.upperLimit - inB.lowerLimit;
        }
    }
    else
    {
        InvalidCodePath;
    }
}

void Difference::step()
{
    if (!m_inputA.isParamIndexInRange() || !m_inputB.isParamIndexInRange())
        return;

    if (m_inputA.paramIndex != Slot::NoParamIndex && m_inputB.paramIndex != Slot::NoParamIndex)
    {
        // Both inputs are single values
        auto &out(m_output.parameters[0]);
        const auto &inA(m_inputA.inputPipe->parameters[m_inputA.paramIndex]);
        const auto &inB(m_inputB.inputPipe->parameters[m_inputB.paramIndex]);
        out.valid = (inA.valid && inB.valid);
        if (out.valid)
        {
            out.value = inA.value - inB.value;
        }
    }
    else if (m_inputA.paramIndex == Slot::NoParamIndex && m_inputB.paramIndex == Slot::NoParamIndex)
    {
        // Both inputs are arrays
        const auto &paramsA(m_inputA.inputPipe->parameters);
        const auto &paramsB(m_inputB.inputPipe->parameters);
        auto &paramsOut(m_output.parameters);

        s32 maxIdx = paramsOut.size();
        for (s32 idx = 0; idx < maxIdx; ++idx)
        {
            paramsOut[idx].valid = (paramsA[idx].valid && paramsB[idx].valid);
            if (paramsOut[idx].valid)
            {
                paramsOut[idx].value = paramsA[idx].value - paramsB[idx].value;
            }
        }
    }
    else
    {
        InvalidCodePath;
    }
}

void Difference::read(const QJsonObject &json)
{
}

void Difference::write(QJsonObject &json) const
{
}

//
// Sum
//
Sum::Sum(QObject *parent)
    : BasicOperator(parent)
{
    m_inputSlot.acceptedInputTypes = InputType::Array;
}

void Sum::beginRun(const RunInfo &)
{
    auto &out(m_output.getParameters());

    if (m_inputSlot.inputPipe)
    {
        out.resize(1);

        const auto &in(m_inputSlot.inputPipe->getParameters());
        out.name = objectName();// in.name;
        out.unit = in.unit;

        double lowerBound = 0.0;
        double upperBound = 0.0;

        for (s32 i = 0; i < in.size(); ++i)
        {
            const auto &param(in[i]);

            lowerBound += std::min(param.lowerLimit, param.upperLimit);
            upperBound += std::max(param.lowerLimit, param.upperLimit);
        }

        if (m_calculateMean)
        {
            lowerBound /= in.size();
            upperBound /= in.size();
        }

        out[0].lowerLimit = std::min(lowerBound, upperBound);
        out[0].upperLimit = std::max(lowerBound, upperBound);
    }
    else
    {
        out.resize(0);
        out.name = QString();
        out.unit = QString();
    }
}

void Sum::step()
{
    if (m_inputSlot.inputPipe)
    {
        auto &outParam(m_output.getParameters()[0]);
        const auto &in(m_inputSlot.inputPipe->getParameters());

        outParam.value = 0.0;
        outParam.valid = false;
        s32 validCount = 0;

        for (s32 i = 0; i < in.size(); ++i)
        {
            const auto &inParam(in[i]);

            if (inParam.valid)
            {
                outParam.value += inParam.value;
                outParam.valid = true;
                ++validCount;
            }
        }

        if (m_calculateMean)
        {
            if (validCount > 0)
            {
                outParam.value /= validCount;
            }
            else
            {
                outParam.valid = false;
            }
        }
    }
}

void Sum::read(const QJsonObject &json)
{
    m_calculateMean = json["isMean"].toBool();
}

void Sum::write(QJsonObject &json) const
{
    json["isMean"] = m_calculateMean;
}

//
// AggregateOps
//

// FIXME: AggregateOps implementation is horrible

static QString aggregateOp_to_string(AggregateOps::Operation op)
{
    switch (op)
    {
        case AggregateOps::Op_Sum:
            return QSL("sum");
        case AggregateOps::Op_Mean:
            return QSL("mean");
        case AggregateOps::Op_Sigma:
            return QSL("sigma");
        case AggregateOps::Op_Min:
            return QSL("min");
        case AggregateOps::Op_Max:
            return QSL("max");
        case AggregateOps::Op_Multiplicity:
            return QSL("multiplicity");
        case AggregateOps::Op_MinX:
            return QSL("maxx");
        case AggregateOps::Op_MaxX:
            return QSL("maxy");
        case AggregateOps::Op_MeanX:
            return QSL("meanx");
        case AggregateOps::Op_SigmaX:
            return QSL("sigmax");

        case AggregateOps::NumOps:
            break;
    }
    return {};
}

static AggregateOps::Operation aggregateOp_from_string(const QString &str)
{
    if (str == QSL("sum"))
        return AggregateOps::Op_Sum;

    if (str == QSL("mean"))
        return AggregateOps::Op_Mean;

    if (str == QSL("sigma"))
        return AggregateOps::Op_Sigma;

    if (str == QSL("min"))
        return AggregateOps::Op_Min;

    if (str == QSL("max"))
        return AggregateOps::Op_Max;

    if (str == QSL("multiplicity"))
        return AggregateOps::Op_Multiplicity;

    if (str == QSL("maxx"))
        return AggregateOps::Op_MinX;

    if (str == QSL("maxy"))
        return AggregateOps::Op_MaxX;

    if (str == QSL("meanx"))
        return AggregateOps::Op_MeanX;

    if (str == QSL("sigmax"))
        return AggregateOps::Op_SigmaX;

    return AggregateOps::Op_Sum;
}

AggregateOps::AggregateOps(QObject *parent)
    : BasicOperator(parent)
{
    m_inputSlot.acceptedInputTypes = InputType::Array;
}

// FIXME: min and max thresholds are not taken into account when calculating
// the output lower and upper limits!
void AggregateOps::beginRun(const RunInfo &runInfo)
{
    auto &out(m_output.getParameters());

    if (m_inputSlot.inputPipe)
    {
        const auto &in(m_inputSlot.inputPipe->getParameters());

        out.resize(1);
        out.name = objectName();
        out.unit = m_outputUnitLabel.isEmpty() ? in.unit : m_outputUnitLabel;

        double lowerBound = 0.0;
        double upperBound = 0.0;

        switch (m_op)
        {
            case Op_Multiplicity:
                {
                    lowerBound = 0.0;
                    upperBound = in.size();
                } break;

            case Op_Sigma: // FIXME: sigma bounds
            case Op_Min:
            case Op_Max:
                {
                    double llMin = std::numeric_limits<double>::max();
                    double ulMax = std::numeric_limits<double>::lowest();

                    for (s32 i = 0; i < in.size(); ++i)
                    {
                        const auto &param(in[i]);

                        llMin = std::min(llMin, std::min(param.lowerLimit, param.upperLimit));
                        ulMax = std::max(ulMax, std::max(param.lowerLimit, param.upperLimit));
                    }

                    if (m_op == Op_Sigma)
                    {
                        lowerBound = 0.0;
                        upperBound = std::sqrt(ulMax - llMin);
                    }
                    else
                    {
                        lowerBound = llMin;
                        upperBound = ulMax;
                    }
                } break;

            case Op_Sum:
            case Op_Mean:
                {
                    for (s32 i = 0; i < in.size(); ++i)
                    {
                        const auto &param(in[i]);

                        lowerBound += std::min(param.lowerLimit, param.upperLimit);
                        upperBound += std::max(param.lowerLimit, param.upperLimit);
                    }

                    if (m_op == Op_Mean)
                    {
                        lowerBound /= in.size();
                        upperBound /= in.size();
                    }
                } break;

            case Op_MinX:
            case Op_MaxX:
            case Op_SigmaX: // FIXME: sigma bounds
            case Op_MeanX:
                {
                    lowerBound = 0.0;
                    upperBound = in.size();
                } break;

            case NumOps:
                break;
        }

        out[0].lowerLimit = std::min(lowerBound, upperBound);
        out[0].upperLimit = std::max(lowerBound, upperBound);
    }
    else
    {
        out.resize(0);
        out.name = QString();
        out.unit = QString();
    }
}

void AggregateOps::step()
{
    // validity check and threshold tests
    auto is_valid_and_inside = [](const auto param, double tmin, double tmax)
    {
        return (param.valid
                && (std::isnan(tmin) || param.value >= tmin)
                && (std::isnan(tmax) || param.value <= tmax));
    };

    if (!m_inputSlot.inputPipe)
        return;

    auto &outParam(m_output.getParameters()[0]);
    const auto &in(m_inputSlot.inputPipe->getParameters());

    if (m_op == Op_Min)
    {
        outParam.value = std::numeric_limits<double>::max();
    }
    else if (m_op == Op_Max)
    {
        outParam.value = std::numeric_limits<double>::lowest();
    }
    else
    {
        outParam.value = 0.0;
    }

    outParam.valid = false;
    u32 validCount = 0;

    s32 minMaxIndex = 0; // stores index of min/max value for Op_MinX/Op_MaxX
    double meanX = 0.0;
    double meanXEntryCount = 0.0;

    for (s32 i = 0; i < in.size(); ++i)
    {
        const auto &inParam(in[i]);

        if (is_valid_and_inside(inParam, m_minThreshold, m_maxThreshold))
        {
            ++validCount;

            if (m_op == Op_Sum || m_op == Op_Mean || m_op == Op_Sigma)
            {
                outParam.value += inParam.value;
            }
            else if (m_op == Op_Min)
            {
                outParam.value = std::min(outParam.value, inParam.value);
            }
            else if (m_op == Op_Max)
            {
                outParam.value = std::max(outParam.value, inParam.value);
            }
            else if (m_op == Op_MinX)
            {
                if (inParam.value < in[minMaxIndex].value)
                {
                    minMaxIndex = i;
                }
            }
            else if (m_op == Op_MaxX)
            {
                if (inParam.value > in[minMaxIndex].value)
                {
                    minMaxIndex = i;
                }
            }
            else if (m_op == Op_MeanX || m_op == Op_SigmaX)
            {
                meanX += inParam.value * i;
                meanXEntryCount += inParam.value;
            }
        }
    }

    outParam.valid = (validCount > 0);

    if ((m_op == Op_Mean || m_op == Op_Sigma) && outParam.valid)
    {
        outParam.value /= validCount; // mean

        if (m_op == Op_Sigma && outParam.value != 0.0)
        {
            double mu = outParam.value;
            double sigma = 0.0;

            for (s32 i = 0; i < in.size(); ++i)
            {
                const auto &inParam(in[i]);
                if (is_valid_and_inside(inParam, m_minThreshold, m_maxThreshold))
                {
                    double d = inParam.value - mu;
                    d *= d;
                    sigma += d;
                }
            }
            sigma = std::sqrt(sigma / validCount);
            outParam.value = sigma;
        }
    }
    else if (m_op == Op_Multiplicity)
    {
        outParam.value = validCount;
        outParam.valid = true;
    }
    else if (m_op == Op_MinX || m_op == Op_MaxX)
    {
        outParam.value = minMaxIndex;
    }
    else if (m_op == Op_MeanX || m_op == Op_SigmaX)
    {
        outParam.valid = true;

        if (meanXEntryCount != 0.0)
        {
            meanX /= meanXEntryCount;

            if (m_op == Op_MeanX)
            {
                outParam.value = meanX;
            }
            else if (m_op == Op_SigmaX)
            {
                double sigma = 0.0;

                if (meanX != 0.0)
                {
                    for (s32 i = 0; i < in.size(); ++i)
                    {
                        const auto &inParam(in[i]);
                        if (is_valid_and_inside(inParam, m_minThreshold, m_maxThreshold))
                        {
                            double v = inParam.value;
                            if (v != 0.0)
                            {
                                double d = i - meanX;
                                d *= d;
                                sigma += d * v;
                            }
                        }
                    }
                    sigma = sqrt(sigma / meanXEntryCount);
                }

                outParam.value = sigma;
            }
        }
        else
        {
            outParam.value = 0.0;
        }
    }
}

void AggregateOps::read(const QJsonObject &json)
{
    m_op = aggregateOp_from_string(json["operation"].toString());
    m_minThreshold = json["minThreshold"].toDouble(make_quiet_nan());
    m_maxThreshold = json["maxThreshold"].toDouble(make_quiet_nan());
    m_outputUnitLabel = json["outputUnitLabel"].toString();
}

void AggregateOps::write(QJsonObject &json) const
{
    json["operation"] = aggregateOp_to_string(m_op);
    json["minThreshold"] = m_minThreshold;
    json["maxThreshold"] = m_maxThreshold;
    json["outputUnitLabel"] = m_outputUnitLabel;
}

QString AggregateOps::getDisplayName() const
{
    return QSL("Aggregate Operations");
}

QString AggregateOps::getShortName() const
{
    if (m_op == AggregateOps::Op_Multiplicity)
        return QSL("Mult");

    return getOperationName(m_op);
}

void AggregateOps::setOperation(Operation op)
{
    m_op = op;
}

AggregateOps::Operation AggregateOps::getOperation() const
{
    return m_op;
}

void AggregateOps::setMinThreshold(double t)
{
    m_minThreshold = t;
}

double AggregateOps::getMinThreshold() const
{
    return m_minThreshold;
}

void AggregateOps::setMaxThreshold(double t)
{
    m_maxThreshold = t;
}

double AggregateOps::getMaxThreshold() const
{
    return m_maxThreshold;
}

QString AggregateOps::getOperationName(Operation op)
{
    switch (op)
    {
        case Op_Sum:
            return QSL("Sum");
        case Op_Mean:
            return QSL("Mean");
        case Op_Sigma:
            return QSL("Sigma");
        case Op_Min:
            return QSL("Min");
        case Op_Max:
            return QSL("Max");
        case Op_Multiplicity:
            return QSL("Multiplicity");
        case AggregateOps::Op_MinX:
            return QSL("MinX");
        case AggregateOps::Op_MaxX:
            return QSL("MaxX");
        case AggregateOps::Op_MeanX:
            return QSL("MeanX");
        case AggregateOps::Op_SigmaX:
            return QSL("SigmaX");

        case AggregateOps::NumOps:
            break;
    }
    return QString();
}

//
// ArrayMap
//
ArrayMap::ArrayMap(QObject *parent)
    : OperatorInterface(parent)
{
    addSlot();
    m_output.setSource(this);
}

bool ArrayMap::addSlot()
{
    /* If InputType::Array is passed directly inside make_shared() call I get
     * an "undefined reference to InputType::Array. */
    auto inputType = InputType::Array;

    auto slot = std::make_shared<Slot>(
        this, getNumberOfSlots(),
        QSL("Input#") + QString::number(getNumberOfSlots()), inputType);

    m_inputs.push_back(slot);

    return true;
}

bool ArrayMap::removeLastSlot()
{
    if (getNumberOfSlots() > 1)
    {
        m_inputs.back()->disconnectPipe();
        m_inputs.pop_back();

        return true;
    }

    return false;
}

void ArrayMap::beginRun(const RunInfo &)
{
    s32 mappingCount = m_mappings.size();
    m_output.parameters.name = objectName();
    m_output.parameters.resize(mappingCount);

#if ENABLE_ANALYSIS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << this << "#mappings =" << mappingCount;
#endif

    for (s32 mIndex = 0;
         mIndex < mappingCount;
         ++mIndex)
    {
        IndexPair ip(m_mappings.at(mIndex));
        Parameter *inParam = nullptr;
        Slot *inputSlot = ip.slotIndex < m_inputs.size() ? m_inputs[ip.slotIndex].get() : nullptr;

        if (inputSlot && inputSlot->inputPipe)
        {
            inParam = inputSlot->inputPipe->getParameter(ip.paramIndex);

            if (mIndex == 0)
            {
                // Use the first inputs name and unit label.
                //m_output.parameters.name = inputSlot->inputPipe->parameters.name;
                m_output.parameters.unit = inputSlot->inputPipe->parameters.unit;
            }
        }

        if (inParam)
        {
            m_output.parameters[mIndex].lowerLimit = inParam->lowerLimit;
            m_output.parameters[mIndex].upperLimit = inParam->upperLimit;
        }
    }

#if ENABLE_ANALYSIS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << this << "#output params =" << m_output.parameters.size();
#endif
}

void ArrayMap::step()
{
    s32 mappingCount = m_mappings.size();

    for (s32 mIndex = 0;
         mIndex < mappingCount;
         ++mIndex)
    {
        IndexPair ip(m_mappings.at(mIndex));
        Parameter *inParam = nullptr;
        Slot *inputSlot = ip.slotIndex < m_inputs.size() ? m_inputs[ip.slotIndex].get() : nullptr;

        if (inputSlot && inputSlot->inputPipe)
        {
            inParam = inputSlot->inputPipe->getParameter(ip.paramIndex);
        }

        if (inParam)
        {
            m_output.parameters[mIndex] = *inParam;
        }
        else
        {
            m_output.parameters[mIndex].valid = false;
        }
    }
}

s32 ArrayMap::getNumberOfSlots() const
{
    return m_inputs.size();
}

Slot *ArrayMap::getSlot(s32 slotIndex)
{
    Slot *result = nullptr;

    if (slotIndex < getNumberOfSlots())
    {
        result = m_inputs[slotIndex].get();
    }

    return result;
}

s32 ArrayMap::getNumberOfOutputs() const
{
    return 1;
}

QString ArrayMap::getOutputName(s32 outputIndex) const
{
    return QSL("Output");
}

Pipe *ArrayMap::getOutput(s32 index)
{
    return &m_output;
}

void ArrayMap::read(const QJsonObject &json)
{
    m_inputs.clear();

    s32 inputCount = json["numberOfInputs"].toInt();

    for (s32 inputIndex = 0;
         inputIndex < inputCount;
         ++inputIndex)
    {
        addSlot();
    }

    auto mappingsArray = json["mappings"].toArray();

    for (auto it = mappingsArray.begin();
         it != mappingsArray.end();
         ++it)
    {
        auto objectJson = it->toObject();
        IndexPair ip;
        ip.slotIndex  = objectJson["slotIndex"].toInt();
        ip.paramIndex = objectJson["paramIndex"].toInt();
        m_mappings.push_back(ip);
    }
}

void ArrayMap::write(QJsonObject &json) const
{
    json["numberOfInputs"] = getNumberOfSlots();

    QJsonArray mappingsArray;

    for (auto mapping: m_mappings)
    {
        QJsonObject dest;
        dest["slotIndex"]  = mapping.slotIndex;
        dest["paramIndex"] = mapping.paramIndex;
        mappingsArray.append(dest);
    }

    json["mappings"] = mappingsArray;
}

QString ArrayMap::getDisplayName() const
{
    return QSL("Array Map");
}

QString ArrayMap::getShortName() const
{
    return QSL("Map");
}

//
// RangeFilter1D
//
RangeFilter1D::RangeFilter1D(QObject *parent)
    : BasicOperator(parent)
{
    m_inputSlot.acceptedInputTypes = InputType::Both;
}

void RangeFilter1D::beginRun(const RunInfo &)
{
    auto &out(m_output.getParameters());
    out.resize(0);
    out.name = objectName();
    out.unit = QString();

    if (m_inputSlot.isParamIndexInRange())
    {
        const auto &in(m_inputSlot.inputPipe->getParameters());

        s32 idxMin = 0;
        s32 idxMax = in.size();

        if (m_inputSlot.isParameterConnection())
        {
            out.resize(1);
            idxMin = m_inputSlot.paramIndex;
            idxMax = idxMin + 1;
        }
        else
        {
            out.resize(in.size());
        }

        out.invalidateAll();
        out.unit = in.unit;

        for (s32 idx = idxMin, outIdx = 0;
             idx < idxMax;
             ++idx, ++outIdx)
        {
            auto &outParam(out[outIdx]);
            const auto &inParam(in[idx]);

            if (!m_keepOutside)
            {
                outParam.lowerLimit = m_minValue;
                outParam.upperLimit = m_maxValue;
            }
            else
            {
                outParam.lowerLimit = inParam.lowerLimit;
                outParam.upperLimit = inParam.upperLimit;
            }
        }
    }
}

void RangeFilter1D::step()
{
    if (m_inputSlot.isParamIndexInRange())
    {
        auto &out(m_output.getParameters());
        const auto &in(m_inputSlot.inputPipe->getParameters());

        s32 idxMin = 0;
        s32 idxMax = in.size();

        if (m_inputSlot.isParameterConnection())
        {
            idxMin = m_inputSlot.paramIndex;
            idxMax = idxMin + 1;
        }

        for (s32 idx = idxMin, outIdx = 0;
             idx < idxMax;
             ++idx, ++outIdx)
        {
            auto &outParam(out[outIdx]);
            const auto &inParam(in[idx]);

            if (inParam.valid)
            {
                bool inRange = (m_minValue <= inParam.value && inParam.value < m_maxValue);

                if ((inRange && !m_keepOutside)
                    || (!inRange && m_keepOutside))
                {
                    // Only assigning value instead of the whole parameter
                    // because the limits have been adjusted in beginRun()
                    outParam.value = inParam.value;
                    outParam.valid = true;
                }
                else
                {
                    outParam.valid = false;
                }
            }
            else
            {
                outParam.valid = false;
            }
        }
    }
}

void RangeFilter1D::read(const QJsonObject &json)
{
    m_minValue = json["minValue"].toDouble();
    m_maxValue = json["maxValue"].toDouble();
    m_keepOutside = json["keepOutside"].toBool();
}

void RangeFilter1D::write(QJsonObject &json) const
{
    json["minValue"] = m_minValue;
    json["maxValue"] = m_maxValue;
    json["keepOutside"] = m_keepOutside;
}

//
// ConditionFilter
//
ConditionFilter::ConditionFilter(QObject *parent)
    : OperatorInterface(parent)
    , m_dataInput(this, 0, QSL("Data"))
    , m_conditionInput(this, 1, QSL("Condition"))
{
    m_output.setSource(this);
    m_dataInput.acceptedInputTypes = InputType::Both;
    m_conditionInput.acceptedInputTypes = InputType::Both;
}

void ConditionFilter::beginRun(const RunInfo &)
{
    auto &out(m_output.getParameters());

    out.resize(0);
    out.name = objectName();// in.name;
    out.unit = QString();

    if (!m_dataInput.isParamIndexInRange() || !m_conditionInput.isParamIndexInRange())
    {
        return;
    }

    if (m_dataInput.isConnected() && m_conditionInput.isConnected())
    {
        const auto &in(m_dataInput.inputPipe->getParameters());

        s32 idxMin = 0;
        s32 idxMax = in.size();

        if (m_dataInput.isParameterConnection())
        {
            out.resize(1);
            idxMin = m_dataInput.paramIndex;
            idxMax = idxMin + 1;
        }
        else
        {
            out.resize(in.size());
        }

        out.invalidateAll();
        //out.name = in.name;
        out.unit = in.unit;

        for (s32 idx = idxMin, outIdx = 0;
             idx < idxMax;
             ++idx, ++outIdx)
        {
            const Parameter &inParam(in[idx]);
            Parameter &outParam(out[outIdx]);

            outParam.lowerLimit = inParam.lowerLimit;
            outParam.upperLimit = inParam.upperLimit;
        }
    }
}

void ConditionFilter::step()
{
    if (m_dataInput.isParamIndexInRange() && m_conditionInput.isParamIndexInRange())
    {
        auto &out(m_output.getParameters());
        const auto &dataIn(m_dataInput.inputPipe->getParameters());
        const auto &condIn(m_conditionInput.inputPipe->getParameters());

        s32 idxMin = 0;
        s32 idxMax = out.size();

        if (m_dataInput.isParameterConnection())
        {
            idxMin = m_dataInput.paramIndex;
            idxMax = idxMin + 1;
        }

        for (s32 idx = idxMin, outIdx = 0;
             idx < idxMax;
             ++idx, ++outIdx)
        {
            auto &outParam(out[outIdx]);
            const auto &dataParam(dataIn[idx]);

            // The index into the condition array can be out of range if the
            // condition array is smaller than the data array. In that case a
            // default constructed and thus invalid parameter will be used.
            const auto condParam(m_conditionInput.isParameterConnection()
                                 ? condIn.value(m_conditionInput.paramIndex)
                                 : condIn.value(idx));

            if (condParam.valid)
            {
                outParam.valid = dataParam.valid;
                outParam.value = dataParam.value;
            }
            else
            {
                outParam.valid = false;
            }
        }
    }
}

// Inputs
s32 ConditionFilter::getNumberOfSlots() const
{
    return 2;
}

Slot *ConditionFilter::getSlot(s32 slotIndex)
{
    if (slotIndex == 0)
    {
        return &m_dataInput;
    }
    else if (slotIndex == 1)
    {
        return &m_conditionInput;
    }

    return nullptr;
}

s32 ConditionFilter::getNumberOfOutputs() const
{
    return 1;
}

QString ConditionFilter::getOutputName(s32 outputIndex) const
{
    return QSL("Output");
}

Pipe *ConditionFilter::getOutput(s32 index)
{
    return &m_output;
}

void ConditionFilter::read(const QJsonObject &json)
{
}

void ConditionFilter::write(QJsonObject &json) const
{
}

//
// RectFilter2D
//
RectFilter2D::RectFilter2D(QObject *parent)
    : OperatorInterface(parent)
    , m_xInput(this, 0, QSL("X Data"))
    , m_yInput(this, 1, QSL("Y Data"))
{
    m_output.setSource(this);
    m_xInput.acceptedInputTypes = InputType::Value;
    m_yInput.acceptedInputTypes = InputType::Value;
}

void RectFilter2D::beginRun(const RunInfo &)
{
    auto &out(m_output.getParameters());
    out.resize(0);
    out.name = objectName();
    out.unit = QString();

    if (!m_xInput.isParamIndexInRange() || !m_yInput.isParamIndexInRange())
        return;

    // Both connected and in range
    out.resize(1);
}

void RectFilter2D::step()
{
    if (!m_xInput.isParamIndexInRange() || !m_yInput.isParamIndexInRange())
        return;

    Parameter *out(m_output.getParameter(0));
    Parameter *px = m_xInput.inputPipe->getParameter(m_xInput.paramIndex);
    Parameter *py = m_yInput.inputPipe->getParameter(m_yInput.paramIndex);

    Q_ASSERT(out);
    Q_ASSERT(px);
    Q_ASSERT(py);

    out->valid = false;

    if (px->valid && py->valid)
    {
        bool xInRange = m_xInterval.contains(px->value);
        bool yInRange = m_yInterval.contains(py->value);

        out->valid = (m_op == OpAnd
                      ? (xInRange && yInRange)
                      : (xInRange || yInRange));
    }
}

s32 RectFilter2D::getNumberOfSlots() const
{
    return 2;
}

Slot *RectFilter2D::getSlot(s32 slotIndex)
{
    switch (slotIndex)
    {
        case 0:
            return &m_xInput;
        case 1:
            return &m_yInput;
    }
    return nullptr;
}

s32 RectFilter2D::getNumberOfOutputs() const
{
    return 1;
}

QString RectFilter2D::getOutputName(s32 outputIndex) const
{
    return QSL("Output");
}

Pipe *RectFilter2D::getOutput(s32 index)
{
    return &m_output;
}

void RectFilter2D::read(const QJsonObject &json)
{
    m_op = OpAnd;
    if (json["operator"].toString() == "or")
    {
        m_op = OpOr;
    }

    double x1 = json["x1"].toDouble();
    double x2 = json["x2"].toDouble();
    double y1 = json["y1"].toDouble();
    double y2 = json["y2"].toDouble();

    setXInterval(x1, x2);
    setYInterval(y1, y2);
}

void RectFilter2D::write(QJsonObject &json) const
{
    json["operator"] = (m_op == OpAnd ? QSL("and") : QSL("or"));
    json["x1"] = m_xInterval.minValue();
    json["x2"] = m_xInterval.maxValue();
    json["y1"] = m_yInterval.minValue();
    json["y2"] = m_yInterval.maxValue();
}

//
// BinarySumDiff
//
struct EquationImpl
{
    const QString displayString;
    // args are (inputA, inputB, output)
    void (*impl)(const ParameterVector &, const ParameterVector &, ParameterVector &);
};

// Do not reorder the array as indexes are stored in config files!
static const QVector<EquationImpl> EquationImpls =
{
    { QSL("C = A + B"), [](const ParameterVector &a, const ParameterVector &b, ParameterVector &o)
        {
            for (s32 i = 0; i < a.size(); ++i)
            {
                o[i].valid = a[i].valid && b[i].valid;
                o[i].value = a[i].value +  b[i].value;
            }
        }
    },

    { QSL("C = A - B"), [](const ParameterVector &a, const ParameterVector &b, ParameterVector &o)
        {
            for (s32 i = 0; i < a.size(); ++i)
            {
                o[i].valid = a[i].valid && b[i].valid;
                o[i].value = a[i].value -  b[i].value;
            }
        }
    },

    { QSL("C = (A + B) / (A - B)"), [](const ParameterVector &a, const ParameterVector &b, ParameterVector &o)
        {
            for (s32 i = 0; i < a.size(); ++i)
            {
                o[i].valid = (a[i].valid && b[i].valid && (a[i].value - b[i].value != 0.0));

                if (o[i].valid)
                {
                    o[i].value = (a[i].value + b[i].value) / (a[i].value - b[i].value);
                }
            }
        }
    },

    { QSL("C = (A - B) / (A + B)"), [](const ParameterVector &a, const ParameterVector &b, ParameterVector &o)
        {
            for (s32 i = 0; i < a.size(); ++i)
            {
                o[i].valid = (a[i].valid && b[i].valid && (a[i].value + b[i].value != 0.0));

                if (o[i].valid)
                {
                    o[i].value = (a[i].value - b[i].value) / (a[i].value + b[i].value);
                }
            }
        }
    },

    { QSL("C = A / (A - B)"), [](const ParameterVector &a, const ParameterVector &b, ParameterVector &o)
        {
            for (s32 i = 0; i < a.size(); ++i)
            {
                o[i].valid = (a[i].valid && b[i].valid && (a[i].value - b[i].value != 0.0));

                if (o[i].valid)
                {
                    o[i].value = a[i].value / (a[i].value - b[i].value);
                }
            }
        }
    },

    { QSL("C = (A - B) / A"), [](const ParameterVector &a, const ParameterVector &b, ParameterVector &o)
        {
            for (s32 i = 0; i < a.size(); ++i)
            {
                o[i].valid = (a[i].valid && b[i].valid && (a[i].value != 0.0));

                if (o[i].valid)
                {
                    o[i].value = (a[i].value - b[i].value) / a[i].value;
                }
            }
        }
    },
};

BinarySumDiff::BinarySumDiff(QObject *parent)
    : OperatorInterface(parent)
    , m_inputA(this, 0, QSL("A"))
    , m_inputB(this, 1, QSL("B"))
    , m_equationIndex(0)
    , m_outputLowerLimit(0.0)
    , m_outputUpperLimit(0.0)
{
    m_output.setSource(this);
    m_inputA.acceptedInputTypes = InputType::Array;
    m_inputB.acceptedInputTypes = InputType::Array;
}

s32 BinarySumDiff::getNumberOfEquations() const
{
    return EquationImpls.size();
}

QString BinarySumDiff::getEquationDisplayString(s32 index) const
{
    if (0 <= index && index < EquationImpls.size())
    {
        return EquationImpls.at(index).displayString;
    }

    return QString();
}

// FIXME: basic implementation bailing out if input sizes are not equal. Check
// what a2 does and maybe not fix it but document the behaviour and do not
// crash!
// Implement it so that the smalles input size is used for calculations and the
// other output values are filled with invalids.
void BinarySumDiff::beginRun(const RunInfo &)
{
    auto &out(m_output.getParameters());

    if (m_inputA.isConnected()
        && m_inputB.isConnected()
        && m_inputA.inputPipe->getSize() == m_inputB.inputPipe->getSize()
        && 0 <= m_equationIndex && m_equationIndex < EquationImpls.size())
    {
        out.resize(m_inputA.inputPipe->getSize());
        out.name = objectName();
        out.unit = m_outputUnitLabel;

        for (auto &param: out)
        {
            param.valid = false;
            param.lowerLimit = m_outputLowerLimit;
            param.upperLimit = m_outputUpperLimit;
        }
    }
    else
    {
        out.resize(0);
        out.name = QString();
        out.unit = QString();
    }
}

void BinarySumDiff::step()
{
    auto &o(m_output.getParameters());

    if (!o.isEmpty())
    {
        const auto &a(m_inputA.inputPipe->getParameters());
        const auto &b(m_inputB.inputPipe->getParameters());
        auto fn = EquationImpls.at(m_equationIndex).impl;

        fn(a, b, o);
    }
    else
    {
        o.invalidateAll();
    }
}

s32 BinarySumDiff::getNumberOfSlots() const
{
    return 2;
}

Slot *BinarySumDiff::getSlot(s32 slotIndex)
{
    switch (slotIndex)
    {
        case 0:
            return &m_inputA;
        case 1:
            return &m_inputB;
    }
    return nullptr;
}

s32 BinarySumDiff::getNumberOfOutputs() const
{
    return 1;
}

QString BinarySumDiff::getOutputName(s32 outputIndex) const
{
    return QSL("Output");
}

Pipe *BinarySumDiff::getOutput(s32 index)
{
    return &m_output;
}

void BinarySumDiff::read(const QJsonObject &json)
{
    m_equationIndex = json["equationIndex"].toInt();
    m_outputUnitLabel = json["outputUnitLabel"].toString();
    m_outputLowerLimit = json["outputLowerLimit"].toDouble();
    m_outputUpperLimit = json["outputUpperLimit"].toDouble();
}

void BinarySumDiff::write(QJsonObject &json) const
{
    json["equationIndex"] = m_equationIndex;
    json["outputUnitLabel"] = m_outputUnitLabel;
    json["outputLowerLimit"] = m_outputLowerLimit;
    json["outputUpperLimit"] = m_outputUpperLimit;
}

//
// ExpressionOperator
//
ExpressionOperator::ExpressionOperator(QObject *parent)
    : BasicOperator(parent)
{
    m_inputSlot.acceptedInputTypes = InputType::Array;

    m_exprBegin = QSL
        (
        "var lower_limits[input_lower_limits[]] := input_lower_limits;\n"
        "var upper_limits[input_upper_limits[]] := input_upper_limits;\n"
        "return [lower_limits, upper_limits];\n"
        );

    m_exprStep = QSL
        (
        "for (var i := 0; i < input[]; i += 1)\n"
        "{\n"
        "   output[i] := input[i];\n"
        "}\n"
        );
}

void ExpressionOperator::beginRun(const RunInfo &runInfo)
{
    if (!m_inputSlot.inputPipe) return;

    memory::Arena arena(Kilobytes(256));

    auto a1_inPipe = m_inputSlot.inputPipe;

    a2::PipeVectors a2_inPipe = {};
    a2_inPipe.data = a2::push_param_vector(&arena, a1_inPipe->getSize(), make_quiet_nan());
    a2_inPipe.lowerLimits = a2::push_param_vector(&arena, a1_inPipe->getSize());
    a2_inPipe.upperLimits = a2::push_param_vector(&arena, a1_inPipe->getSize());

    for (s32 i = 0; i < a1_inPipe->getSize(); i++)
    {
        a2_inPipe.lowerLimits[i] = a1_inPipe->getParameter(i)->lowerLimit;
        a2_inPipe.upperLimits[i] = a1_inPipe->getParameter(i)->upperLimit;
    }

    auto a2_op = a2::make_expression_operator(
        &arena,
        a2_inPipe,
        getBeginExpression().toStdString(),
        getStepExpression().toStdString());

    auto &params(m_output.getParameters());
    params.resize(a2_op.outputLowerLimits[0].size);

    for (s32 i = 0; i < params.size(); i++)
    {
        params[i].lowerLimit = a2_op.outputLowerLimits[0][i];
        params[i].upperLimit = a2_op.outputUpperLimits[0][i];
    }

    params.invalidateAll();
}

void ExpressionOperator::step()
{
    assert(!"not implemented. a2 should be used!");
}

void ExpressionOperator::write(QJsonObject &json) const
{
    json["exprBegin"] = m_exprBegin;
    json["exprStep"] = m_exprStep;
}

void ExpressionOperator::read(const QJsonObject &json)
{
    m_exprBegin = json["exprBegin"].toString();
    m_exprStep = json["exprStep"].toString();
}

//
// Histo1DSink
//

static const size_t HistoMemAlignment = 64;

Histo1DSink::Histo1DSink(QObject *parent)
    : BasicSink(parent)
{
}

void Histo1DSink::beginRun(const RunInfo &runInfo)
{
#if ANALYSIS_USE_SHARED_HISTO1D_MEM
    /* Single memory block allocation strategy:
     * Don't shrink.
     * If resizing to a larger size, recreate the arena. This will invalidate
     * all pointers into the histograms. Recreate pointers into arena, clear
     * memory. Update pointers for existing Histo1D instances.
     */
    if (!m_inputSlot.isParamIndexInRange())
    {
        m_histos.resize(0);
        m_histoArena.reset();
        return;
    }

    size_t histoCount = 0;
    s32 minIdx = 0;
    s32 maxIdx = 0;

    if (m_inputSlot.paramIndex != Slot::NoParamIndex)
    {
        histoCount = 1;
        minIdx = m_inputSlot.paramIndex;
        maxIdx = minIdx + 1;
    }
    else
    {
        histoCount = m_inputSlot.inputPipe->parameters.size();
        minIdx = 0;
        maxIdx = (s32)histoCount;
    }

    m_histos.resize(histoCount);

    // Space for the histos plus space to allow proper alignment
    size_t requiredMemory = histoCount * m_bins * sizeof(double) + histoCount * HistoMemAlignment;

    if (!m_histoArena || m_histoArena->size < requiredMemory)
    {
        m_histoArena = std::make_shared<memory::Arena>(requiredMemory);
    }
    else
    {
        m_histoArena->reset();
    }

    for (s32 idx = minIdx, histoIndex = 0; idx < maxIdx; idx++, histoIndex++)
    {
        double xMin = m_xLimitMin;
        double xMax = m_xLimitMax;

        if (std::isnan(xMin))
        {
            xMin = m_inputSlot.inputPipe->parameters[idx].lowerLimit;
        }

        if (std::isnan(xMax))
        {
            xMax = m_inputSlot.inputPipe->parameters[idx].upperLimit;
        }

        AxisBinning binning(m_bins, xMin, xMax);
        SharedHistoMem histoMem = { m_histoArena, m_histoArena->pushArray<double>(m_bins, HistoMemAlignment) };

        assert(histoMem.data);

        auto histo = m_histos[histoIndex];

        if (histo)
        {
            assert(!histo->ownsMemory());
            histo->setData(histoMem, binning);

            if (!runInfo.keepAnalysisState)
            {
                histo->clear();
            }
        }
        else
        {
            m_histos[histoIndex] = histo = std::make_shared<Histo1D>(binning, histoMem);
        }

        assert(histo);

        auto histoName = this->objectName();
        AxisInfo axisInfo;
        axisInfo.title = this->m_xAxisTitle;
        axisInfo.unit  = m_inputSlot.inputPipe->parameters.unit;

        if (maxIdx - minIdx > 1)
        {
            histoName = QString("%1[%2]").arg(histoName).arg(idx);
            axisInfo.title = QString("%1[%2]").arg(axisInfo.title).arg(idx);
        }
        histo->setObjectName(histoName);
        histo->setAxisInfo(Qt::XAxis, axisInfo);
        histo->setTitle(histoName);

        if (!runInfo.runId.isEmpty())
        {
            histo->setFooter(QString("<small>runId=%1</small>").arg(runInfo.runId));
        }
    }

#else
    // Instead of just clearing the histos vector and recreating it this code
    // tries to reuse existing histograms. This is done so that open histogram
    // windows still reference the correct histogram after beginRun() is
    // invoked. Otherwise the user would have to reopen histogram windows quite
    // frequently.

    if (m_inputSlot.isParamIndexInRange())
    {
        s32 minIdx = 0;
        s32 maxIdx = m_inputSlot.inputPipe->parameters.size();

        if (m_inputSlot.paramIndex != Slot::NoParamIndex)
        {
            minIdx = m_inputSlot.paramIndex;
            maxIdx = minIdx + 1;
        }

        m_histos.resize(maxIdx - minIdx);

        for (s32 idx = minIdx, histoIndex = 0; idx < maxIdx; ++idx, ++histoIndex)
        {
            double xMin = m_xLimitMin;
            double xMax = m_xLimitMax;

            if (std::isnan(xMin))
            {
                xMin = m_inputSlot.inputPipe->parameters[idx].lowerLimit;
            }

            if (std::isnan(xMax))
            {
                xMax = m_inputSlot.inputPipe->parameters[idx].upperLimit;
            }

            if (m_histos[histoIndex])
            {
                auto histo = m_histos[histoIndex].get();

                if (histo->getNumberOfBins() != static_cast<u32>(m_bins) || !runInfo.keepAnalysisState)
                {
                    histo->resize(m_bins); // calls clear() even if the size does not change
                }

                AxisBinning newBinning(m_bins, xMin, xMax);

                if (newBinning != histo->getAxisBinning(Qt::XAxis))
                {
                    histo->setAxisBinning(Qt::XAxis, newBinning);
                    histo->clear(); // have to clear because the binning changed
                }
            }
            else
            {
                m_histos[histoIndex] = std::make_unique<Histo1D>(m_bins, xMin, xMax);
            }

            auto histo = m_histos[histoIndex].get();
            auto histoName = this->objectName();
            AxisInfo axisInfo;
            axisInfo.title = this->m_xAxisTitle;
            axisInfo.unit  = m_inputSlot.inputPipe->parameters.unit;

            if (maxIdx - minIdx > 1)
            {
                histoName = QString("%1[%2]").arg(histoName).arg(idx);
                axisInfo.title = QString("%1[%2]").arg(axisInfo.title).arg(idx);
            }
            histo->setObjectName(histoName);
            histo->setAxisInfo(Qt::XAxis, axisInfo);
            histo->setTitle(histoName);

            if (!runInfo.runId.isEmpty())
            {
                histo->setFooter(QString("<small>runId=%1</small>").arg(runInfo.runId));
            }
        }
    }
    else
    {
        m_histos.resize(0);
    }
#endif
}

void Histo1DSink::step()
{
    if (m_inputSlot.inputPipe && !m_histos.empty())
    {
        s32 paramIndex = m_inputSlot.paramIndex;

        if (paramIndex >= 0)
        {
            // Input is a single value
            const Parameter *param = m_inputSlot.inputPipe->getParameter(paramIndex);
            if (param && param->valid)
            {
                m_histos[0]->fill(param->value);
            }
        }
        else
        {
            // Input is an array
            const auto &in(m_inputSlot.inputPipe->getParameters());
            const s32 inSize = in.size();
            const s32 histoCount = m_histos.size();

            for (s32 paramIndex = 0; paramIndex < std::min(inSize, histoCount); ++paramIndex)
            {
                const Parameter *param = m_inputSlot.inputPipe->getParameter(paramIndex);
                if (param && param->valid)
                {
                    m_histos[paramIndex]->fill(param->value);
                }
            }
        }
    }
}

void Histo1DSink::read(const QJsonObject &json)
{
    m_bins = json["nBins"].toInt();
    m_xAxisTitle = json["xAxisTitle"].toString();
    m_xLimitMin = json["xLimitMin"].toDouble(make_quiet_nan());
    m_xLimitMax = json["xLimitMax"].toDouble(make_quiet_nan());

    Q_ASSERT(m_bins > 0);
}

void Histo1DSink::write(QJsonObject &json) const
{
    json["nBins"] = static_cast<qint64>(m_bins);
    json["xAxisTitle"] = m_xAxisTitle;
    json["xLimitMin"]  = m_xLimitMin;
    json["xLimitMax"]  = m_xLimitMax;
}

size_t Histo1DSink::getStorageSize() const
{
#if ANALYSIS_USE_SHARED_HISTO1D_MEM
    return m_histoArena ? m_histoArena->size : 0;
#else
    return std::accumulate(m_histos.begin(), m_histos.end(),
                           static_cast<size_t>(0u),
                           [](size_t v, const auto &histoPtr) {
        return v + histoPtr->getStorageSize();
    });
#endif
}

//
// Histo2DSink
//
Histo2DSink::Histo2DSink(QObject *parent)
    : SinkInterface(parent)
    , m_inputX(this, 0, QSL("X-Axis"), InputType::Value)
    , m_inputY(this, 1, QSL("Y-Axis"), InputType::Value)
{
}

// Creates or resizes the histogram. Updates the axis limits to match
// the input parameters limits. Clears the histogram.
void Histo2DSink::beginRun(const RunInfo &runInfo)
{
    if (m_inputX.inputPipe && m_inputY.inputPipe
        && m_inputX.paramIndex < m_inputX.inputPipe->parameters.size()
        && m_inputY.paramIndex < m_inputY.inputPipe->parameters.size())
    {
        double xMin = m_xLimitMin;
        double xMax = m_xLimitMax;

        if (std::isnan(xMin))
        {
            xMin = m_inputX.inputPipe->parameters[m_inputX.paramIndex].lowerLimit;
        }

        if (std::isnan(xMax))
        {
            xMax = m_inputX.inputPipe->parameters[m_inputX.paramIndex].upperLimit;
        }

        double yMin = m_yLimitMin;
        double yMax = m_yLimitMax;

        if (std::isnan(yMin))
        {
            yMin = m_inputY.inputPipe->parameters[m_inputY.paramIndex].lowerLimit;
        }

        if (std::isnan(yMax))
        {
            yMax = m_inputY.inputPipe->parameters[m_inputY.paramIndex].upperLimit;
        }

        if (!m_histo)
        {
            m_histo = std::make_shared<Histo2D>(m_xBins, xMin, xMax,
                                                m_yBins, yMin, yMax);

        }
        else
        {
            if (m_histo->getAxisBinning(Qt::XAxis).getBins() != static_cast<u32>(m_xBins)
                || m_histo->getAxisBinning(Qt::YAxis).getBins() != static_cast<u32>(m_yBins)
                || !runInfo.keepAnalysisState)
            {
                // resize always clears the histo
                m_histo->resize(m_xBins, m_yBins);
            }

            AxisBinning newXBinning(m_xBins, xMin, xMax);
            AxisBinning newYBinning(m_yBins, yMin, yMax);

            if (m_histo->getAxisBinning(Qt::XAxis) != newXBinning
                || m_histo->getAxisBinning(Qt::YAxis) != newYBinning)
            {
                m_histo->setAxisBinning(Qt::XAxis, newXBinning);
                m_histo->setAxisBinning(Qt::YAxis, newYBinning);
                m_histo->clear(); // have to clear because the binning changed
            }
        }

        m_histo->setObjectName(objectName());
        m_histo->setTitle(objectName());

        if (!runInfo.runId.isEmpty())
        {
            m_histo->setFooter(QString("<small>runId=%1</small>").arg(runInfo.runId));
        }

        {
            AxisInfo info;
            info.title = m_xAxisTitle;
            info.unit  = m_inputX.inputPipe->parameters.unit;
            m_histo->setAxisInfo(Qt::XAxis, info);
        }

        {
            AxisInfo info;
            info.title = m_yAxisTitle;
            info.unit  = m_inputY.inputPipe->parameters.unit;
            m_histo->setAxisInfo(Qt::YAxis, info);
        }
    }
}

s32 Histo2DSink::getNumberOfSlots() const
{
    return 2;
}

Slot *Histo2DSink::getSlot(s32 slotIndex)
{
    switch (slotIndex)
    {
        case 0:
            return &m_inputX;
        case 1:
            return &m_inputY;
        default:
            return nullptr;
    }
}

void Histo2DSink::step()
{
    if (m_inputX.inputPipe && m_inputY.inputPipe && m_histo)
    {
        auto paramX = m_inputX.inputPipe->getParameter(m_inputX.paramIndex);
        auto paramY = m_inputY.inputPipe->getParameter(m_inputY.paramIndex);

        if (isParameterValid(paramX) && isParameterValid(paramY))
        {
            m_histo->fill(paramX->value, paramY->value);
        }
    }
}

void Histo2DSink::read(const QJsonObject &json)
{
    m_xBins = static_cast<s32>(json["xBins"].toInt());
    m_xLimitMin = json["xLimitMin"].toDouble(make_quiet_nan());
    m_xLimitMax = json["xLimitMax"].toDouble(make_quiet_nan());

    m_yBins = static_cast<s32>(json["yBins"].toInt());
    m_yLimitMin = json["yLimitMin"].toDouble(make_quiet_nan());
    m_yLimitMax = json["yLimitMax"].toDouble(make_quiet_nan());

    m_xAxisTitle = json["xAxisTitle"].toString();
    m_yAxisTitle = json["yAxisTitle"].toString();
}

void Histo2DSink::write(QJsonObject &json) const
{
    json["xBins"] = m_xBins;
    json["xLimitMin"]  = m_xLimitMin;
    json["xLimitMax"]  = m_xLimitMax;

    json["yBins"] = m_yBins;
    json["yLimitMin"]  = m_yLimitMin;
    json["yLimitMax"]  = m_yLimitMax;

    json["xAxisTitle"] = m_xAxisTitle;
    json["yAxisTitle"] = m_yAxisTitle;
}

size_t Histo2DSink::getStorageSize() const
{
    return m_histo ? m_histo->getStorageSize() : 0u;
}

//
// RateMonitorSink
//

static QString to_string(RateMonitorSink::Type type)
{
    QString result;

    switch (type)
    {
        case RateMonitorSink::Type::PrecalculatedRate:
            result = QSL("PrecalculatedRate");
            break;
        case RateMonitorSink::Type::CounterDifference:
            result = QSL("CounterDifference");
            break;
        case RateMonitorSink::Type::FlowRate:
            result = QSL("FlowRate");
            break;
    }

    return result;
}

static RateMonitorSink::Type rate_monitor_sink_type_from_string(const QString &str)
{
    RateMonitorSink::Type result = RateMonitorSink::Type::CounterDifference;

    if (str.compare(QSL("PrecalculatedRate"), Qt::CaseInsensitive) == 0)
        result = RateMonitorSink::Type::PrecalculatedRate;

    if (str.compare(QSL("CounterDifference"), Qt::CaseInsensitive) == 0)
        result = RateMonitorSink::Type::CounterDifference;

    if (str.compare(QSL("FlowRate"), Qt::CaseInsensitive) == 0)
        result = RateMonitorSink::Type::FlowRate;

    return result;
}


RateMonitorSink::RateMonitorSink(QObject *parent)
    : BasicSink(parent)
{
    m_inputSlot.acceptedInputTypes = InputType::Array;
}

void RateMonitorSink::beginRun(const RunInfo &runInfo)
{
    if (!m_inputSlot.isConnected())
    {
        m_samplers.resize(0);
        return;
    }

    // Currently only supports connecting to arrays, not single parameters.
    assert(m_inputSlot.paramIndex == Slot::NoParamIndex);

    m_samplers.resize(m_inputSlot.inputPipe->parameters.size());

    for (auto &sampler: m_samplers)
    {
        if (!sampler)
        {
            sampler = std::make_shared<a2::RateSampler>();
            sampler->rateHistory = RateHistoryBuffer(m_rateHistoryCapacity);
        }
        else
        {
            sampler->lastValue = 0.0;
            sampler->lastRate  = 0.0;
            sampler->lastDelta = 0.0;

            /* If the new capacity is >= the old capacity then the rateHistory
             * contents are kept, otherwise the oldest values are discarded. */
            if (sampler->rateHistory.capacity() != m_rateHistoryCapacity)
            {
                sampler->rateHistory.set_capacity(m_rateHistoryCapacity);
                sampler->rateHistory.resize(0);
                sampler->totalSamples = 0.0;
            }

            if (!runInfo.keepAnalysisState)
            {
                // truncates the history size (not the capacity) to zero
                sampler->rateHistory.resize(0);
                sampler->totalSamples = 0.0;
            }
        }

        sampler->scale = getCalibrationFactor();
        sampler->offset = getCalibrationOffset();
        sampler->interval = getSamplingInterval();

        assert(sampler->rateHistory.capacity() == m_rateHistoryCapacity);
        assert(runInfo.keepAnalysisState || sampler->rateHistory.size() == 0);
        assert(sampler->scale == getCalibrationFactor());
        assert(sampler->offset == getCalibrationOffset());
        assert(sampler->interval == getSamplingInterval());
    }
}

void RateMonitorSink::step()
{
    assert(!"not implemented. a2 should be used!");
}

void RateMonitorSink::write(QJsonObject &json) const
{
    json["type"] = to_string(getType());
    json["capacity"] = static_cast<qint64>(m_rateHistoryCapacity);
    json["unitLabel"] = getUnitLabel();
    json["calibrationFactor"] = getCalibrationFactor();
    json["calibrationOffset"] = getCalibrationOffset();
    json["samplingInterval"]  = getSamplingInterval();
}

void RateMonitorSink::read(const QJsonObject &json)
{
    m_type = rate_monitor_sink_type_from_string(json["type"].toString());
    m_rateHistoryCapacity = json["capacity"].toInt();
    m_unitLabel = json["unitLabel"].toString();
    m_calibrationFactor = json["calibrationFactor"].toDouble(1.0);
    m_calibrationOffset = json["m_calibrationOffset"].toDouble(0.0);
    m_samplingInterval  = json["samplingInterval"].toDouble(1.0);
}

size_t RateMonitorSink::getStorageSize() const
{
    return std::accumulate(m_samplers.begin(), m_samplers.end(),
                           static_cast<size_t>(0u),
                           [](size_t accu, const a2::RateSamplerPtr &sampler) {
        return accu + sampler->rateHistory.capacity() * sizeof(double);
   });
}

//
// Analysis
//

static const size_t A2InitialArenaSize = Kilobytes(256);

Analysis::Analysis(QObject *parent)
    : QObject(parent)
    , m_modified(false)
    , m_timetickCount(0.0)
    , m_a2ArenaIndex(0)
{
    m_registry.registerSource<ListFilterExtractor>();
    m_registry.registerSource<Extractor>();

    m_registry.registerOperator<CalibrationMinMax>();
    m_registry.registerOperator<IndexSelector>();
    m_registry.registerOperator<PreviousValue>();
    //m_registry.registerOperator<RetainValid>();
    m_registry.registerOperator<Difference>();
    m_registry.registerOperator<Sum>();
    m_registry.registerOperator<ArrayMap>();
    m_registry.registerOperator<RangeFilter1D>();
    m_registry.registerOperator<ConditionFilter>();
    m_registry.registerOperator<RectFilter2D>();
    m_registry.registerOperator<BinarySumDiff>();
    m_registry.registerOperator<AggregateOps>();
    m_registry.registerOperator<ExpressionOperator>();

    m_registry.registerSink<Histo1DSink>();
    m_registry.registerSink<Histo2DSink>();
    m_registry.registerSink<RateMonitorSink>();

    qDebug() << "Registered Sources:   " << m_registry.getSourceNames();
    qDebug() << "Registered Operators: " << m_registry.getOperatorNames();
    qDebug() << "Registered Sinks:     " << m_registry.getSinkNames();

    // create a2 arenas
    for (size_t i = 0; i < m_a2Arenas.size(); i++)
    {
        m_a2Arenas[i] = std::make_unique<memory::Arena>(A2InitialArenaSize);
    }
    m_a2WorkArena = std::make_unique<memory::Arena>(A2InitialArenaSize);
}

Analysis::~Analysis()
{
}

void Analysis::beginRun(
    const RunInfo &runInfo,
    const vme_analysis_common::VMEIdToIndex &vmeMap)
{
    m_runInfo = runInfo;
    m_vmeMap = vmeMap;

    if (!runInfo.keepAnalysisState)
    {
        m_timetickCount = 0.0;
    }

    // Update operator ranks and then sort. This needs to be done before the a2
    // system can be built.

    updateRanks();

    qSort(m_operators.begin(), m_operators.end(), [] (const OperatorEntry &oe1, const OperatorEntry &oe2) {
        return oe1.op->getMaximumInputRank() < oe2.op->getMaximumInputRank();
    });

#if ENABLE_ANALYSIS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "<<<<< operators sorted by maximum input rank";
    for (const auto &opEntry: m_operators)
    {
        qDebug() << "  "
            << "max input rank =" << opEntry.op->getMaximumInputRank()
            << getClassName(opEntry.op.get())
            << opEntry.op->objectName()
            << ", max output rank =" << opEntry.op->getMaximumOutputRank();
    }
    qDebug() << __PRETTY_FUNCTION__ << ">>>>> operators sorted by maximum input rank";
#endif
    qDebug() << __PRETTY_FUNCTION__ << "analysis::Analysis:"
        << m_sources.size() << " sources,"
        << m_operators.size() << " operators";

    for (auto &sourceEntry: m_sources)
    {
        sourceEntry.source->beginRun(runInfo);
    }

    for (auto &operatorEntry: m_operators)
    {
        operatorEntry.op->beginRun(runInfo);
    }

    // Build the a2 system

    // a2 arena swap
    m_a2ArenaIndex = (m_a2ArenaIndex + 1) % m_a2Arenas.size();
    m_a2Arenas[m_a2ArenaIndex]->reset();
    m_a2WorkArena->reset();

    qDebug() << __PRETTY_FUNCTION__ << "########## a2 active ##########";
    qDebug() << __PRETTY_FUNCTION__ << "a2: using a2 arena" << (u32)m_a2ArenaIndex;

    m_a2State = std::make_unique<A2AdapterState>(
        a2_adapter_build_memory_wrapper(
            m_a2Arenas[m_a2ArenaIndex],
            m_a2WorkArena,
            m_sources,
            m_operators,
            m_vmeMap));
}

void Analysis::beginRun()
{
    beginRun(m_runInfo, m_vmeMap);
}

void Analysis::beginEvent(int eventIndex)
{
    a2_begin_event(m_a2State->a2, eventIndex);
}

void Analysis::processModuleData(int eventIndex, int moduleIndex, u32 *data, u32 size)
{
    a2_process_module_data(m_a2State->a2, eventIndex, moduleIndex, data, size);
}

void Analysis::endEvent(int eventIndex)
{
    a2_end_event(m_a2State->a2, eventIndex);
}

void Analysis::processTimetick()
{
    m_timetickCount += 1.0;
    a2_timetick(m_a2State->a2);
}

void Analysis::addSource(const QUuid &eventId, const QUuid &moduleId, const SourcePtr &source)
{
    m_sources.push_back({eventId, moduleId, source});
    beginRun(m_runInfo, m_vmeMap);
    setModified();
}

void Analysis::addOperator(const QUuid &eventId, const OperatorPtr &op, s32 userLevel)
{
    m_operators.push_back({eventId, op, userLevel});
    beginRun(m_runInfo, m_vmeMap);
    setModified();
}

double Analysis::getTimetickCount() const
{
    return m_timetickCount;
}

void Analysis::updateRanks()
{
#if ENABLE_ANALYSIS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << ">>>>> begin";
#endif

    for (auto &sourceEntry: m_sources)
    {
        SourceInterface *source = sourceEntry.source.get();
        Q_ASSERT(source);
        const s32 outputCount = source->getNumberOfOutputs();

#if ENABLE_ANALYSIS_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "setting output ranks of source"
            << getClassName(source) << source->objectName() << "to 0";
#endif


        for (s32 outputIndex = 0;
             outputIndex < outputCount;
             ++outputIndex)
        {
            source->getOutput(outputIndex)->setRank(0);
        }
    }

    QSet<OperatorInterface *> updated;

    for (auto &opEntry: m_operators)
    {
        OperatorInterface *op = opEntry.op.get();

        updateRank(op, updated);
    }
#if ENABLE_ANALYSIS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "<<<<< end";
#endif
}

void Analysis::updateRank(OperatorInterface *op, QSet<OperatorInterface *> &updated)
{
    if (updated.contains(op))
        return;

#if ENABLE_ANALYSIS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << ">>>>> updating rank for"
        << getClassName(op)
        << op->objectName();
#endif

    for (s32 inputIndex = 0;
         inputIndex < op->getNumberOfSlots();
         ++inputIndex)
    {
        Pipe *input = op->getSlot(inputIndex)->inputPipe;

        if (input)
        {
            PipeSourceInterface *source(input->getSource());

            // Only operators need to be updated. Sources will have their rank
            // set to 0 already.
            OperatorInterface *sourceOp(qobject_cast<OperatorInterface *>(source));

            if (sourceOp)
            {
                updateRank(sourceOp, updated);
            }
        }
        else
        {
#if ENABLE_ANALYSIS_DEBUG
            qDebug() << __PRETTY_FUNCTION__ << "input slot" << inputIndex << "is not connected";
#endif
        }
    }

    s32 maxInputRank = op->getMaximumInputRank();

#if ENABLE_ANALYSIS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "maxInputRank =" << maxInputRank;
#endif

    for (s32 outputIndex = 0;
         outputIndex < op->getNumberOfOutputs();
         ++outputIndex)
    {
        op->getOutput(outputIndex)->setRank(maxInputRank + 1);
        updated.insert(op);

#if ENABLE_ANALYSIS_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << "output"
            << outputIndex << "now has rank"
            << op->getOutput(outputIndex)->getRank();
#endif
    }

#if ENABLE_ANALYSIS_DEBUG
    qDebug() << __PRETTY_FUNCTION__ << "<<<<< updated rank for"
        << getClassName(op)
        << op->objectName()
        << "new output rank" << op->getMaximumOutputRank();
#endif
}

void Analysis::removeSource(const SourcePtr &source)
{
    removeSource(source.get());
}

void Analysis::removeSource(SourceInterface *source)
{
    s32 entryIndex = -1;
    for (s32 i = 0; i < m_sources.size(); ++i)
    {
        if (m_sources[i].source.get() == source)
        {
            entryIndex = i;
            break;
        }
    }

    Q_ASSERT(entryIndex >= 0);

    if (entryIndex >= 0)
    {
        // Remove our output pipes from any connected slots.
        for (s32 outputIndex = 0;
             outputIndex < source->getNumberOfOutputs();
             ++outputIndex)
        {
            Pipe *outPipe = source->getOutput(outputIndex);
            for (Slot *destSlot: outPipe->getDestinations())
            {
                destSlot->disconnectPipe();
            }
            Q_ASSERT(outPipe->getDestinations().isEmpty());
        }

        // Remove the source entry. Releases our reference to the SourcePtr stored there.
        m_sources.remove(entryIndex);

        // Update ranks and recalculate output sizes for all analysis elements.
        beginRun(m_runInfo, m_vmeMap);

        setModified();
    }
}

QVector<ListFilterExtractorPtr>
Analysis::getListFilterExtractors(ModuleConfig *module) const
{
    QVector<ListFilterExtractorPtr> result;

    Q_ASSERT(module->getEventConfig());

    if (!module->getEventConfig())
        return result;

    for (const auto &se: getSources(module->getEventConfig()->getId(), module->getId()))
    {
        if (auto lfe = std::dynamic_pointer_cast<ListFilterExtractor>(se.source))
        {
            result.push_back(lfe);
        }
    }

    return result;
}

/** Replaces the ListFilterExtractors for module with the given extractors. */
void
Analysis::setListFilterExtractors(ModuleConfig *module, const QVector<ListFilterExtractorPtr> &extractors)
{
    Q_ASSERT(module->getEventConfig());

    if (!module->getEventConfig())
        return;

    // Remove all source entries containing a ListFilterExtractor for the module.
    auto it = std::remove_if(m_sources.begin(), m_sources.end(), [module](const SourceEntry &se) {
        return (qobject_cast<ListFilterExtractor *>(se.source.get()) && se.moduleId == module->getId());
    });

    m_sources.erase(it, m_sources.end());

    // Now build new SourceEntries for the given extractors and append them to m_sources
    for (const auto &ex: extractors)
    {
        m_sources.push_back({module->getEventConfig()->getId(), module->getId(), ex});
    }

    qDebug() << __PRETTY_FUNCTION__ << "added" << extractors.size() << "listfilter extractors";

    // Rebuild and notify about modification state
    beginRun(m_runInfo, m_vmeMap);
    setModified();
}

void Analysis::removeOperator(const OperatorPtr &op)
{
    removeOperator(op.get());
}

void Analysis::removeOperator(OperatorInterface *op)
{
    s32 entryIndex = -1;
    for (s32 i = 0; i < m_operators.size(); ++i)
    {
        if (m_operators[i].op.get() == op)
        {
            entryIndex = i;
            break;
        }
    }

    Q_ASSERT(entryIndex >= 0);

    if (entryIndex >= 0)
    {
        // Remove pipe connections to our input slots.
        for (s32 inputSlotIndex = 0;
             inputSlotIndex < op->getNumberOfSlots();
             ++inputSlotIndex)
        {
            Slot *inputSlot = op->getSlot(inputSlotIndex);
            Q_ASSERT(inputSlot);
            inputSlot->disconnectPipe();
            Q_ASSERT(!inputSlot->inputPipe);
        }

        // Remove our output pipes from any connected slots.
        for (s32 outputIndex = 0;
             outputIndex < op->getNumberOfOutputs();
             ++outputIndex)
        {
            Pipe *outPipe = op->getOutput(outputIndex);
            for (Slot *destSlot: outPipe->getDestinations())
            {
                destSlot->disconnectPipe();
            }
            Q_ASSERT(outPipe->getDestinations().isEmpty());
        }

        // Remove the operator entry. Releases our reference to the OperatorPtr stored there.
        m_operators.remove(entryIndex);

        // Update ranks and recalculate output sizes for all analysis elements.
        beginRun(m_runInfo, m_vmeMap);

        setModified();
    }
}

void Analysis::clear()
{
    m_sources.clear();
    m_operators.clear();
    beginRun(m_runInfo, m_vmeMap);
    setModified();
}

bool Analysis::isEmpty() const
{
    return m_sources.isEmpty() && m_operators.isEmpty();
}

s32 Analysis::getNumberOfSinks() const
{
    return std::count_if(m_operators.begin(), m_operators.end(), [](const OperatorEntry &e) {
        return qobject_cast<SinkInterface *>(e.op.get()) != nullptr;
    });
}

size_t Analysis::getTotalSinkStorageSize() const
{
    return std::accumulate(m_operators.begin(), m_operators.end(),
                           static_cast<size_t>(0),
                           [](size_t v, const OperatorEntry &e) {

        if (auto sink = qobject_cast<SinkInterface *>(e.op.get()))
            v += sink->getStorageSize();

        return v;
    });
}

s32 Analysis::getMaxUserLevel() const
{
    auto it = std::max_element(m_operators.begin(), m_operators.end(), [](const auto &a, const auto &b) {
        return a.userLevel < b.userLevel;
    });

    return (it != m_operators.end() ? it->userLevel : 0);
}

s32 Analysis::getMaxUserLevel(const QUuid &eventId) const
{
    auto ops = getOperators(eventId);

    auto it = std::max_element(ops.begin(), ops.end(), [](const auto &a, const auto &b) {
        return a.userLevel < b.userLevel;
    });

    return (it != ops.end() ? it->userLevel : 0);
}

Analysis::ReadResult Analysis::read(const QJsonObject &inputJson, VMEConfig *vmeConfig)
{
    clear();

    ReadResult result = {};

    int version = get_version(inputJson);

    if (version > CurrentAnalysisVersion)
    {
        result.code = VersionTooNew;
        result.errorData["File version"] = version;
        result.errorData["Max supported version"] = CurrentAnalysisVersion;
        result.errorData["Message"] = QSL("The file was generated by a newer version of mvme. Please upgrade.");
        return result;
    }

    QJsonObject json = convert_to_current_version(inputJson, vmeConfig);

    QMap<QUuid, PipeSourcePtr> objectsById;

    // Sources
    {
        QJsonArray array = json["sources"].toArray();

        for (auto it = array.begin(); it != array.end(); ++it)
        {
            auto objectJson = it->toObject();
            auto className = objectJson["class"].toString();

            SourcePtr source(m_registry.makeSource(className));

            if (source)
            {
                source->setId(QUuid(objectJson["id"].toString()));
                source->setObjectName(objectJson["name"].toString());
                source->read(objectJson["data"].toObject());

                auto eventId  = QUuid(objectJson["eventId"].toString());
                auto moduleId = QUuid(objectJson["moduleId"].toString());

                m_sources.push_back({eventId, moduleId, source});

                objectsById.insert(source->getId(), source);
            }
        }
    }

    // Operators
    {
        QJsonArray array = json["operators"].toArray();

        for (auto it = array.begin(); it != array.end(); ++it)
        {
            auto objectJson = it->toObject();
            auto className = objectJson["class"].toString();

            OperatorPtr op(m_registry.makeOperator(className));

            // No operator with the given name exists, try a sink instead.
            if (!op)
            {
                op.reset(m_registry.makeSink(className));
            }

            if (op)
            {
                op->setId(QUuid(objectJson["id"].toString()));
                op->setObjectName(objectJson["name"].toString());
                op->read(objectJson["data"].toObject());

                auto eventId = QUuid(objectJson["eventId"].toString());
                auto userLevel = objectJson["userLevel"].toInt();

                m_operators.push_back({eventId, op, userLevel});

                objectsById.insert(op->getId(), op);
            }
        }
    }

    // Connections
    {
        /* Connections are defined by a structure looking like this:
        struct Connection
        {
            PipeSourceInterface *srcObject;
            s32 srcIndex; // the output index of the source object

            OperatorInterface *dstObject;
            s32 dstIndex; // the input index of the dest object
            s32 dstParamIndex; // array index the input uses or Slot::NoParamIndex if the whole input is used
        };
        */

        QJsonArray array = json["connections"].toArray();
        for (auto it = array.begin(); it != array.end(); ++it)
        {
            auto objectJson = it->toObject();

            // PipeSourceInterface data
            QUuid srcId(objectJson["srcId"].toString());
            s32 srcIndex = objectJson["srcIndex"].toInt();

            // OperatorInterface data
            QUuid dstId(objectJson["dstId"].toString());
            s32 dstIndex = objectJson["dstIndex"].toInt();

            // Slot data
            s32 paramIndex = objectJson["dstParamIndex"].toInt();

            auto srcObject = objectsById.value(srcId);
            auto dstObject = std::dynamic_pointer_cast<OperatorInterface>(objectsById.value(dstId));

            if (srcObject && dstObject)
            {
                auto srcRawPtr = srcObject.get();
                auto dstRawPtr = dstObject.get();
                Slot *dstSlot = dstObject->getSlot(dstIndex);
                Q_ASSERT(dstSlot);

                if (dstSlot)
                {
                    dstSlot->paramIndex = paramIndex;

                    Pipe *thePipe = srcRawPtr->getOutput(srcIndex);

                    Q_ASSERT(thePipe);
                    Q_ASSERT(thePipe->source == srcRawPtr);

                    dstRawPtr->connectInputSlot(dstIndex, thePipe, paramIndex);

                    Q_ASSERT(thePipe->destinations.contains(dstSlot));
                }
            }
        }
    }

    // Dynamic QObject Properties
    loadDynamicProperties(json["properties"].toObject(), this);

    //beginRun(m_runInfo, m_vmeMap);
    setModified(false);

    return result;
}

void Analysis::write(QJsonObject &json) const
{
    json["MVMEAnalysisVersion"] = CurrentAnalysisVersion;

    // Sources
    {
        QJsonArray destArray;
        for (auto &sourceEntry: m_sources)
        {
            SourceInterface *source = sourceEntry.source.get();
            QJsonObject destObject;
            destObject["id"] = source->getId().toString();
            destObject["name"] = source->objectName();
            destObject["eventId"]  = sourceEntry.eventId.toString();
            destObject["moduleId"] = sourceEntry.moduleId.toString();
            destObject["class"] = getClassName(source);
            QJsonObject dataJson;
            source->write(dataJson);
            destObject["data"] = dataJson;
            destArray.append(destObject);
        }
        json["sources"] = destArray;
    }

    // Operators
    {
        QJsonArray destArray;
        for (auto &opEntry: m_operators)
        {
            OperatorInterface *op = opEntry.op.get();
            QJsonObject destObject;
            destObject["id"] = op->getId().toString();
            destObject["name"] = op->objectName();
            destObject["eventId"]  = opEntry.eventId.toString();
            destObject["class"] = getClassName(op);
            destObject["userLevel"] = opEntry.userLevel;
            QJsonObject dataJson;
            op->write(dataJson);
            destObject["data"] = dataJson;
            destArray.append(destObject);
        }
        json["operators"] = destArray;
    }

    // Connections
    {
        QJsonArray conArray;
        QVector<PipeSourceInterface *> pipeSources;

        for (auto &sourceEntry: m_sources)
        {
            pipeSources.push_back(sourceEntry.source.get());
        }

        for (auto &opEntry: m_operators)
        {
            pipeSources.push_back(opEntry.op.get());
        }

        for (PipeSourceInterface *srcObject: pipeSources)
        {
            for (s32 outputIndex = 0; outputIndex < srcObject->getNumberOfOutputs(); ++outputIndex)
            {
                Pipe *pipe = srcObject->getOutput(outputIndex);

                for (Slot *dstSlot: pipe->getDestinations())
                {
                    auto dstOp = dstSlot->parentOperator;
                    if (dstOp)
                    {
                        //qDebug() << "Connection:" << srcObject << outputIndex << "->" << dstOp << dstSlot << dstSlot->parentSlotIndex;
                        QJsonObject conJson;
                        conJson["srcId"] = srcObject->getId().toString();
                        conJson["srcIndex"] = outputIndex;
                        conJson["dstId"] = dstOp->getId().toString();
                        conJson["dstIndex"] = dstSlot->parentSlotIndex;
                        conJson["dstParamIndex"] = static_cast<qint64>(dstSlot->paramIndex);
                        conArray.append(conJson);
                    }
                }
            }
        }

        json["connections"] = conArray;
    }

    // Dynamic QObject Properties
    auto props = storeDynamicProperties(this);

    if (!props.isEmpty())
        json["properties"] = props;
}

void Analysis::setModified(bool b)
{
    emit modified(b);

    if (m_modified != b)
    {
        m_modified = b;
        emit modifiedChanged(b);
    }
}

static const double maxRawHistoBins = (1 << 16);

RawDataDisplay make_raw_data_display(std::shared_ptr<Extractor> extractor, double unitMin, double unitMax,
                                     const QString &xAxisTitle, const QString &unitLabel)
{
    RawDataDisplay result;
    result.extractor = extractor;

    auto objectName = extractor->objectName();
    auto extractionFilter = extractor->getFilter();

    double srcMin = 0.0;
    double srcMax = std::pow(2.0, extractionFilter.getDataBits());
    u32 histoBins = static_cast<u32>(std::min(srcMax, maxRawHistoBins));

    auto calibration = std::make_shared<CalibrationMinMax>();
    calibration->setObjectName(objectName);
    calibration->setUnitLabel(unitLabel);
    calibration->connectArrayToInputSlot(0, extractor->getOutput(0));

    const u32 addressCount = 1u << extractionFilter.getAddressBits();

    for (u32 addr = 0; addr < addressCount; ++addr)
    {
        calibration->setCalibration(addr, unitMin, unitMax);
    }

    result.calibration = calibration;

    auto rawHistoSink = std::make_shared<Histo1DSink>();
    rawHistoSink->setObjectName(QString("%1_raw").arg(objectName));
    rawHistoSink->m_bins = histoBins;
    rawHistoSink->m_xAxisTitle = xAxisTitle;
    result.rawHistoSink = rawHistoSink;

    auto calHistoSink = std::make_shared<Histo1DSink>();
    calHistoSink->setObjectName(QString("%1").arg(objectName));
    calHistoSink->m_bins = histoBins;
    calHistoSink->m_xAxisTitle = xAxisTitle;
    result.calibratedHistoSink = calHistoSink;

    rawHistoSink->connectArrayToInputSlot(0, extractor->getOutput(0));
    calHistoSink->connectArrayToInputSlot(0, calibration->getOutput(0));

    return result;
}

RawDataDisplay make_raw_data_display(const MultiWordDataFilter &extractionFilter, double unitMin, double unitMax,
                                     const QString &objectName, const QString &xAxisTitle, const QString &unitLabel)
{
    auto extractor = std::make_shared<Extractor>();
    extractor->setFilter(extractionFilter);
    extractor->setObjectName(objectName);

    return make_raw_data_display(extractor, unitMin, unitMax, xAxisTitle, unitLabel);
}

void add_raw_data_display(Analysis *analysis, const QUuid &eventId, const QUuid &moduleId, const RawDataDisplay &display)
{
    analysis->addSource(eventId, moduleId, display.extractor);
    analysis->addOperator(eventId, display.rawHistoSink, 0);
    analysis->addOperator(eventId, display.calibration, 1);
    analysis->addOperator(eventId, display.calibratedHistoSink, 1);
}

void do_beginRun_forward(PipeSourceInterface *pipeSource, const RunInfo &runInfo)
{
    Q_ASSERT(pipeSource);

    qDebug() << __PRETTY_FUNCTION__ << "calling beginRun() on" << pipeSource;
    pipeSource->beginRun(runInfo);

    const s32 outputCount = pipeSource->getNumberOfOutputs();

    for (s32 outputIndex = 0;
         outputIndex < outputCount;
         ++outputIndex)
    {
        Pipe *outPipe = pipeSource->getOutput(outputIndex);
        Q_ASSERT(outPipe); // Must exist as the source said it would exist.

        // Copy destinations vector here as disconnectPipe() below will modify it.
        auto destinations = outPipe->destinations;
        const s32 destCount = destinations.size();

        for (s32 destIndex = 0;
             destIndex < destCount;
             ++destIndex)
        {
            Slot *destSlot = destinations[destIndex];

            if (destSlot)
            {
                Q_ASSERT(destSlot->parentOperator);

                auto destOperator = destSlot->parentOperator;

                // Check all the slots of the destination operator. This is
                // necessary as multiple slots might be connected to the
                // current pipeSource.
                for (s32 destSlotIndex = 0;
                     destSlotIndex < destOperator->getNumberOfSlots();
                     ++destSlotIndex)
                {
                    Slot *parentSlot = destOperator->getSlot(destSlotIndex);

                    if (parentSlot->inputPipe
                        && parentSlot->paramIndex != Slot::NoParamIndex
                        && parentSlot->paramIndex >= parentSlot->inputPipe->parameters.size())
                    {
                        // The paramIndex is out of range. This can happen when
                        // e.g. an Extractor is edited and the new number of
                        // address bits is less than the old number.
                        // For now handle this case by just disconnecting the slot.
                        // It will then be highlighted in the UI.
                        qDebug() << __PRETTY_FUNCTION__
                            << "disconnecting Slot" << parentSlot->name << "of operator" << destOperator
                            << "because its paramIndex is now out of range";
                        parentSlot->disconnectPipe();
                    }
                }

                do_beginRun_forward(destSlot->parentOperator);
            }
        }
    }
}

QString make_unique_operator_name(Analysis *analysis, const QString &prefix)
{
    int suffixNumber = 0;

    for (const auto &opEntry: analysis->getOperators())
    {
        const auto &op(opEntry.op);
        auto name = op->objectName();

        if (name.startsWith(prefix))
        {
            QString suffix;
            if (name.size() - 1 > prefix.size())
            {
                suffix = name.right(name.size() - prefix.size() - 1);
            }

            if (suffix.size())
            {
                bool ok;
                int n = suffix.toInt(&ok);
                if (ok)
                {
                    suffixNumber = std::max(suffixNumber, n);
                }
            }
        }
    }

    ++suffixNumber;

    return prefix + QSL(".") + QString::number(suffixNumber);
}

bool all_inputs_connected(OperatorInterface *op)
{
    bool result = true;

    for (s32 slotIndex = 0;
         slotIndex < op->getNumberOfSlots();
         ++slotIndex)
    {
        result = result && op->getSlot(slotIndex)->isConnected();
    }

    return result;
}

bool no_input_connected(OperatorInterface *op)
{
    bool result = true;

    for (s32 slotIndex = 0;
         slotIndex < op->getNumberOfSlots();
         ++slotIndex)
    {
        result = result && !op->getSlot(slotIndex)->isConnected();
    }

    return result;
}

void generate_new_object_ids(Analysis *analysis)
{
    for (auto &sourceEntry: analysis->getSources())
        sourceEntry.source->setId(QUuid::createUuid());

    for (auto &operatorEntry: analysis->getOperators())
        operatorEntry.op->setId(QUuid::createUuid());
}

QString info_string(const Analysis *analysis)
{
    QString result = QString("Analysis: %1 Data Sources, %2 Operators")
        .arg(analysis->getNumberOfSources())
        .arg(analysis->getNumberOfOperators());

    return result;
}

static void adjust_userlevel_forward(QVector<Analysis::OperatorEntry> &opEntries, OperatorInterface *op, s32 levelDelta,
                                     QSet<OperatorInterface *> &adjusted)
{
    // Note: Could be optimized by searching from the last operators position
    // forward, as the vector is sorted by operator rank: adjust_userlevel_forward(entryIt, endIt, op, ...)

    if (adjusted.contains(op) || levelDelta == 0)
        return;

    auto entryIt = std::find_if(opEntries.begin(), opEntries.end(), [op](const auto &opEntry) {
        return opEntry.op.get() == op;
    });

    if (entryIt != opEntries.end())
    {
        auto &opEntry(*entryIt);

        opEntry.userLevel += levelDelta;
        adjusted.insert(op);

        for (s32 outputIndex = 0;
             outputIndex < op->getNumberOfOutputs();
             ++outputIndex)
        {
            auto outputPipe = op->getOutput(outputIndex);

            for (Slot *destSlot: outputPipe->getDestinations())
            {
                if (destSlot->parentOperator)
                {
                    adjust_userlevel_forward(opEntries, destSlot->parentOperator, levelDelta, adjusted);
                }
            }
        }
    }
}

void adjust_userlevel_forward(QVector<Analysis::OperatorEntry> &opEntries, OperatorInterface *op, s32 levelDelta)
{
    QSet<OperatorInterface *> adjusted;

    adjust_userlevel_forward(opEntries, op, levelDelta, adjusted);
}

}
