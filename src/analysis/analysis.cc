#include "analysis.h"
#include <QJsonArray>
#include <QJsonObject>

#include <chrono>
#include <random>

#define ENABLE_ANALYSIS_DEBUG 0

template<typename T>
QDebug &operator<< (QDebug &dbg, const std::shared_ptr<T> &ptr)
{
    dbg << ptr.get();
    return dbg;
}

namespace analysis
{
    static const int CurrentAnalysisVersion = 1;

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
{
    m_output.setSource(this);

    // Generate a random seed for the rng. This seed will be written out in
    // write() and restored in read().
    std::random_device rd;
    std::uniform_int_distribution<u64> dist;
    m_rngSeed = dist(rd);
}

void Extractor::beginRun()
{
    m_currentCompletionCount = 0;

    u32 addressCount = 1 << m_filter.getAddressBits();
    u32 upperLimit = (1 << m_filter.getDataBits()) - 1;

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
}

void Extractor::beginEvent()
{
    m_output.getParameters().invalidateAll();
}

void Extractor::processDataWord(u32 data, s32 wordIndex)
{
    m_filter.handleDataWord(data, wordIndex);

    if (m_filter.isComplete())
    {
        ++m_currentCompletionCount;

        if (m_requiredCompletionCount == m_currentCompletionCount)
        {
            u64 value   = m_filter.getResultValue();
            s32 address = m_filter.getResultAddress();

            auto &param = m_output.getParameters()[address];
            // Only fill if not valid to keep the first value in case of multiple hits in this event
            if (!param.valid)
            {
                double dValue = value + RealDist01(m_rng);

                param.valid = true;
                param.value = dValue;


#if ENABLE_ANALYSIS_DEBUG
                qDebug() << this << "setting param valid, addr =" << address << ", value =" << param.value
                    << ", dataWord =" << QString("0x%1").arg(data, 8, 16, QLatin1Char('0'));
#endif
            }

            m_currentCompletionCount = 0;
        }
        m_filter.clearCompletion();
    }

#if 0
    qDebug() << this << "output:";
    auto &params(m_output.getParameters());

    for (s32 ip=0; ip<params.size(); ++ip)
    {
        auto &param(params[ip]);
        if (param.valid)
            qDebug() << "        " << ip << "=" << to_string(param);
        else
            qDebug() << "        " << ip << "=" << "<not valid>";
    }
#endif
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
        DataFilter filter(filterString, wordIndex);
        m_filter.addSubFilter(filter);
    }

    setRequiredCompletionCount(static_cast<u32>(json["requiredCompletionCount"].toInt()));
}

