#ifndef __ANALYSIS_H__
#define __ANALYSIS_H__

#include "../typedefs.h"
#include "data_filter.h"
#include "histograms.h"

#include <memory>

// TODO: rank calculation
//   The output rank of an operator depends on the operators inputs and thus is operator specific.
//   The operator needs to implement a getOutputRank() method that yields 1
//   plus the maximum rank of all the inputs.
//
//   Histo1DSink and Histo2DSink should probably not do the channel selection.
//   Instead they except a single value (a pair of values) for hist2d and fill
//   the histo if both values are valid.
//   To implement this i need an IndexSelector Operator

namespace analysis
{

static constexpr double make_quiet_nan()
{
    return std::numeric_limits<double>::quiet_NaN();
}

#if 0
struct Parameter
{
    bool valid = false;
    double value = 0.0;
};
#else
struct Parameter
{
    enum Type { Double, Uint, Bool };
    Type type = Double;
    bool valid = false;

    double lowerLimit = 0.0; // inclusive
    double upperLimit = 0.0; // exclusive

    union
    {
        double dval = 0.0;
        u64 ival;
        bool bval;
    };
};

// dest = a - b iff a and b are valid and a and b are of the same type
// FIXME: not a good idea for Uint values as they're _unsigned_! Could convert
// the result to be of type Double... Same for Bool values as `false - true = -1'.
void subtract_params(const Parameter &a, const Parameter &b, Parameter &dest)
{
    if (a.valid && b.valid && a.type == b.type)
    {
        switch (a.type)
        {
            case Parameter::Double:
                {
                    dest.dval = a.dval - b.dval;
                } break;
            case Parameter::Uint:
                {
                    dest.dval = static_cast<double>(a.ival) - static_cast<double>(b.ival);
                } break;
            case Parameter::Bool:
                {
                    dest.dval = static_cast<double>(a.bval) - static_cast<double>(b.bval);
                } break;
        }
        dest.type = Parameter::Double;
        dest.valid = true;
        // FIXME: limits
    }
    else
    {
        dest.valid = false;
    }
}
#endif

typedef QVector<Parameter> ParameterVector;

void clear_parameters(ParameterVector &params)
{
    for (auto &param: params)
    {
        param.valid = false;
    }
}

struct Pipe
{
    ParameterVector parameters;
    int rank = 0;

    const Parameter &first() const
    {
        if (!parameters.isEmpty())
        {
            return parameters[0];
        }

        return dummy;
    }

    const Parameter dummy = {};
};

//
// Sources
//

struct Source
{
    virtual void beginRun() {}
    virtual void beginEvent() {}
    virtual void processDataWord(u32 data, s32 wordIndex) = 0;
    virtual ~Source() {}

    Pipe output;
};

typedef std::shared_ptr<Source> SourcePtr;

/* A Source using a MultiWordDataFilter for data extraction. Additionally
 * requiredCompletionCount can be set to only produce output for the nth
 * match (in the current event). */
struct Extractor: public Source
{
    // configuration
    MultiWordDataFilter filter;
    u32 requiredCompletionCount = 0;

    // state
    u32 currentCompletionCount = 0;

    // result
    Pipe output;

    virtual void beginRun() override
    {
        currentCompletionCount = 0;

        u32 addressCount = 1 << filter.getAddressBits();

        output.parameters.resize(addressCount);

        u32 upperLimit = 1 << filter.getDataBits();

        for (int i=0; i<output.parameters.size(); ++i)
        {
            output.parameters[i].type = Parameter::Uint;
            output.parameters[i].lowerLimit = 0.0;
            output.parameters[i].upperLimit = upperLimit;
        }
    }

    virtual void beginEvent() override
    {
        clear_parameters(output.parameters);
    }

    virtual void processDataWord(u32 data, s32 wordIndex) override
    {
        filter.handleDataWord(data, wordIndex);
        if (filter.isComplete())
        {
            ++currentCompletionCount;

            if (requiredCompletionCount == 0 || requiredCompletionCount == currentCompletionCount)
            {
                u64 value   = filter.getResultValue();
                s32 address = filter.getResultAddress();

                if (address < output.parameters.size())
                {
                    auto &param = output.parameters[address];
                    // only fill if not valid to keep the first value in case of multiple hits
                    if (!param.valid)
                    {
                        param.valid = true;
                        param.ival = value;
                        //qDebug() << __PRETTY_FUNCTION__ << "address" << address << "value" << value;
                    }
                }
            }
            filter.clearCompletion();
        }
    }
};

typedef std::shared_ptr<Extractor> ExtractorPtr;

void extractor_begin_run(Extractor &ex)
{
}

void extractor_begin_event(Extractor &ex)
{
}

void extractor_process_data(Extractor &ex, u32 data, u32 wordIndex)
{
}

//
// Operators
//

struct Operator
{
    virtual void step() = 0;
    virtual ~Operator() {}

