#include "analysis.h"
#include <QJsonArray>
#include <QJsonObject>

#define ENABLE_ANALYSIS_DEBUG 0

template<typename T>
QDebug &operator<< (QDebug &dbg, const std::shared_ptr<T> &ptr)
{
    dbg << ptr.get();
    return dbg;
}

namespace analysis
{
//
// Slot
//

// TODO: remove this
void Slot::setInput_(Pipe *newInput)
{
    if (inputPipe)
    {
        inputPipe->removeDestination(this);
    }

    inputPipe = newInput;

    if (inputPipe)
    {
        inputPipe->addDestination(this);
    }
}

void Slot::connectPipe(Pipe *newInput, s32 newParamIndex)
{
    disconnectPipe();
    if (newInput)
    {
        inputPipe = newInput;
        paramIndex = newParamIndex;
        inputPipe->addDestination(this);
    }
}

void Slot::disconnectPipe()
{
    if (inputPipe)
    {
        inputPipe->removeDestination(this);
        paramIndex = Slot::NoParamIndex;
    }
}

//
// OperatorInterface
//
// XXX: does not perform acceptedInputTypes validity test atm!
bool OperatorInterface::connectInputSlot(s32 slotIndex, Pipe *inputPipe, s32 paramIndex)
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
Extractor::Extractor(QObject *parent)
    : SourceInterface(parent)
{
    m_output.setSource(this);
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
        auto param(params[i]);
        param.lowerLimit = 0.0;
        param.upperLimit = upperLimit;
    }
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

