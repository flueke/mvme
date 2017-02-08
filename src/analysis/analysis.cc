#include "analysis.h"
#include "../qt_util.h"

#define ENABLE_ANALYSIS_DEBUG 0

namespace analysis
{

//
// OperatorInterface
//
s32 OperatorInterface::getMaximumInputRank()
{
    s32 result = 0;

    for (s32 inputIndex = 0;
         inputIndex < getNumberOfInputs();
         ++inputIndex)
    {
        if (Pipe *input = getInput(inputIndex))
        {
            result = std::max(result, input->getRank());
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
    u32 upperLimit = 1 << m_filter.getDataBits();

    auto &params(m_output.getParameters());
    params.resize(addressCount);

    for (int i=0; i<params.size(); ++i)
    {
        auto &param(params[i]);
        param.type = Parameter::Double; // XXX: using the double here! ival is not used anymore

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

            if (address < m_output.getParameters().size())
            {
                auto &param = m_output.getParameters()[address];
                // only fill if not valid to keep the first value in case of multiple hits
                if (!param.valid)
                {
                    param.valid = true;
                    param.dval = value; // XXX: using the double here! ival is not used anymore
                    qDebug() << this << "setting param valid, addr =" << address << ", value =" << param.dval
                        << ", dataWord =" << QString("0x%1").arg(data, 8, 16, QLatin1Char('0'));
                }
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

int Extractor::getNumberOfOutputs() const
{
    return 1;
}

QString Extractor::outputName(int outputIndex) const
{
    return QSL("Extracted data array");
}

Pipe *Extractor::getOutput(int index)
{
    Pipe *result = nullptr;

    if (index == 0)
    {
        result = &m_output;
    }

    return result;
}

//
// BasicOperator
//
BasicOperator::BasicOperator(QObject *parent)
    : OperatorInterface(parent)
{
    m_output.setSource(this);
}

BasicOperator::~BasicOperator()
{
}

int BasicOperator::getNumberOfInputs() const
{
    return 1;
}

QString BasicOperator::getInputName(int inputIndex) const
{
    if (inputIndex == 0)
    {
        return QSL("Input");
    }
    
    return QString();
}

void BasicOperator::setInput(int index, Pipe *inputPipe)
{
    if (index == 0)
    {
        if (m_input)
        {
            m_input->removeDestination(this);
        }

        m_input = inputPipe;

        if (m_input)
        {
            m_input->addDestination(this);
        }
    }
}

Pipe *BasicOperator::getInput(int index) const
{
    Pipe *result = nullptr;
    if (index == 0)
    {
        result = m_input;
    }
    return result;
}

void BasicOperator::removeInput(Pipe *pipe)
{
    if (m_input && m_input == pipe)
    {
        m_input->removeDestination(this);
        m_input = nullptr;
    }
}

int BasicOperator::getNumberOfOutputs() const
{
    return 1;
}

QString BasicOperator::getOutputName(int outputIndex) const
{
    if (outputIndex == 1)
    {
        return QSL("Output");
    }
    return QString();

}

Pipe *BasicOperator::getOutput(int index)
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
BasicSink::~BasicSink()
{
}

int BasicSink::getNumberOfInputs() const
{
    return 1;
}

QString BasicSink::getInputName(int inputIndex) const
{
    if (inputIndex == 0)
    {
        return QSL("Input");
    }
    
    return QString();
}

void BasicSink::setInput(int index, Pipe *inputPipe)
{
    if (index == 0)
    {
        if (m_input)
        {
            m_input->removeDestination(this);
        }

        m_input = inputPipe;

        if (m_input)
        {
            m_input->addDestination(this);
        }
    }
}

Pipe *BasicSink::getInput(int index) const
{
    Pipe *result = nullptr;
    if (index == 0)
    {
        result = m_input;
    }
    return result;
}

void BasicSink::removeInput(Pipe *pipe)
{
    if (m_input && m_input == pipe)
    {
        m_input->removeDestination(this);
        m_input = nullptr;
    }
}


int BasicSink::getNumberOfOutputs() const
{
    return 0;
}

QString BasicSink::getOutputName(int outputIndex) const
{
    return QString();
}

Pipe *BasicSink::getOutput(int index)
{
    Pipe *result = nullptr;
    return result;
}

//
// CalibrationOperator
//
CalibrationOperator::CalibrationOperator(QObject *parent)
    : BasicOperator(parent)
{
}

void CalibrationOperator::step()
{
    if (m_input)
    {
        auto &out(m_output.getParameters());
        const auto &in(m_input->getParameters());

        out.resize(in.size());
        out.invalidateAll();

        out.name = in.name;
        out.unit = getUnitLabel();


        const s32 size = in.size();
        for (s32 address = 0;
             address < size;
             ++address)
        {
            auto &outParam(out[address]);
            const auto &inParam(in[address]);

            if (inParam.valid)
            {
                CalibrationParameters calib(getCalibration(address));

                outParam.dval = inParam.dval * calib.factor + calib.offset;
                outParam.type = Parameter::Double;
                outParam.valid = true;
            }
        }
    }
}

void CalibrationOperator::setCalibration(s32 address, const CalibrationParameters &params)
{
    m_calibrations.resize(std::max(m_calibrations.size(), address+1));
    m_calibrations[address] = params;
}

CalibrationParameters CalibrationOperator::getCalibration(s32 address) const
{
    CalibrationParameters result = m_globalCalibration;

    if (address < m_calibrations.size() && m_calibrations[address].isValid())
    {
        result = m_calibrations[address];
    }

    return result;
}

//
// IndexSelector
//
void IndexSelector::step()
{
    if (m_input)
    {
        auto &out(m_output.getParameters());
        const auto &in(m_input->getParameters());

        out.resize(1);
        out.invalidateAll();

        if (m_index < in.size())
        {
            const auto &inParam(in[m_index]);

            if (inParam.valid)
            {
                out.name = in.name;
                out.unit = in.unit;
                out[0] = inParam;
            }
        }
    }
}

//
// Histo1DSink
//
void Histo1DSink::step()
{
    if (m_input && histo)
    {
        const auto &param(m_input->first());

        if (param.valid)
        {
            histo->fill(param.dval);

#if ENABLE_ANALYSIS_DEBUG
            qDebug() << this << "fill" << histo.get() << param.dval;
            ++fillsSinceLastDebug;
            if (fillsSinceLastDebug > 10000)
            {
                histo->debugDump(false);
                fillsSinceLastDebug = 0;
            }
#endif
        }
    }
}

//
// Analysis
//
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

void Analysis::beginEvent(int eventIndex)
{
    for (auto &sourceEntry: m_sources)
    {
        if (sourceEntry.eventIndex == eventIndex)
        {
            sourceEntry.source->beginEvent();
        }
    }
}

void Analysis::processDataWord(int eventIndex, int moduleIndex, u32 data, s32 wordIndex)
{
    for (auto &sourceEntry: m_sources)
    {
        if (sourceEntry.eventIndex == eventIndex && sourceEntry.moduleIndex == moduleIndex)
        {
            sourceEntry.source->processDataWord(data, wordIndex);
        }
    }
}

void Analysis::endEvent(int eventIndex)
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
         inputIndex < op->getNumberOfInputs();
         ++inputIndex)
    {
        Pipe *input = op->getInput(inputIndex);

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

    s32 maxInputRank = op->getMaximumInputRank();

    for (s32 outputIndex = 0;
         outputIndex < op->getNumberOfOutputs();
         ++outputIndex)
    {
        op->getOutput(outputIndex)->setRank(maxInputRank + 1);
        updated.insert(op);
    }
}

}