    Pipe output;
};

typedef std::shared_ptr<Operator> OperatorPtr;

struct CalibrationParams
{
    CalibrationParams()
    {}

    CalibrationParams(double factor, double offset)
        : factor(factor)
        , offset(offset)
    {}

    bool isValid() const
    {
        return !(std::isnan(factor) || std::isnan(offset));
    }

    double factor = make_quiet_nan();
    double offset = make_quiet_nan();
};

struct CalibrationOperator: public Operator
{
    CalibrationParams globalCalibration = { 1.0, 0.0 };

    // optional calibrations for per incoming address
    QVector<CalibrationParams> calibrations;

    Pipe *input = nullptr;

    virtual void step() override
    {
        if (input)
        {
            // TODO: for performance the resize should happen at init time and usually not change afterwards
            output.parameters.resize(input->parameters.size());
            clear_parameters(output.parameters);

            for (int address = 0;
                 address < input->parameters.size();
                 ++address)
            {
                const auto &parameter(input->parameters[address]);

                if (parameter.valid)
                {
                    double value;
                    bool validValue = false;
                    if (parameter.type == Parameter::Double)
                    {
                        value = parameter.dval;
                        validValue = true;
                    }
                    else if (parameter.type == Parameter::Uint)
                    {
                        value = parameter.ival;
                        validValue = true;
                    }

                    if (validValue)
                    {
                        // get the CalibrationParams
                        auto calib = globalCalibration;
                        if (address < calibrations.size() && calibrations[address].isValid())
                        {
                            calib = calibrations[address];
                        }

                        value = value * calib.factor + calib.offset;

                        output.parameters[address].type = Parameter::Double;
                        output.parameters[address].dval = value;
                        output.parameters[address].valid = true;
                        //TODO output.current[address].lowerLimit =
                        //TODO output.current[address].upperLimit =
                    }
                }
            }
        }
    }
};

#if 0
struct HypotheticalSortingMachine: public Operator
{
    /* Takes a variable amount of inputs.
     * The rank of this operators output is the highest input rank + 1.
     * This functionality would be used to take data from multiple modules and
     * assign virtual channel numbers to it. For example if there's 4 MSCF16s
     * as input, input[0] provides amplitudes 0-15, input[1] amplitudes 16-31
     * and so on up to amplitude 63 for input[3].
     *
     * Output size is the sum of the input sizes.
     *
     * FIXME: Assumption for now: all inputs have the same size! The user would
     * normally want this to be true but it may not be the case. If it's not
     * the case the output address would be the input address + the sum of the
     * size of the previous inputs.
     */


    QVector<Pipe *> inputs;

    virtual void step() override
    {
        // calc output size. FIXME: this should be done in a preparation step
        int output_size = 0;
        for (auto transport: inputs)
        {
            output_size += transport->parameters.size();
        }
        output.parameters.resize(output_size);

        for (int input_index = 0;
             input_index < inputs.size();
             ++input_index)
        {
            Pipe *transport = inputs[input_index];

            for (int address = 0;
                 address < transport->parameters.size();
                 ++address)
            {
                const auto &param(transport->parameters[address]);
                if (param.valid)
                {
                    int output_address = address + (input_index * 16);

                    output.parameters[output_address] = param; // copy the param struct
                }
            }
        }
    }
};
#endif

struct IndexSelector: public Operator
{
    Pipe *input = nullptr;
    s32 index;

    IndexSelector(s32 index)
        : index(index)
    { }

    virtual void step() override
    {
        if (input)
        {
            output.parameters.resize(1);
            clear_parameters(output.parameters);

            if (index < input->parameters.size())
            {
                output.parameters[0] = input->parameters[index];
            }
        }
    }
};

// Calculates A - B;
struct Difference: public Operator
{
    Pipe *inputA;
    Pipe *inputB;

    virtual void step() override
    {
        if (inputA && inputB)
        {
            s32 minSize = std::min(inputA->parameters.size(), inputB->parameters.size());
            output.parameters.resize(minSize);
            clear_parameters(output.parameters);

            for (s32 paramIndex = 0;
                 paramIndex < minSize;
                 ++paramIndex)
            {
                const auto &paramA(inputA->parameters[paramIndex]);
                const auto &paramB(inputB->parameters[paramIndex]);

                if (paramA.valid && paramB.valid)
                {
                    output.parameters[paramIndex].valid = true;
                    output.parameters[paramIndex].type  = Parameter::Double;
                    output.parameters[paramIndex].dval = paramA.dval - paramB.dval;
                }
            }
        }
    }
};

struct PreviousValue: public Operator
{
    Pipe *input;

