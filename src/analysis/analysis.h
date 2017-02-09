#ifndef __ANALYSIS_H__
#define __ANALYSIS_H__

#include "typedefs.h"
#include "data_filter.h"
#include "histo1d.h"
#include "histo2d.h"

#include <memory>

class QJsonObject;

/* TODO: rank calculation
 *   Operators vs Sinks:
 *   - difference was: sinks have no output
 *   - both Histo1DSink and Histo2DSink could have outputs: an array of the
 *     bins of the histogram (information about the bins would still have to be
 *     carried around elsewhere, bin width or histo min max value)
 *     Producing the output for histos is expensive compared to something like
 *     a data filter with 5 address bits).
 *     Solutions: - Make sinks have no output
 *                - Only generate output if there are consumers
 *                  Also make updating of the output efficient: Only change
 *                  modified bins, otherwise leave the output untouched.
 *
 *   Operators vs Sources:
 *   - Sources have no input but are directly attached to a module.
 *     -> They have eventIndex and moduleIndex whereas operators are only
 *        associated with an event.
 *   - Source have a processDataWord() method
 *

 *
 */

namespace analysis
{

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

inline QString to_string(const Parameter &p)
{
    return QString("P(%1, %2, [%3, %4[)")
        .arg(p.valid)
        .arg(p.dval)
        .arg(p.lowerLimit)
        .arg(p.upperLimit)
        ;
}

struct ParameterVector: public QVector<Parameter>
{
    void invalidateAll()
    {
        for (auto &param: *this)
        {
            param.valid = false;
        }
    }

    QString name;
    QString unit;
};

class OperatorInterface;

/* Interface to indicate that something can the be source of a Pipe. Mostly
 * exists to have a common base for SourceInterface and OperatorInterface... */
class PipeSourceInterface: public QObject
{
    Q_OBJECT
    public:
        PipeSourceInterface(QObject *parent = 0): QObject(parent) {}
        virtual ~PipeSourceInterface() {}
};

}

#define PipeSourceInterface_iid "com.mesytec.mvme.analysis.PipeSourceInterface.1"
Q_DECLARE_INTERFACE(analysis::PipeSourceInterface, PipeSourceInterface_iid);

namespace analysis
{

class Pipe
{
    public:
        const Parameter &first() const
        {
            if (!m_parameters.isEmpty())
            {
                return m_parameters[0];
            }

            return dummy;
        }

        Parameter &first()
        {
            if (m_parameters.isEmpty())
            {
                m_parameters.resize(1);
            }
            return m_parameters[0];
        }

        const ParameterVector &getParameters() const { return m_parameters; }
        ParameterVector &getParameters() { return m_parameters; }

        PipeSourceInterface *getSource() const { return m_source; }
        void setSource(PipeSourceInterface *source) { m_source = source; }

        void addDestination(OperatorInterface *dest)
        {
            if (!m_destinations.contains(dest))
            {
                m_destinations.push_back(dest);
            }
        }

        void removeDestination(OperatorInterface *dest)
        {
            m_destinations.removeAll(dest);
        }

        QVector<OperatorInterface *> getDestinations() const
        {
            return m_destinations;
        }

        void invalidateAll()
        {
            m_parameters.invalidateAll();
        }

        s32 getRank() const { return m_rank; }
        void setRank(s32 rank) { m_rank = rank; }

    private:
        const Parameter dummy = {};

        ParameterVector m_parameters;

        PipeSourceInterface *m_source = nullptr;
        QVector<OperatorInterface *> m_destinations;

        s32 m_rank = 0;
};

/* Data source interface. The analysis feeds single data words into this using
 * processDataWord(). */
class SourceInterface: public PipeSourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::PipeSourceInterface)
    public:
        SourceInterface(QObject *parent = 0): PipeSourceInterface(parent) {}

        virtual void beginRun() {}
        virtual void beginEvent() {}
        virtual void processDataWord(u32 data, s32 wordIndex) = 0;

        virtual int getNumberOfOutputs() const = 0;
        virtual QString getOutputName(int outputIndex) const = 0;
        virtual Pipe *getOutput(int index) = 0;

        //virtual void read(const QJsonObject &json) const = 0;
        //virtual void write(QJsonObject &json) const = 0;

        virtual ~SourceInterface() {}
};

/* Operator interface. Consumes one or multiple input pipes and produces one or
 * multiple output pipes. */
