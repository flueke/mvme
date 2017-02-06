#include "analysis.h"
#include "../qt_util.h"

namespace analysis
{

//
// Extractor
//
Extractor::Extractor(QObject *parent)
    : QObject(parent)
{
}

void Extractor::beginRun()
{
    m_currentCompletionCount = 0;

    u32 addressCount = 1 << m_filter.getAddressBits();

    m_output.getParameters().resize(addressCount);

    u32 upperLimit = 1 << m_filter.getDataBits();

    for (int i=0; i<m_output.getParameters().size(); ++i)
    {
        auto &param(m_output.getParameters()[i]);
        param.type = Parameter::Uint;
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
                    param.ival = value;
                    //qDebug() << __PRETTY_FUNCTION__ << "address" << address << "value" << value;
                }
            }
        }
        m_filter.clearCompletion();
    }
}

int Extractor::numberOfOutputs() const
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
// CalibrationOperator
//
CalibrationOperator::CalibrationOperator(QObject *parent)
    : QObject(parent)
{
    m_output.setSource(this);
}

void CalibrationOperator::step()
{
}

int CalibrationOperator::getNumberOfInputs() const
{
    return 1;
}

QString CalibrationOperator::getInputName(int inputIndex) const
{
    return "Input Array";
}

void CalibrationOperator::setInput(int index, Pipe *inputPipe)
{
    m_input = inputPipe;
    m_input->addDestination(this);
}

void CalibrationOperator::removeInput(Pipe *pipe)
{
    if (m_input == pipe)
    {
        m_input->removeDestination(this);
        m_input = nullptr;
    }
}

int CalibrationOperator::getNumberOfOutputs() const
{
    return 1;
}

QString CalibrationOperator::getOutputName(int outputIndex) const
{
    return QSL("Calibrated data array");
}

Pipe *CalibrationOperator::getOutput(int index)
{
    Pipe *result = nullptr;
    if (index == 0)
    {
        result = &m_output;
    }
    return result;
}

}