        if (m_requiredCompletionCount == 0 || m_requiredCompletionCount == m_currentCompletionCount)
        {
            u64 value   = m_filter.getResultValue();
            s32 address = m_filter.getResultAddress();

            auto &param = m_output.getParameters()[address];
            // only fill if not valid to keep the first value in case of multiple hits
            if (!param.valid)
            {
                param.valid = true;
                param.value = value; // XXX: using the double here! ival is not used anymore
#if ENABLE_ANALYSIS_DEBUG
                qDebug() << this << "setting param valid, addr =" << address << ", value =" << param.value
                    << ", dataWord =" << QString("0x%1").arg(data, 8, 16, QLatin1Char('0'));
#endif
            }
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

#if 0
void BasicOperator::connectInputSlot(s32 slotIndex, Pipe *inputPipe, s32 paramIndex)
{
    if (slotIndex == 0)
    {
        if (m_inputSlot.inputPipe)
        {
            m_inputSlot.inputPipe->removeDestination(&m_inputSlot);
        }

        m_inputSlot.inputPipe = inputPipe;
        m_inputSlot.paramIndex = paramIndex;

        if (inputPipe)
        {
            inputPipe->addDestination(&m_inputSlot);
        }
    }
}
#endif

Slot *BasicOperator::getSlot(s32 slotIndex)
{
    Slot *result = nullptr;
    if (slotIndex == 0)
    {
        result = &m_inputSlot;
    }
    return result;
}

#if 0
void BasicOperator::disconnectSlot(Pipe *sourcePipe)
{
    if (sourcePipe && m_inputSlot.inputPipe == sourcePipe)
    {
        m_inputSlot.inputPipe->removeDestination(&m_inputSlot);
        m_inputSlot.inputPipe = nullptr;
        m_inputSlot.paramIndex = Slot::NoParamIndex;
    }
}
#endif

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
BasicSink::BasicSink()
    : m_inputSlot(this, 0, QSL("Input"))
{
}

BasicSink::~BasicSink()
{
}

s32 BasicSink::getNumberOfSlots() const
{
    return 1;
}

#if 0
void BasicSink::connectInputSlot(s32 slotIndex, Pipe *inputPipe, s32 paramIndex)
{
    if (slotIndex == 0)
    {
        if (m_inputSlot.inputPipe)
        {
            m_inputSlot.inputPipe->removeDestination(&m_inputSlot);
        }

        m_inputSlot.inputPipe = inputPipe;
        m_inputSlot.paramIndex = paramIndex;

        if (inputPipe)
        {
            inputPipe->addDestination(&m_inputSlot);
        }
    }
}
#endif

Slot *BasicSink::getSlot(s32 slotIndex)
{
    Slot *result = nullptr;
    if (slotIndex == 0)
    {
        result = &m_inputSlot;
    }
    return result;
}

#if 0
void BasicSink::disconnectSlot(Pipe *sourcePipe)
{
    if (sourcePipe && m_inputSlot.inputPipe == sourcePipe)
    {
        m_inputSlot.inputPipe->removeDestination(&m_inputSlot);
        m_inputSlot.inputPipe = nullptr;
        m_inputSlot.paramIndex = Slot::NoParamIndex;
    }
}
#endif

//
// Calibration
//
Calibration::Calibration(QObject *parent)
    : BasicOperator(parent)
{
}

void Calibration::beginRun()
{
    if (m_inputSlot.inputPipe)
    {
        auto &out(m_output.getParameters());
        const auto &in(m_inputSlot.inputPipe->getParameters());

        if (m_inputSlot.paramIndex != Slot::NoParamIndex)
        {
            out.resize(1);
        }
        else
        {
            out.resize(in.size());
        }

        out.name = in.name; // TODO: set the new parameter name here
        out.unit = getUnitLabel();
    }
}

void Calibration::step()
{
    auto calibOneParam = [](const Parameter &inParam, Parameter &outParam, const CalibrationParameters &calib)
    {
        outParam.valid = inParam.valid;
        if (inParam.valid)
        {
            outParam.value = inParam.value * calib.factor + calib.offset;
            outParam.lowerLimit = inParam.lowerLimit * calib.factor + calib.offset;
            outParam.upperLimit = inParam.upperLimit * calib.factor + calib.offset;
            outParam.valid = true;
        }
    };


    if (m_inputSlot.inputPipe)
    {
        auto &out(m_output.getParameters());
        const auto &in(m_inputSlot.inputPipe->getParameters());
        const s32 inSize = in.size();

        if (m_inputSlot.paramIndex >= 0)
        {
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

            for (s32 address = 0; address < size; ++address)
            {
                auto &outParam(out[address]);
                const auto &inParam(in[address]);

                calibOneParam(inParam, outParam, getCalibration(address));
            }
        }
    }
}

void Calibration::setCalibration(s32 address, const CalibrationParameters &params)
{
    m_calibrations.resize(std::max(m_calibrations.size(), address+1));
    m_calibrations[address] = params;
}

CalibrationParameters Calibration::getCalibration(s32 address) const
{
    CalibrationParameters result = m_globalCalibration;

    if (address < m_calibrations.size() && m_calibrations[address].isValid())
    {
        result = m_calibrations[address];
    }

    return result;
}

void Calibration::read(const QJsonObject &json)
{
    m_unit = json["unitLabel"].toString();
    m_globalCalibration.factor = json["globalFactor"].toDouble();
    m_globalCalibration.offset = json["globalOffset"].toDouble();

    m_calibrations.clear();
    QJsonArray calibArray = json["calibrations"].toArray();

    for (auto it=calibArray.begin();
         it != calibArray.end();
         ++it)
    {
        auto paramJson = it->toObject();
        CalibrationParameters param;
        if (paramJson.contains("factor") && paramJson.contains("offset"))
        {
            param.factor = paramJson["factor"].toDouble();
            param.offset = paramJson["offset"].toDouble();
        }
        m_calibrations.push_back(param);
    }
}

void Calibration::write(QJsonObject &json) const
{
    json["unitLabel"] = m_unit;
    json["globalFactor"] = m_globalCalibration.factor;
    json["globalOffset"] = m_globalCalibration.offset;

    QJsonArray calibArray;

    for (auto &param: m_calibrations)
    {
        QJsonObject paramJson;
        if (param.isValid())
        {
            paramJson["factor"] = param.factor;
            paramJson["offset"] = param.offset;
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
    if (m_inputSlot.inputPipe)
    {
        auto &out(m_output.getParameters());
        const auto &in(m_inputSlot.inputPipe->getParameters());

        out.resize(1);
        out.name = in.name;
        out.unit = in.unit;
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
// Histo1DSink
//
void Histo1DSink::step()
{
    if (m_inputSlot.inputPipe && !histos.isEmpty())
    {
        s32 paramIndex = m_inputSlot.paramIndex;

        if (paramIndex >= 0)
        {
            // Input is a single value
            const Parameter *param = m_inputSlot.inputPipe->getParameter(paramIndex);
            if (param && param->valid)
            {
                histos[0]->fill(param->value);
            }
        }
        else
        {
            // Input is an array
            const auto &in(m_inputSlot.inputPipe->getParameters());
            const s32 inSize = in.size();
            const s32 histoSize = histos.size();

            for (s32 paramIndex = 0; paramIndex < std::min(inSize, histoSize); ++paramIndex)
            {
                const Parameter *param = m_inputSlot.inputPipe->getParameter(paramIndex);
                if (param && param->valid)
                {
                    histos[paramIndex]->fill(param->value);
                }
            }
        }
    }
}

void Histo1DSink::read(const QJsonObject &json)
{
    QJsonArray histosJson = json["histos"].toArray();

    for (auto it=histosJson.begin();
         it != histosJson.end();
         ++it)
    {
        auto objectJson = it->toObject();

        u32 nBins = static_cast<u32>(objectJson["nBins"].toInt());
        double xMin = objectJson["xMin"].toDouble();
        double xMax = objectJson["xMax"].toDouble();
        histos.push_back(std::make_shared<Histo1D>(nBins, xMin, xMax));
    }
}

void Histo1DSink::write(QJsonObject &json) const
{
    QJsonArray histosJson;

    for (const auto &histo: histos)
    {
        QJsonObject objectJson;
        objectJson["nBins"] = static_cast<qint64>(histo->getNumberOfBins());
        objectJson["xMin"] = histo->getXMin();
        objectJson["xMax"] = histo->getXMax();
        histosJson.append(objectJson);
    }

    json["histos"] = histosJson;
}

//
// Histo2DSink
//
Histo2DSink::Histo2DSink()
    : m_inputX(this, 0, QSL("X-Axis"), InputType::Value)
    , m_inputY(this, 1, QSL("Y-Axis"), InputType::Value)
{
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

#if 0
void Histo2DSink::connectInputSlot(s32 slotIndex, Pipe *inputPipe, s32 paramIndex)
{
    Slot *destSlot = getSlot(slotIndex);

    if (!destSlot || !inputPipe || paramIndex < 0)
        return;

    destSlot->setInput(inputPipe);
    destSlot->paramIndex = paramIndex;
}
#endif

#if 0
void Histo2DSink::disconnectSlot(Pipe *sourcePipe)
{
    if (sourcePipe && m_inputSlot.inputPipe == sourcePipe)
    {
        m_inputSlot.inputPipe->removeDestination(&m_inputSlot);
        m_inputSlot.inputPipe = nullptr;
        m_inputSlot.paramIndex = -1;
    }
}
#endif

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
    u32 xBins = static_cast<u32>(json["xBins"].toInt());
    double xMin = json["xMin"].toDouble();
    double xMax = json["xMax"].toDouble();

    u32 yBins = static_cast<u32>(json["yBins"].toInt());
    double yMin = json["yMin"].toDouble();
    double yMax = json["yMax"].toDouble();

    m_histo = std::make_shared<Histo2D>(xBins, xMin, xMax,
                                      yBins, yMin, yMax);
}

void Histo2DSink::write(QJsonObject &json) const
{
    if (m_histo)
    {
        json["xBins"] = static_cast<qint64>(m_histo->getAxis(Qt::XAxis).getBins());
        json["xMin"]  = m_histo->getAxis(Qt::XAxis).getMin();
        json["xMax"]  = m_histo->getAxis(Qt::XAxis).getMax();

        json["yBins"] = static_cast<qint64>(m_histo->getAxis(Qt::YAxis).getBins());
        json["yMin"]  = m_histo->getAxis(Qt::YAxis).getMin();
        json["yMax"]  = m_histo->getAxis(Qt::YAxis).getMax();
    }
}

//
// Analysis
//
Analysis::Analysis()
{
    m_registry.registerSource<Extractor>();

    m_registry.registerOperator<Calibration>();
    m_registry.registerOperator<IndexSelector>();


    m_registry.registerSink<Histo1DSink>();
    m_registry.registerSink<Histo2DSink>();

    qDebug() << m_registry.getSourceNames();
    qDebug() << m_registry.getOperatorNames();
    qDebug() << m_registry.getSinkNames();
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

void Analysis::processDataWord(s32 eventIndex, s32 moduleIndex, u32 data, s32 wordIndex)
{
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

void Analysis::removeSource(const SourcePtr &source) // TODO: test this
{
    s32 entryIndex = -1;
    for (s32 i = 0; i < m_sources.size(); ++i)
    {
        if (m_sources[i].source == source)
        {
            entryIndex = i;
            break;
        }
    }

    if (entryIndex >= 0)
    {
        m_sources.remove(entryIndex);

        for (s32 outputIndex = 0; outputIndex < source->getNumberOfOutputs(); ++outputIndex)
        {
            Pipe *outPipe = source->getOutput(outputIndex);
            for (Slot *destSlot: outPipe->getDestinations())
            {
                destSlot->inputPipe->removeDestination(destSlot);
            }
        }
    }
}

void Analysis::removeOperator(const OperatorPtr &op) // TODO: test this
{
    s32 entryIndex = -1;
    for (s32 i = 0; i < m_operators.size(); ++i)
    {
        if (m_operators[i].op == op)
        {
            entryIndex = i;
            break;
        }
    }

    if (entryIndex >= 0)
    {
        m_operators.remove(entryIndex);

        for (s32 outputIndex = 0; outputIndex < op->getNumberOfOutputs(); ++outputIndex)
        {
            Pipe *outPipe = op->getOutput(outputIndex);
            for (Slot *destSlot: outPipe->getDestinations())
            {
                destSlot->inputPipe->removeDestination(destSlot);
            }
        }
    }
}


void Analysis::clear()
{
    m_sources.clear();
    m_operators.clear();
}

#if 0
        struct Connection
        {
            PipeSourceInterface *srcObject;
            s32 srcIndex; // the output index of the source object

            OperatorInterface *dstObject;
            s32 dstIndex; // the input index of the dest object
        };
#endif

template<typename T>
QString getClassName(T *obj)
{
    return obj->metaObject()->className();
}

void Analysis::read(const QJsonObject &json)
{
    clear();

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
            u32 acceptedInputTypes = static_cast<u32>(objectJson["dstAcceptedInputTypes"].toInt());
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
                    dstSlot->acceptedInputTypes = acceptedInputTypes;
                    dstSlot->paramIndex = paramIndex;

                    Pipe *thePipe = srcRawPtr->getOutput(srcIndex);
                    Q_ASSERT(thePipe);
                    Q_ASSERT(thePipe->source == srcRawPtr);

                    dstRawPtr->connectInputSlot(dstIndex, thePipe, paramIndex);

                    Q_ASSERT(thePipe->destinations.contains(dstSlot));
                }


#if 0
                Slot *dstSlot = dstObject->getSlot(dstIndex);
                Q_ASSERT(dstSlot); // FIXME: testing
                if (dstSlot)
                {
                    dstSlot->acceptedInputTypes = acceptedInputTypes;
                    dstSlot->paramIndex = paramIndex;
                    dstObject->connectInputSlot(dstIndex, srcObject->getOutput(srcIndex), paramIndex);
                }
#endif
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
}

void Analysis::write(QJsonObject &json) const
{
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
                        conJson["dstAcceptedInputTypes"] = static_cast<qint64>(dstSlot->acceptedInputTypes);
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

        /* TODO/FIXME:
         * - histo axis titles are still missing
         * - easier to use MultiWordDataFilter constructor
         * - IndexSelector.m_index is signed because of Qts containers using a
         *   signed type for their sizes. What happens if the index is actually
         *   negative?
         */
RawDataDisplay make_raw_data_display(const MultiWordDataFilter &extractionFilter, double unitMin, double unitMax,
                                     const QString &filterName, const QString &xAxisTitle, const QString &unitLabel)
{
    RawDataDisplay result;

    auto extractor = std::make_shared<Extractor>();
    extractor->setFilter(extractionFilter);
    extractor->setObjectName(filterName);

    result.extractor = extractor;

    double srcMin  = 0.0;
    double srcMax  = (1 << extractionFilter.getDataBits());

    // factor in U/S, offset in U
    // TODO: should offset be in S?
    double factor = std::abs(unitMax - unitMin) / (srcMax - srcMin);
    double offset = unitMin - srcMin * factor; // FIXME: correct in all cases?

    qDebug() << ">>>>> factor =" << factor << ", offset =" << offset;

    auto calibration = std::make_shared<Calibration>();
    calibration->setGlobalCalibration(factor, offset);
    calibration->setObjectName(filterName);
    calibration->connectArrayToInputSlot(0, extractor->getOutput(0));

    result.calibration = calibration;

    auto rawHistoSink = std::make_shared<Histo1DSink>();
    rawHistoSink->setObjectName(QString("Raw %1").arg(filterName));
    result.rawHistoSink = rawHistoSink;

    auto calHistoSink = std::make_shared<Histo1DSink>();
    calHistoSink->setObjectName(QString("Cal %1").arg(filterName));
    result.calibratedHistoSink = calHistoSink;

    u32 addressCount = (1 << extractionFilter.getAddressBits());

    for (u32 address = 0; address < addressCount; ++address)
    {
        // create a histo for the raw uncalibrated data
        auto histo = std::make_shared<Histo1D>(srcMax, 0.0, srcMax);
        histo->setObjectName(QString("%1[%2]").arg(rawHistoSink->objectName()).arg(address));
        rawHistoSink->histos.push_back(histo);
        // TODO: rawHistoSink->histo->setXAxisTitle(xAxisTitle);

        // create a histo for the calibrated data
        histo = std::make_shared<Histo1D>(srcMax, unitMin, unitMax);
        histo->setObjectName(QString("%1[%2]").arg(calHistoSink->objectName()).arg(address));
        calHistoSink->histos.push_back(histo);
    }

    rawHistoSink->connectArrayToInputSlot(0, extractor->getOutput(0));
    calHistoSink->connectArrayToInputSlot(0, calibration->getOutput(0));

    return result;
}

void add_raw_data_display(Analysis *analysis, s32 eventIndex, s32 moduleIndex, const RawDataDisplay &display)
{
    analysis->addSource(eventIndex, moduleIndex, display.extractor);
    analysis->addOperator(eventIndex, display.rawHistoSink, 0);
    analysis->addOperator(eventIndex, display.calibration, 1);
    analysis->addOperator(eventIndex, display.calibratedHistoSink, 1);
}

}
