#ifndef __ANALYSIS_H__
#define __ANALYSIS_H__

#include "../typedefs.h"
#include "data_filter.h"
#include "histograms.h"

#include <memory>

// TODO: is occurrence used? don't requiredMatchCount and currentMatchCount handle this?
// TODO: rank calculation
//   The output rank of an operator depends on the operators inputs and thus is operator specific.
//   The operator needs to implement a getOutputRank() method that yields 1
//   plus the maximum rank of all the inputs.
//
//   Histo1DSink and Histo2DSink should probably not do the channel selection.
//   Instead they except a single value (a pair of values) for hist2d and fill
//   the histo if both values are valid.

namespace analysis
{

static constexpr double make_quiet_nan()
{
    return std::numeric_limits<double>::quiet_NaN();
}

struct Parameter
{
    enum Type { Double, Uint, Bool };
    Type type = Double;
    bool valid = false;
    u32 occurrence = 0;

    double lowerLimit = 0.0; // inclusive
    double upperLimit = 0.0; // exclusive

    union
    {
        double dval;
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

typedef QVector<Parameter> ParameterVector;

void clear_parameters(ParameterVector &params)
{
    for (auto &param: params)
    {
        param.valid = false;
        param.occurrence = 0;
    }
}

struct Transport
{
    ParameterVector current;
    ParameterVector last;
    int rank = 0;
};

struct Extractor
{
    // configuration
    MultiWordDataFilter filter;
    u32 requiredMatchCount = 0;

    // state
    u32 currentMatchCount = 0;

    // result
    Transport output;
};

typedef std::shared_ptr<Extractor> ExtractorPtr;

void extractor_begin_run(Extractor &ex)
{
    ex.currentMatchCount = 0;

    u32 addressCount = 1 << ex.filter.getAddressBits();

    ex.output.current.resize(addressCount);
    ex.output.last.resize(addressCount);

    u32 upperLimit = 1 << ex.filter.getDataBits();

    for (int i=0; i<ex.output.current.size(); ++i)
    {
        ex.output.current[i].type = Parameter::Uint;
        ex.output.current[i].lowerLimit = 0.0;
        ex.output.current[i].upperLimit = upperLimit;
        ex.output.current[i].occurrence = 0;

        ex.output.last[i].type = Parameter::Uint;
        ex.output.last[i].lowerLimit = 0.0;
        ex.output.last[i].upperLimit = upperLimit;
        ex.output.last[i].occurrence = 0;
    }
}

void extractor_begin_event(Extractor &ex)
{
    ex.output.current.swap(ex.output.last);
    clear_parameters(ex.output.current);
}

void extractor_process_data(Extractor &ex, u32 data, u32 wordIndex)
{
    ex.filter.handleDataWord(data, wordIndex);
    if (ex.filter.isComplete())
    {
        ++ex.currentMatchCount;

        if (ex.requiredMatchCount == 0 || ex.requiredMatchCount == ex.currentMatchCount)
        {
            u64 value   = ex.filter.getResultValue();
            s32 address = ex.filter.getResultAddress();

            if (address < ex.output.current.size())
            {
                auto &param = ex.output.current[address];
                // only fill if not valid to keep the first value in case of multiple hits
                if (!param.valid)
                {
                    param.valid = true;
                    param.occurrence++;
                    param.ival = value;
                    qDebug() << __PRETTY_FUNCTION__ << "address" << address << "value" << value;
                }
            }
        }
        ex.filter.clearCompletion();
    }
}

struct Operator
{
    virtual void step() = 0;
    virtual ~Operator() {}

    Transport output;
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

    // TODO: for some operators there might need to be a way to select the transport "channel" (current or last).
    Transport *input = nullptr;

    // TODO: is a simple step() enough? for the extractor it's beginEvent() followed by calls to processDataWord().
    // Operators do not really care about events, just about the input being
    // available or not. If input is available they do produce output otherwise
    // they don't.
    // XXX: Important: when to swap Transport.current and .last? Ensure that
    // operators do not consume the same data multiple times if it's not
    // desired!
    virtual void step() override
    {
        if (input)
        {
            output.current.swap(output.last);
            // TODO: for performance the resize should happen at init time and usually not change afterwards
            output.current.resize(input->current.size());
            clear_parameters(output.current);

            for (int address = 0;
                 address < input->current.size();
                 ++address)
            {
                const auto &parameter(input->current[address]);

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

                        output.current[address].type = Parameter::Double;
                        output.current[address].dval = value;
                        output.current[address].valid = true;
                        //TODO output.current[address].lowerLimit =
                        //TODO output.current[address].upperLimit =
                        //FIXME output.current[address].occurrence
                    }
                }
            }
        }
    }
};

struct HypotheticalSortingMachine: public Operator
{
    /* Takes a variable amount of inputs.
     * The rank of this operators output is the highest input rank + 1.
     * This functionality would be used to take data from multiple modules and
     * assign virtual channel numbers to it. For example it there's 4 MSCF16s
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


    QVector<Transport *> inputs;

    virtual void step() override
    {
        // swap as usual
        output.current.swap(output.last);

        // calc output size. FIXME: this should be done in a preparation step
        int output_size = 0;
        for (auto transport: inputs)
        {
            output_size += transport->current.size();
        }
        output.current.resize(output_size);

        for (int input_index = 0;
             input_index < inputs.size();
             ++input_index)
        {
            Transport *transport = inputs[input_index];

            for (int address = 0;
                 address < transport->current.size();
                 ++address)
            {
                const auto &param(transport->current[address]);
                if (param.valid)
                {
                    int output_address = address + (input_index * 16);

                    output.current[output_address] = param; // copy the param struct
                }
            }
        }
    }
};

/* XXX: If an input channel (current/last) could be explicitly selected then
 * the effect of DifferenceToPreviousValue could be created by using the
 * Difference operator with the same input but using the "current" channel for
 * inputA and the "last" channel for inputB. */

struct DifferenceToPreviousValue: public Operator
{
    Transport *input;
    