void Extractor::write(QJsonObject &json) const
{
    json["rngSeed"] = QString::number(m_rngSeed, 16);

    QJsonArray filterArray;
    for (auto dataFilter: m_filter.getSubFilters())
    {
        QJsonObject filterJson;
        filterJson["filterString"] = QString::fromLocal8Bit(dataFilter.getFilter());
        filterJson["wordIndex"] = dataFilter.getWordIndex();
        filterArray.append(filterJson);
    }

    json["subFilters"] = filterArray;
    json["requiredCompletionCount"] = static_cast<qint64>(m_requiredCompletionCount);
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

void CalibrationMinMax::beginRun()
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

        s32 outIdx = 0;
        for (s32 idx = idxMin; idx < idxMax; ++idx)
        {
            const Parameter &inParam(in[idx]);
            Parameter &outParam(out[outIdx++]);
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
            outParam.valid = true;
        }
    };

    if (m_inputSlot.inputPipe)
    {
        auto &out(m_output.getParameters());
        const auto &in(m_inputSlot.inputPipe->getParameters());
        const s32 inSize = in.size();

        if (m_inputSlot.paramIndex != Slot::NoParamIndex)
        {
            Q_ASSERT(out.size() == 1);
            auto &outParam(out[0]);
            outParam.valid = false;
            s32 paramIndex = m_inputSlot.paramIndex;

            if (paramIndex >= 0 && paramIndex < inSize)
            {
                const auto &inParam(in[paramIndex]);
                calibOneParam(inParam, outParam, m_globalCalibration);
            }
        }
        else
        {
            const s32 size = in.size();
            Q_ASSERT(out.size() == size);

            for (s32 address = 0; address < size; ++address)
            {
                auto &outParam(out[address]);
                const auto &inParam(in[address]);

                calibOneParam(inParam, outParam, getCalibration(address));
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
    CalibrationMinMaxParameters result = m_globalCalibration;

    if (address < m_calibrations.size() && m_calibrations[address].isValid())
    {
        result = m_calibrations[address];
    }

    return result;
}

void CalibrationMinMax::read(const QJsonObject &json)
{
    m_unit = json["unitLabel"].toString();
    m_globalCalibration.unitMin = json["globalUnitMin"].toDouble();
    m_globalCalibration.unitMax = json["globalUnitMax"].toDouble();

    m_calibrations.clear();
    QJsonArray calibArray = json["calibrations"].toArray();

    for (auto it=calibArray.begin();
         it != calibArray.end();
         ++it)
    {
        auto paramJson = it->toObject();
        CalibrationMinMaxParameters param;
        if (paramJson.contains("unitMin") && paramJson.contains("unitMax"))
        {
            param.unitMin = paramJson["unitMin"].toDouble();
            param.unitMax = paramJson["unitMax"].toDouble();
        }
        m_calibrations.push_back(param);
    }
}

void CalibrationMinMax::write(QJsonObject &json) const
{
    json["unitLabel"] = m_unit;
    json["globalUnitMin"] = m_globalCalibration.unitMin;
    json["globalUnitMax"] = m_globalCalibration.unitMax;

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

void IndexSelector::beginRun()
{
    auto &out(m_output.getParameters());


    if (m_inputSlot.inputPipe)
    {
        const auto &in(m_inputSlot.inputPipe->getParameters());

        out.resize(1);
        out.name = in.name;
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

void PreviousValue::beginRun()
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
        out.name = in.name;
        out.unit = in.unit;

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

void RetainValid::beginRun()
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
        out.name = in.name;
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

void Difference::beginRun()
{
    m_output.parameters.name = QSL("A-B"); // FIXME
    m_output.parameters.unit = QString();

    if (!(m_inputA.inputPipe && m_inputB.inputPipe))
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
    if (!(m_inputA.inputPipe && m_inputB.inputPipe))
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

void Sum::beginRun()
{
    // FIXME: limits!

    auto &out(m_output.getParameters());

    if (m_inputSlot.inputPipe)
    {
        out.resize(1);

        const auto &in(m_inputSlot.inputPipe->getParameters());
        out.name = in.name;
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
// Histo1DSink
//
Histo1DSink::Histo1DSink(QObject *parent)
    : BasicSink(parent)
{
}

void Histo1DSink::beginRun()
{
    // Instead of just clearing the histos vector and recreating it this code
    // tries to reuse existing histograms. This is done so that open histogram
    // windows still reference the correct histogram after beginRun() is
    // invoked. Otherwise the user would have to reopen histogram windows quite
    // frequently.

    if (m_inputSlot.inputPipe)
    {
        Q_ASSERT(m_bins);
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
                auto histo = m_histos[histoIndex];
                histo->resize(m_bins); // calls clear() even if the size does not change
                histo->setAxisBinning(Qt::XAxis, AxisBinning(m_bins, xMin, xMax));
            }
            else
            {
                m_histos[histoIndex] = std::make_shared<Histo1D>(m_bins, xMin, xMax);
            }

            auto histo = m_histos[histoIndex];
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
        }
    }
    else
    {
        m_histos.resize(0);
    }
}

void Histo1DSink::step()
{
    if (m_inputSlot.inputPipe && !m_histos.isEmpty())
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

#if 0
    QJsonArray histosJson = json["histos"].toArray();

    for (auto it=histosJson.begin();
         it != histosJson.end();
         ++it)
    {
        auto objectJson = it->toObject();

        u32 nBins = static_cast<u32>(objectJson["nBins"].toInt());
        double xMin = objectJson["xMin"].toDouble();
        double xMax = objectJson["xMax"].toDouble();
        auto histo = std::make_shared<Histo1D>(nBins, xMin, xMax);
        m_histos.push_back(histo);
    }
#endif
}

void Histo1DSink::write(QJsonObject &json) const
{
    json["nBins"] = static_cast<qint64>(m_bins);
    json["xAxisTitle"] = m_xAxisTitle;
    json["xLimitMin"]  = m_xLimitMin;
    json["xLimitMax"]  = m_xLimitMax;

#if 0
    QJsonArray histosJson;

    for (const auto &histo: m_histos)
    {
        QJsonObject objectJson;
        objectJson["nBins"] = static_cast<qint64>(histo->getNumberOfBins());
        objectJson["xMin"] = histo->getXMin();
        objectJson["xMax"] = histo->getXMax();
        histosJson.append(objectJson);
    }

    json["histos"] = histosJson;
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
void Histo2DSink::beginRun()
{
    if (m_inputX.inputPipe && m_inputY.inputPipe)
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

            m_histo->setObjectName(objectName());
        }
        else
        {
            m_histo->resize(m_xBins, m_yBins);

            m_histo->setAxisBinning(Qt::XAxis, AxisBinning(m_xBins, xMin, xMax));
            m_histo->setAxisBinning(Qt::YAxis, AxisBinning(m_yBins, yMin, yMax));
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

//
// Analysis
//
const QMap<Analysis::ReadResult::Code, const char *> Analysis::ReadResult::ErrorCodeStrings =
{
    { NoError, "No Error" },
    { VersionMismatch, "Version Mismatch" },
};

Analysis::Analysis()
{
    m_registry.registerSource<Extractor>();

    //m_registry.registerOperator<CalibrationFactorOffset>();
    m_registry.registerOperator<CalibrationMinMax>();
    m_registry.registerOperator<IndexSelector>();
    m_registry.registerOperator<PreviousValue>();
    //m_registry.registerOperator<RetainValid>();
    m_registry.registerOperator<Difference>();
    m_registry.registerOperator<Sum>();


    m_registry.registerSink<Histo1DSink>();
    m_registry.registerSink<Histo2DSink>();

    qDebug() << "Registered Sources:   " << m_registry.getSourceNames();
    qDebug() << "Registered Operators: " << m_registry.getOperatorNames();
    qDebug() << "Registered Sinks:     " << m_registry.getSinkNames();
}

void Analysis::beginRun()
{
    updateRanks();

    qSort(m_operators.begin(), m_operators.end(), [] (const OperatorEntry &oe1, const OperatorEntry &oe2) {
        return oe1.op->getMaximumInputRank() < oe2.op->getMaximumInputRank();
    });

    for (auto &sourceEntry: m_sources)
    {
        sourceEntry.source->beginRun();
    }

    for (auto &operatorEntry: m_operators)
    {
        operatorEntry.op->beginRun();
    }

    qDebug() << "Analysis NG:"
        << m_sources.size() << " sources,"
        << m_operators.size() << " operators";
}

void Analysis::beginEvent(s32 eventIndex)
{
    for (auto &sourceEntry: m_sources)
    {
        if (sourceEntry.eventIndex == eventIndex)
        {
            sourceEntry.source->beginEvent();
        }
    }
}

void Analysis::addSource(s32 eventIndex, s32 moduleIndex, const SourcePtr &source)
{
    source->beginRun();
    m_sources.push_back({eventIndex, moduleIndex, source});
    updateRanks();
}

void Analysis::addOperator(s32 eventIndex, const OperatorPtr &op, s32 userLevel)
{
    op->beginRun();
    m_operators.push_back({eventIndex, op, userLevel});
    updateRanks();
}

using HighResClock = std::chrono::high_resolution_clock;

struct TimedBlock
{
    TimedBlock(const char *name_)
        : name(name_)
        , start(HighResClock::now())
    {
    }

    ~TimedBlock()
    {
        end = HighResClock::now();
        std::chrono::duration<double, std::nano> diff = end - start;
        qDebug() << "end timed block" << name << diff.count() << "ns";
    }

    const char *name;
    HighResClock::time_point start;
    HighResClock::time_point end;
};

void Analysis::processDataWord(s32 eventIndex, s32 moduleIndex, u32 data, s32 wordIndex)
{
    //TimedBlock tb(__PRETTY_FUNCTION__);

    for (auto &sourceEntry: m_sources)
    {
        if (sourceEntry.eventIndex == eventIndex && sourceEntry.moduleIndex == moduleIndex)
        {
            sourceEntry.source->processDataWord(data, wordIndex);
        }
    }
}

void Analysis::endEvent(s32 eventIndex)
{
    //TimedBlock tb(__PRETTY_FUNCTION__);
    /* In beginRun() operators are sorted by rank. This way step()'ing
     * operators can be done by just traversing the array. */

#if ENABLE_ANALYSIS_DEBUG
    qDebug() << "begin endEvent()" << eventIndex;
#endif

    for (auto &opEntry: m_operators)
    {
        if (opEntry.eventIndex == eventIndex)
        {
            OperatorInterface *op = opEntry.op.get();

#if ENABLE_ANALYSIS_DEBUG
            qDebug() << "  stepping operator" << opEntry.op.get()
                << ", input rank =" << opEntry.op->getMaximumInputRank()
                << ", output rank =" << opEntry.op->getMaximumOutputRank()
                ;
#endif
            opEntry.op->step();
        }
    }

#if ENABLE_ANALYSIS_DEBUG
    qDebug() << "  >>> Sources <<<";
    for (auto &entry: m_sources)
    {
        auto source = entry.source;
        qDebug() << "    Source: e =" << entry.eventIndex << ", m =" << entry.moduleIndex << ", src =" << source.get();

        for (s32 outputIndex = 0; outputIndex < source->getNumberOfOutputs(); ++outputIndex)
        {
            auto pipe = source->getOutput(outputIndex);
            const auto &params = pipe->getParameters();
            qDebug() << "      Output#" << outputIndex << ", name =" << params.name << ", unit =" << params.unit << ", size =" << params.size();
            for (s32 ip=0; ip<params.size(); ++ip)
            {
                auto &param(params[ip]);
                if (param.valid)
                    qDebug() << "        " << ip << "=" << to_string(param);
                else
                    qDebug() << "        " << ip << "=" << "<not valid>";
            }
        }
    }
    qDebug() << "  <<< End Sources >>>";

    qDebug() << "  >>> Operators <<<";
    for (auto &entry: m_operators)
    {
        auto op = entry.op;

        if (op->getNumberOfOutputs() == 0)
            continue;

        qDebug() << "    Op: e =" << entry.eventIndex << ", op =" << op.get();

        for (s32 outputIndex = 0; outputIndex < op->getNumberOfOutputs(); ++outputIndex)
        {
            auto pipe = op->getOutput(outputIndex);
            const auto &params = pipe->getParameters();
            qDebug() << "      Output#" << outputIndex << ", name =" << params.name << ", unit =" << params.unit;
            for (s32 ip=0; ip<params.size(); ++ip)
            {
                auto &param(params[ip]);
                if (param.valid)
                    qDebug() << "        " << ip << "=" << to_string(param);
                else
                    qDebug() << "        " << ip << "=" << "<not valid>";
            }
        }
    }
    qDebug() << " <<< End Operators >>>";
#endif

#if ENABLE_ANALYSIS_DEBUG
    qDebug() << "end endEvent()" << eventIndex;
#endif
}

void Analysis::updateRanks()
{
    for (auto &sourceEntry: m_sources)
    {
        SourceInterface *source = sourceEntry.source.get();
        const s32 outputCount = source->getNumberOfOutputs();

        for (s32 outputIndex = 0;
             outputIndex < source->getNumberOfOutputs();
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
}

void Analysis::updateRank(OperatorInterface *op, QSet<OperatorInterface *> &updated)
{
    if (updated.contains(op))
        return;


    for (s32 inputIndex = 0;
         inputIndex < op->getNumberOfSlots();
         ++inputIndex)
    {
        Pipe *input = op->getSlot(inputIndex)->inputPipe;

        if (input)
        {

            PipeSourceInterface *source(input->getSource());
            OperatorInterface *sourceOp(qobject_cast<OperatorInterface *>(source));

            if (sourceOp)
            {
                updateRank(sourceOp, updated);
            }
            else
            {
                input->setRank(0);
            }
        }
    }

    s32 maxInputRank = op->getMaximumInputRank();

    for (s32 outputIndex = 0;
         outputIndex < op->getNumberOfOutputs();
         ++outputIndex)
    {
        op->getOutput(outputIndex)->setRank(maxInputRank + 1);
        updated.insert(op);
    }
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
        beginRun();
    }
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
        beginRun();
    }
}

void Analysis::clear()
{
    m_sources.clear();
    m_operators.clear();
}

template<typename T>
QString getClassName(T *obj)
{
    return obj->metaObject()->className();
}

Analysis::ReadResult Analysis::read(const QJsonObject &json)
{
    clear();

    QMap<QUuid, PipeSourcePtr> objectsById;

    ReadResult result = {};

    // Bit of a hack: check the version number only if there's a matching key
    // in the json data. This works around the problem that loading an empty
    // config led to a version mismatch error.
    if (json.contains(QSL("MVMEAnalysisVersion")))
    {
        int version = json[QSL("MVMEAnalysisVersion")].toInt(0);

        if (version != CurrentAnalysisVersion)
        {
            result.code = ReadResult::VersionMismatch;
            result.data["version"] = version;
            result.data["expected version"] = CurrentAnalysisVersion;
            return result;
        }
    }

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

                addSource(objectJson["eventIndex"].toInt(),
                          objectJson["moduleIndex"].toInt(),
                          source);

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

                addOperator(objectJson["eventIndex"].toInt(),
                            op,
                            objectJson["userLevel"].toInt()
                           );

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
            u32 acceptedInputTypes; // input type mask the slot accepts (Array | Value)
            s32 paramIndex; // if input type is a single value this is the array index of that value
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
                Q_ASSERT(dstSlot); // FIXME: testing

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

    // Compound objects
    {
#if 0 // TODO: RawDataDisplays are not stored anymore. Delete this code sometime soon
        // FIXME: error checking
        QJsonArray sourceArray = json["rawDataDisplays"].toArray();

        for (auto it = sourceArray.begin(); it != sourceArray.end(); ++it)
        {
            auto objectJson = it->toObject();

            RawDataDisplay display;

            display.id = QUuid(objectJson["id"].toString());
            display.extractor = std::dynamic_pointer_cast<SourceInterface>(objectsById.value(QUuid(objectJson["extractorId"].toString())));
            display.calibration = std::dynamic_pointer_cast<OperatorInterface>(objectsById.value(QUuid(objectJson["calibrationId"].toString())));

            auto rawSinkArray = objectJson["rawHistoSinks"].toArray();

            for (auto jt = rawSinkArray.begin(); jt != rawSinkArray.end(); ++jt)
            {
                auto rawSinkJson = jt->toObject();
                auto selector = std::dynamic_pointer_cast<OperatorInterface>(
                    objectsById.value(QUuid(rawSinkJson["selectorId"].toString())));
                auto histoSink = std::dynamic_pointer_cast<OperatorInterface>(
                    objectsById.value(QUuid(rawSinkJson["histoSinkId"].toString())));

                display.rawHistoSinks.push_back({selector, histoSink});
            }

            auto cmp = [](const RawDataDisplay::RawHistoSink &aS, const RawDataDisplay::RawHistoSink &bS)
            {
                auto a = qobject_cast<IndexSelector *>(aS.selector.get());
                auto b = qobject_cast<IndexSelector *>(bS.selector.get());

                return (a && b && a->getIndex() < b->getIndex());
            };

            // make sure selectors and histosinks are in selector address order!
            qSort(display.rawHistoSinks.begin(), display.rawHistoSinks.end(), cmp);


            rawDataDisplays.push_back(display);
        }
#endif
    }
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
            destObject["eventIndex"]  = static_cast<qint64>(sourceEntry.eventIndex);
            destObject["moduleIndex"] = static_cast<qint64>(sourceEntry.moduleIndex);
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
            destObject["eventIndex"]  = static_cast<qint64>(opEntry.eventIndex);
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
                        qDebug() << "Connection:" << srcObject << outputIndex << "->" << dstOp << dstSlot << dstSlot->parentSlotIndex;
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

    // Compound objects
    {
#if 0 // TODO: RawDataDisplays are not stored anymore. Delete this code sometime soon
        QJsonArray destArray;

        for (auto &display: rawDataDisplays)
        {
            QJsonObject displayObject;
            displayObject["id"] = display.id.toString();
            displayObject["extractorId"] = display.extractor->getId().toString();
            displayObject["calibrationId"] = display.calibration->getId().toString();

            QJsonArray rawSinkArray;

            for (const auto &rawSink: display.rawHistoSinks)
            {
                QJsonObject rawSinkJson;
                rawSinkJson["selectorId"] = rawSink.selector->getId().toString();
                rawSinkJson["histoSinkId"] = rawSink.histoSink->getId().toString();
                rawSinkArray.append(rawSinkJson);
            }

            displayObject["rawHistoSinks"] = rawSinkArray;

            destArray.append(displayObject);
        }

        json["rawDataDisplays"] = destArray;
#endif
    }
}

static const u32 maxRawHistoBins = (1 << 16);

        /* TODO/FIXME:
         * - histo axis titles are still missing
         * - easier to use MultiWordDataFilter constructor
         * - IndexSelector.m_index is signed because of Qts containers using a
         *   signed type for their sizes. What happens if the index is actually
         *   negative?
         */

RawDataDisplay make_raw_data_display(std::shared_ptr<Extractor> extractor, double unitMin, double unitMax,
                                     const QString &xAxisTitle, const QString &unitLabel)
{
    RawDataDisplay result;
    result.extractor = extractor;

    auto objectName = extractor->objectName();
    auto extractionFilter = extractor->getFilter();

    double srcMin  = 0.0;
    double srcMax  = (1 << extractionFilter.getDataBits());
    u32 histoBins  = std::min(static_cast<u32>(srcMax), maxRawHistoBins);

    auto calibration = std::make_shared<CalibrationMinMax>();
    calibration->setObjectName(objectName);
    calibration->setGlobalCalibration(unitMin, unitMax);
    calibration->setUnitLabel(unitLabel);
    calibration->connectArrayToInputSlot(0, extractor->getOutput(0));
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

void add_raw_data_display(Analysis *analysis, s32 eventIndex, s32 moduleIndex, const RawDataDisplay &display)
{
    analysis->addSource(eventIndex, moduleIndex, display.extractor);
    analysis->addOperator(eventIndex, display.rawHistoSink, 0);
    analysis->addOperator(eventIndex, display.calibration, 1);
    analysis->addOperator(eventIndex, display.calibratedHistoSink, 1);
}

void do_beginRun_forward(PipeSourceInterface *pipeSource)
{
    Q_ASSERT(pipeSource);

    qDebug() << __PRETTY_FUNCTION__ << "calling beginRun() on" << pipeSource;
    pipeSource->beginRun();

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

}