class OperatorInterface: public PipeSourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::PipeSourceInterface)
    public:
        OperatorInterface(QObject *parent = 0): PipeSourceInterface(parent) {}

        virtual void beginRun() {}
        virtual void step() = 0;

        virtual int getNumberOfInputs() const = 0;
        virtual QString getInputName(int inputIndex) const = 0;
        virtual void setInput(int index, Pipe *inputPipe) = 0;
        virtual Pipe *getInput(int index) const = 0;
        virtual void removeInput(Pipe *pipe) = 0;

        virtual int getNumberOfOutputs() const = 0;
        virtual QString getOutputName(int outputIndex) const = 0;
        virtual Pipe *getOutput(int index) = 0;

        //virtual void read(const QJsonObject &json) const = 0;
        //virtual void write(QJsonObject &json) const = 0;

        virtual ~OperatorInterface() {}

        s32 getMaximumInputRank();
        s32 getMaximumOutputRank();
};

}

#define SourceInterface_iid "com.mesytec.mvme.analysis.SourceInterface.1"
Q_DECLARE_INTERFACE(analysis::SourceInterface, SourceInterface_iid);

#define OperatorInterface_iid "com.mesytec.mvme.analysis.OperatorInterface.1"
Q_DECLARE_INTERFACE(analysis::OperatorInterface, OperatorInterface_iid);

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

// dest = a - b iff a and b are valid and a and b are of the same type
// FIXME: not a good idea for Uint values as they're _unsigned_! Could convert
// the result to be of type Double... Same for Bool values as `false - true = -1'.
inline void subtract_params(const Parameter &a, const Parameter &b, Parameter &dest)
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

//
// Sources
//

typedef std::shared_ptr<SourceInterface> SourcePtr;

/* A Source using a MultiWordDataFilter for data extraction. Additionally
 * requiredCompletionCount can be set to only produce output for the nth
 * match (in the current event). */
class Extractor: public SourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SourceInterface)

    public:
        Extractor(QObject *parent = 0);

        const MultiWordDataFilter &getFilter() const { return m_filter; }
        MultiWordDataFilter &getFilter() { return m_filter; }
        void setFilter(const MultiWordDataFilter &filter) { m_filter = filter; }

        u32 getRequiredCompletionCount() const { return m_requiredCompletionCount; }
        void setRequiredCompletionCount(u32 count) { m_requiredCompletionCount = count; }

        virtual void beginRun() override;
        virtual void beginEvent() override;
        virtual void processDataWord(u32 data, s32 wordIndex) override;

        virtual int getNumberOfOutputs() const override;
        virtual QString getOutputName(int outputIndex) const override;
        virtual Pipe *getOutput(int index) override;

    private:
        // configuration
        MultiWordDataFilter m_filter;
        u32 m_requiredCompletionCount = 0;

        // state
        u32 m_currentCompletionCount = 0;

        Pipe m_output;
};

//
// Operators
//

typedef std::shared_ptr<OperatorInterface> OperatorPtr;

/* An operator with one input and one output pipe. Only step() needs to be
 * implemented in subclasses. */
class BasicOperator: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        BasicOperator(QObject *parent = 0);
        ~BasicOperator();

        int getNumberOfInputs() const override;
        QString getInputName(int inputIndex) const override;
        void setInput(int index, Pipe *inputPipe) override;
        Pipe *getInput(int index) const override;
        void removeInput(Pipe *pipe) override;

        int getNumberOfOutputs() const override;
        QString getOutputName(int outputIndex) const override;
        Pipe *getOutput(int index) override;

    protected:
        Pipe m_output;
        Pipe *m_input = nullptr;
};

/* An operator with one input and no output. Only step() needs to be
 * implemented in subclasses. */
class BasicSink: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        using OperatorInterface::OperatorInterface;
        ~BasicSink();
        int getNumberOfInputs() const override;
        QString getInputName(int inputIndex) const override;
        void setInput(int index, Pipe *inputPipe) override;
        Pipe *getInput(int index) const override;
        void removeInput(Pipe *pipe) override;

        int getNumberOfOutputs() const override;
        QString getOutputName(int outputIndex) const override;
        Pipe *getOutput(int index) override;

    protected:
        Pipe *m_input = nullptr;
};

struct CalibrationParameters
{
    CalibrationParameters()
    {}

    CalibrationParameters(double factor, double offset)
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

class CalibrationOperator: public BasicOperator
{
    Q_OBJECT
    public:
        CalibrationOperator(QObject *parent = 0);

        virtual void step() override;