    virtual void step() override
    {
        output.current.swap(output.last);

        if (input)
        {
            s32 minSize = std::min(input->current.size(), input->last.size());

            output.current.resize(minSize);
            clear_parameters(output.current);

            for (s32 paramIndex = 0;
                 paramIndex < minSize;
                 ++paramIndex)
            {
                const auto &lastParam(input->last[paramIndex]);
                const auto &currentParam(input->current[paramIndex]);

                if (lastParam.valid && currentParam.valid)
                {
                    output.current[paramIndex].valid = true;
                    output.current[paramIndex].type = Parameter::Double;
                    output.current[paramIndex].dval = currentParam.dval - lastParam.dval;
                }
            }
        }
    }
};

// Calculates A - B;
struct Difference: public Operator
{
    Transport *inputA;
    Transport *inputB;

    virtual void step() override
    {
        output.current.swap(output.last);

        if (inputA && inputB)
        {
            s32 minSize = std::min(inputA->current.size(), inputB->current.size());
            output.current.resize(minSize);
            clear_parameters(output.current);

            for (s32 paramIndex = 0;
                 paramIndex < minSize;
                 ++paramIndex)
            {
                const auto &paramA(inputA->current[paramIndex]);
                const auto &paramB(inputB->current[paramIndex]);

                if (paramA.valid && paramB.valid)
                {
                    output.current[paramIndex].valid = true;
                    output.current[paramIndex].type  = Parameter::Double;
                    output.current[paramIndex].dval = paramA.dval - paramB.dval;
                }
            }
        }
    }
};

struct RetainValidValues: public Operator
{
    Transport *input;

    virtual void step() override
    {
        // XXX: this operator does not do the usual swap of output current/last!

        // FIXME: assumption is that output has been resized to input size // before step() is called.
        // FIXME: also assuming that input size does not change at runtime
        if (input)
        {

            for (s32 i = 0; i < input->current.size(); ++i)
            {
                const auto &inputParam(input->current[i]);
                if (inputParam.valid)
                {
                    auto &outputParam(output.current[i]);
                    output.last[i] = outputParam; // current is about to be updated so copy it into last
                    outputParam = inputParam; // copy valid value
                }
            }
        }
    }
};

// Output size is always 1 as this selects a single address of the incoming data
struct Histo2DRectangleCut: public Operator
{
    Transport *inputX;
    Transport *inputY;
    s32 addressX;
    s32 addressY;

    double minX, maxX, minY, maxY;

    virtual void step() override
    {
        if (inputY && inputX)
        {
            output.current.swap(output.last);

        }
    }
};

struct Sink
{
    virtual void step() = 0;
    virtual ~Sink() {};
};

typedef std::shared_ptr<Sink> SinkPtr;

struct Histo1DSink: public Sink
{
    std::shared_ptr<Histo1D> histo;

    Transport *input = nullptr;
    s32 address;

    u32 fillsSinceLastDebug = 0;

    virtual void step() override
    {
        qDebug() << "begin" << __PRETTY_FUNCTION__;
        if (input && histo)
        {
            if (address < input->current.size())
            {
                const auto &parameter(input->current[address]);

                if (parameter.valid)
                {
                    qDebug() << __PRETTY_FUNCTION__ << "histo fill" << "address" << address << "value" << parameter.dval;
                    histo->fill(parameter.dval);
                    ++fillsSinceLastDebug;

                    if (fillsSinceLastDebug > 1000)
                    {
                        histo->debugDump();
                        fillsSinceLastDebug = 0;
                    }
                }
            }
        }
        qDebug() << "end" << __PRETTY_FUNCTION__;
    }
};


struct Histo2DSink: public Sink
{
    Histo2D *histo;

    Transport *inputX;
    Transport *inputY;
    s32 addressX;
    s32 addressY;

    virtual void step() override
    {
        if (inputX && inputY
            && addressX < inputX->current.size()
            && addressY < inputY->current.size())
        {
            const auto &paramX(inputX->current[addressX]);
            const auto &paramY(inputY->current[addressY]);

            if (paramX.valid && paramY.valid)
            {
                histo->fill(paramX.dval, paramY.dval);
            }
        }
    }
};


class Analysis
{
    public:
        struct ExtractorEntry
        {
            int eventIndex;
            int moduleIndex;
            ExtractorPtr extractor;
        };

        QVector<ExtractorEntry> m_extractors;
        QVector<OperatorPtr> m_operators;
        QVector<SinkPtr> m_sinks;

        void beginRun()
        {
            qSort(m_operators.begin(), m_operators.end(), [] (const OperatorPtr &o1, const OperatorPtr &o2) {
                return o1->output.rank < o2->output.rank;
            });


            for (auto &ee: m_extractors)
            {
                extractor_begin_run(*ee.extractor);
            }
        }

        void beginEvent(int eventIndex)
        {
            for (auto &ee: m_extractors)
            {
                if (ee.eventIndex == eventIndex)
                {
                    extractor_begin_event(*ee.extractor);
                }
            }
        }

        void processDataWord(int eventIndex, int moduleIndex, u32 data, u32 wordIndex)
        {
            for (auto &ee: m_extractors)
            {
                if (ee.eventIndex == eventIndex && ee.moduleIndex == moduleIndex)
                {
                    extractor_process_data(*ee.extractor, data, wordIndex);
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