    virtual void step() override
    {
        if (input)
        {
            output.parameters = previousInput;
            previousInput = input->parameters;
        }
    }

    ParameterVector previousInput;
};

struct RetainValid: public Operator
{
    Pipe *input;

    virtual void step() override
    {
        if (input)
        {
            if (output.parameters.size() < input->parameters.size())
            {
                output.parameters.resize(input->parameters.size());
            }

            s32 indexLimit = std::min(output.parameters.size(), input->parameters.size());

            for (s32 i = 0; i < indexLimit; ++i)
            {
                const auto &inputParam(input->parameters[i]);
                if (inputParam.valid)
                {
                    auto &outputParam(output.parameters[i]);
                    outputParam = inputParam; // copy valid value
                }
            }
        }
    }
};

// Output is a boolean flag
struct Histo2DRectangleCut: public Operator
{
    Pipe *inputX;
    Pipe *inputY;
    s32 addressX;
    s32 addressY;

    double minX, maxX, minY, maxY;

    virtual void step() override
    {
        if (inputY && inputX)
        {
            // TODO: implement
        }
    }
};

//
// Sinks
//
struct Sink
{
    virtual void step() = 0;
    virtual ~Sink() {};
};

typedef std::shared_ptr<Sink> SinkPtr;

// Accepts a single value as input
struct Histo1DSink: public Sink
{
    std::shared_ptr<Histo1D> histo;

    Pipe *input = nullptr;

    u32 fillsSinceLastDebug = 0;

    virtual void step() override
    {
        //qDebug() << "begin" << __PRETTY_FUNCTION__;
        if (input && histo)
        {
            const auto &parameter(input->first());

            if (parameter.valid)
            {
                //qDebug() << __PRETTY_FUNCTION__ << "histo fill" << "address" << address << "value" << parameter.dval;
                histo->fill(parameter.dval);
                ++fillsSinceLastDebug;

                if (fillsSinceLastDebug > 1000)
                {
                    histo->debugDump();
                    fillsSinceLastDebug = 0;
                }
            }
        }
        //qDebug() << "end" << __PRETTY_FUNCTION__;
    }
};


struct Histo2DSink: public Sink
{
    std::shared_ptr<Histo2D> histo;

    Pipe *inputX;
    Pipe *inputY;

    virtual void step() override
    {
        if (inputX && inputY)
        {
            const auto &paramX(inputX->first());
            const auto &paramY(inputX->first());

            if (paramX.valid && paramY.valid)
            {
                qDebug() << __PRETTY_FUNCTION__ << "fill" << histo.get() << paramX.dval << paramY.dval;
                histo->fill(paramX.dval, paramY.dval);
            }
        }
    }
};


class Analysis
{
    public:
        struct SourceEntry
        {
            int eventIndex;
            int moduleIndex;
            SourcePtr source;
        };

        QVector<SourceEntry> m_sources;
        QVector<OperatorPtr> m_operators;
        QVector<SinkPtr> m_sinks;

        void beginRun()
        {
            qSort(m_operators.begin(), m_operators.end(), [] (const OperatorPtr &o1, const OperatorPtr &o2) {
                return o1->output.rank < o2->output.rank;
            });


            for (auto &sourceEntry: m_sources)
            {
                sourceEntry.source->beginRun();
            }
        }

        void beginEvent(int eventIndex)
        {
            for (auto &sourceEntry: m_sources)
            {
                if (sourceEntry.eventIndex == eventIndex)
                {
                    sourceEntry.source->beginEvent();
                }
            }
        }

        void processDataWord(int eventIndex, int moduleIndex, u32 data, s32 wordIndex)
        {
            for (auto &sourceEntry: m_sources)
            {
                if (sourceEntry.eventIndex == eventIndex && sourceEntry.moduleIndex == moduleIndex)
                {
                    sourceEntry.source->processDataWord(data, wordIndex);
                }
            }
        }

        void endEvent(int eventIndex)
        {
            /* In beginRun() operators are sorted by rank. This way step()'ing
             * operators can be done by just traversing the array. */

            // TODO: only operators for the current eventIndex should be stepped!
            // FIXME: does this mean cross-event operators are not possible?
            // They could be as the output of an operator that was generated in
            // another event can stay "alive". So a cross-event operator would
            // take the last known output of an operator from another event and
            // the output of an operator from the current event and just work
            // as usual. When to step operators that come afterwards?
            for (auto &op: m_operators)
            {
                op->step();
            }

            for (auto &sink: m_sinks)
            {
                sink->step();
            }
        }
};

}

#endif /* __ANALYSIS_H__ */