        void setGlobalCalibration(const CalibrationParameters &params)
        {
            m_globalCalibration = params;
        }

        void setGlobalCalibration(double factor, double offset)
        {
            m_globalCalibration = CalibrationParameters(factor, offset);
        }

        CalibrationParameters getGlobalCalibration() const
        {
            return m_globalCalibration;
        }

        void setCalibration(s32 address, const CalibrationParameters &params);
        void setCalibration(s32 address, double factor, double offset)
        {
            setCalibration(address, CalibrationParameters(factor, offset));
        }

        s32 getCalibrationCount() const
        {
            return m_calibrations.size();
        }

        CalibrationParameters getCalibration(s32 address) const;

        QString getUnitLabel() const { return m_unit; }
        void setUnitLabel(const QString &label) { m_unit = label; }

    private:
        CalibrationParameters m_globalCalibration;
        QVector<CalibrationParameters> m_calibrations;
        QString m_unit;
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

class IndexSelector: public BasicOperator
{
    Q_OBJECT
    public:
        IndexSelector(QObject *parent = 0): BasicOperator(parent) {}

        void setIndex(s32 index) { m_index = index; }
        s32 getIndex() const { return m_index; }

        virtual void step() override;

    private:
        s32 m_index;
};

#if 0
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

    virtual QVector<Pipe *> getInputs() override
    {
        QVector<Pipe *> result = { inputA, inputB };

        return result;
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

    virtual QVector<Pipe *> getInputs() override
    {
        QVector<Pipe *> result = { input };

        return result;
    }
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

    virtual QVector<Pipe *> getInputs() override
    {
        QVector<Pipe *> result = { input };

        return result;
    }
};

// Output is a boolean flag
struct Histo2DRectangleCut: public Operator
{
    Pipe *inputX;
    Pipe *inputY;

    double minX, maxX, minY, maxY;

    virtual void step() override
    {
        auto &outParam(output.first());
        outParam.valid = false;

        if (inputX && inputY)
        {
            outParam.valid = true;
            outParam.type = Parameter::Bool;
            outParam.bval = false;

            const auto &parX(inputX->first());
            const auto &parY(inputY->first());

            if (parX.valid && parY.valid)
            {
                double x = parX.dval;
                double y = parY.dval;

                if (x >= minX && x < maxX
                    && y >= minY && y < maxY)
                {
                    outParam.bval = true;
                }
            }
        }
    }

    virtual QVector<Pipe *> getInputs() override
    {
        QVector<Pipe *> result = { inputX, inputY };

        return result;
    }
};
#endif

//
// Sinks
//
// Accepts a single value as input
class Histo1DSink: public BasicSink
{
    Q_OBJECT
    public:
        std::shared_ptr<Histo1D> histo;

        virtual void step() override;

    private:
        u32 fillsSinceLastDebug = 0;
};


#if 0
struct Histo2DSink: public Operator
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

    virtual bool hasOutput() const override { return false; }

    virtual QVector<Pipe *> getInputs() override
    {
        QVector<Pipe *> result;

        return result;
    }
};
#endif

class Analysis
{
    public:
        struct SourceEntry
        {
            int eventIndex;
            int moduleIndex;
            SourcePtr source;
        };

        struct OperatorEntry
        {
            int eventIndex;
            OperatorPtr op;
        };

        void beginRun();
        void beginEvent(int eventIndex);
        void processDataWord(int eventIndex, int moduleIndex, u32 data, s32 wordIndex);
        void endEvent(int eventIndex);

        const QVector<SourceEntry> &getSources() const
        {
            return m_sources;
        }

        const QVector<OperatorEntry> &getOperators() const
        {
            return m_operators;
        }

        void addSource(int eventIndex, int moduleIndex, const SourcePtr &source)
        {
            m_sources.push_back({eventIndex, moduleIndex, source});
        }

        void addOperator(int eventIndex, const OperatorPtr &op)
        {
            m_operators.push_back({eventIndex, op});
        }

        void removeSource(const SourcePtr &source);
        void removeOperator(const OperatorPtr &op);


        void clear()
        {
            m_sources.clear();
            m_operators.clear();
        }

        void read(const QJsonObject &json);
        void write(QJsonObject &json) const;

    private:
        void updateRanks();
        void updateRank(OperatorInterface *op, QSet<OperatorInterface *> &updated);

        QVector<SourceEntry> m_sources;
        QVector<OperatorEntry> m_operators;

};

}
#endif /* __ANALYSIS_H__ */
