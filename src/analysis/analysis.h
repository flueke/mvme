#ifndef __ANALYSIS_H__
#define __ANALYSIS_H__

#include "typedefs.h"
#include "data_filter.h"
#include "histo1d.h"
#include "histo2d.h"
#include "../qt_util.h"

#include <memory>
#include <QUuid>

class QJsonObject;

/*
 *   Operators vs Sources vs Sinks:
 *   - Sources have no input but are directly attached to a module.
 *     -> They have eventIndex and moduleIndex whereas operators are only
 *        associated with an event.
 *   - Source have a processDataWord() method, Operators have a step() method
 *   - Sinks usually don't have any output but consume input. Histograms could
 *     have output but outputting whole histograms into ParameterVectors doubles
 *     the storage required for historams.
 */

/* TODO:
 * - Integrate index selection into Histo1DSink. The stand-alone IndexSelector
 *   will still be available and can be instantiated manually if needed.
 *
 */

namespace analysis
{

struct Parameter
{
    bool valid = false;
    double value = 0.0;
    double lowerLimit = 0.0; // inclusive
    double upperLimit = 0.0; // inclusive
};

inline QString to_string(const Parameter &p)
{
    return QString("P(%1, %2, [%3, %4[)")
        .arg(p.valid)
        .arg(p.value)
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
class Pipe;

/* Interface to indicate that something can the be source of a Pipe. */
class PipeSourceInterface: public QObject
{
    Q_OBJECT
    public:
        PipeSourceInterface(QObject *parent = 0)
            : QObject(parent)
            , m_id(QUuid::createUuid())
        {}

        virtual s32 getNumberOfOutputs() const = 0;
        virtual QString getOutputName(s32 outputIndex) const = 0;
        virtual Pipe *getOutput(s32 index) = 0;

        virtual QString getDisplayName() const = 0;

        virtual ~PipeSourceInterface() {}

        QUuid getId() const { return m_id; }
        /* Note: setId() should only be used when restoring the object from a
         * config file. Otherwise just keep the id that's generated in the
         * constructor. */
        void setId(const QUuid &id) { m_id = id; }

    private:
        QUuid m_id;
};

typedef std::shared_ptr<PipeSourceInterface> PipeSourcePtr;

}

#define PipeSourceInterface_iid "com.mesytec.mvme.analysis.PipeSourceInterface.1"
Q_DECLARE_INTERFACE(analysis::PipeSourceInterface, PipeSourceInterface_iid);

namespace analysis
{

class Pipe
{
    public:
        const Parameter *first() const
        {
            if (!parameters.isEmpty())
            {
                return &parameters[0];
            }

            return nullptr;
        }

        Parameter *first()
        {
            if (!parameters.isEmpty())
            {
                return &parameters[0];
            }

            return nullptr;
        }

        const Parameter *getParameter(u32 index)
        {
            if (index < static_cast<u32>(parameters.size()))
            {
                return &parameters[index];
            }

            return nullptr;
        }

        const ParameterVector &getParameters() const { return parameters; }
        ParameterVector &getParameters() { return parameters; }

        void setParameterName(const QString &name) { parameters.name = name; }
        QString getParameterName() const { return parameters.name; }

        PipeSourceInterface *getSource() const { return source; }
        void setSource(PipeSourceInterface *source) { source = source; }

        void addDestination(OperatorInterface *dest)
        {
            if (!destinations.contains(dest))
            {
                destinations.push_back(dest);
            }
        }

        void removeDestination(OperatorInterface *dest)
        {
            destinations.removeAll(dest);
        }

        QVector<OperatorInterface *> getDestinations() const
        {
            return destinations;
        }

        void invalidateAll()
        {
            parameters.invalidateAll();
        }

        s32 getRank() const { return rank; }
        void setRank(s32 newRank) { rank = newRank; }

        ParameterVector parameters;
        PipeSourceInterface *source = nullptr;
        QVector<OperatorInterface *> destinations;
        s32 rank = 0;
};

/* Data source interface. The analysis feeds single data words into this using
 * processDataWord(). */
class SourceInterface: public PipeSourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::PipeSourceInterface)
    public:
        SourceInterface(QObject *parent = 0): PipeSourceInterface(parent) {}

        /* Use beginRun() to preallocate the outputs and setup internal state.
         * This will also be called by Analysis UI to be able to get array
         * sizes from operator output pipes! */
        virtual void beginRun() {}

        /* Use beginEvent() to invalidate output parameters if needed. */
        virtual void beginEvent() {}

        virtual void processDataWord(u32 data, s32 wordIndex) = 0;

        virtual void read(const QJsonObject &json) = 0;
        virtual void write(QJsonObject &json) const = 0;

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

        /* Use beginRun() to preallocate the outputs and setup internal state. */
        virtual void beginRun() {}

        virtual void step() = 0;

        virtual s32 getNumberOfInputs() const = 0;
        virtual QString getInputName(s32 inputIndex) const = 0;
        virtual void setInput(s32 index, Pipe *inputPipe) = 0;
        virtual Pipe *getInput(s32 index) const = 0;
        virtual void removeInput(Pipe *pipe) = 0;

        virtual void read(const QJsonObject &json) = 0;
        virtual void write(QJsonObject &json) const = 0;

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

        virtual s32 getNumberOfOutputs() const override;
        virtual QString getOutputName(s32 outputIndex) const override;
        virtual Pipe *getOutput(s32 index) override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Extractor"); }


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

        s32 getNumberOfInputs() const override;
        QString getInputName(s32 inputIndex) const override;
        void setInput(s32 index, Pipe *inputPipe) override;
        Pipe *getInput(s32 index) const override;
        void removeInput(Pipe *pipe) override;

        s32 getNumberOfOutputs() const override;
        QString getOutputName(s32 outputIndex) const override;
        Pipe *getOutput(s32 index) override;

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
        s32 getNumberOfInputs() const override;
        QString getInputName(s32 inputIndex) const override;
        void setInput(s32 index, Pipe *inputPipe) override;
        Pipe *getInput(s32 index) const override;
        void removeInput(Pipe *pipe) override;

        s32 getNumberOfOutputs() const override;
        QString getOutputName(s32 outputIndex) const override;
        Pipe *getOutput(s32 index) override;

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

class Calibration: public BasicOperator
{
    Q_OBJECT
    public:
        Calibration(QObject *parent = 0);

        virtual void beginRun() override;
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

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Calibration"); }

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
        s32 output_size = 0;
        for (auto transport: inputs)
        {
            output_size += transport->parameters.size();
        }
        output.parameters.resize(output_size);

        for (s32 input_index = 0;
             input_index < inputs.size();
             ++input_index)
        {
            Pipe *transport = inputs[input_index];

            for (s32 address = 0;
                 address < transport->parameters.size();
                 ++address)
            {
                const auto &param(transport->parameters[address]);
                if (param.valid)
                {
                    s32 output_address = address + (input_index * 16);

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

        virtual void beginRun() override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Index Selection"); }

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
                    output.parameters[paramIndex].value = paramA.value - paramB.value;
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
                double x = parX.value;
                double y = parY.value;

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
        u32 inputIndex;

        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Histo1D"); }

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
                qDebug() << __PRETTY_FUNCTION__ << "fill" << histo.get() << paramX.value << paramY.value;
                histo->fill(paramX.value, paramY.value);
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

template<typename T>
SourceInterface *createSource()
{
    return new T;
}

template<typename T>
OperatorInterface *createOperator()
{
    return new T;
}

class Registry
{
    public:
        template<typename T>
        bool registerSource(const QString &name)
        {
            if (m_sourceRegistry.contains(name))
                return false;

            m_sourceRegistry.insert(name, &createSource<T>);

            return true;
        }

        template<typename T>
        bool registerSource()
        {
            QString className = T::staticMetaObject.className();
            return registerSource<T>(className);
        }

        template<typename T>
        bool registerOperator(const QString &name)
        {
            if (m_operatorRegistry.contains(name))
                return false;

            m_operatorRegistry.insert(name, &createOperator<T>);

            return true;
        }

        template<typename T>
        bool registerOperator()
        {
            QString className = T::staticMetaObject.className();
            return registerOperator<T>(className);
        }


        SourceInterface *makeSource(const QString &name)
        {
            SourceInterface *result = nullptr;

            if (m_sourceRegistry.contains(name))
            {
                result = m_sourceRegistry[name]();
            }

            return result;
        }

        OperatorInterface *makeOperator(const QString &name)
        {
            OperatorInterface *result = nullptr;

            if (m_operatorRegistry.contains(name))
            {
                result = m_operatorRegistry[name]();
            }

            return result;
        }

        QStringList getSourceNames() const
        {
            return m_sourceRegistry.keys();
        }

        QStringList getOperatorNames() const
        {
            return m_operatorRegistry.keys();
        }

    private:
        QMap<QString, SourceInterface *(*)()> m_sourceRegistry;
        QMap<QString, OperatorInterface *(*)()> m_operatorRegistry;
};

class Analysis
{
    public:
        struct SourceEntry
        {
            s32 eventIndex;
            s32 moduleIndex;
            SourcePtr source;
        };

        struct OperatorEntry
        {
            s32 eventIndex;
            OperatorPtr op;
            // A user defined level used for UI display structuring.
            s32 userLevel;
        };

        Analysis();

        void beginRun();
        void beginEvent(s32 eventIndex);
        void processDataWord(s32 eventIndex, s32 moduleIndex, u32 data, s32 wordIndex);
        void endEvent(s32 eventIndex);

        const QVector<SourceEntry> &getSources() const
        {
            return m_sources;
        }

        QVector<SourceEntry> getSources(s32 eventIndex, s32 moduleIndex) const
        {
            QVector<SourceEntry> result;

            for (const auto &e: m_sources)
            {
                if (e.eventIndex == eventIndex && e.moduleIndex == moduleIndex)
                {
                    result.push_back(e);
                }
            }

            return result;
        }

        void addSource(s32 eventIndex, s32 moduleIndex, const SourcePtr &source)
        {
            m_sources.push_back({eventIndex, moduleIndex, source});
            updateRanks();
        }

        const QVector<OperatorEntry> &getOperators() const
        {
            return m_operators;
        }

        const QVector<OperatorEntry> getOperators(s32 eventIndex) const
        {
            QVector<OperatorEntry> result;

            for (const auto &e: m_operators)
            {
                if (e.eventIndex == eventIndex)
                {
                  result.push_back(e);
                }
            }

            return result;
        }

        const QVector<OperatorEntry> getOperators(s32 eventIndex, s32 userLevel) const
        {
            QVector<OperatorEntry> result;

            for (const auto &e: m_operators)
            {
                if (e.eventIndex ==eventIndex && e.userLevel == userLevel)
                {
                  result.push_back(e);
                }
            }

            return result;
        }

        void addOperator(s32 eventIndex, const OperatorPtr &op, s32 userLevel)
        {
            m_operators.push_back({eventIndex, op, userLevel});
            updateRanks();
        }

        void removeSource(const SourcePtr &source);
        void removeOperator(const OperatorPtr &op);

        void clear();

        void read(const QJsonObject &json);
        void write(QJsonObject &json) const;

        s32 getModuleIndex(const SourcePtr &src) const { return getModuleIndex(src.get()); }
        s32 getModuleIndex(const SourceInterface *src) const
        {
            for (const auto &sourceEntry: m_sources)
            {
                if (sourceEntry.source.get() == src)
                {
                    return sourceEntry.moduleIndex;
                }
            }
            return -1 ;
        }

        s32 getEventIndex(const SourcePtr &src) const { return getEventIndex(src.get()); }
        s32 getEventIndex(const SourceInterface *src) const
        {
            for (const auto &sourceEntry: m_sources)
            {
                if (sourceEntry.source.get() == src)
                {
                    return sourceEntry.moduleIndex;
                }
            }
            return -1;
        }

        s32 getEventIndex(const OperatorPtr &op) const { return getEventIndex(op.get()); }
        s32 getEventIndex(const OperatorInterface *op) const
        {
            for (const auto &opEntry: m_operators)
            {
                if (opEntry.op.get() == op)
                {
                    return opEntry.eventIndex;
                }
            }
            return -1;
        }

        void updateRanks();

    private:
        void updateRank(OperatorInterface *op, QSet<OperatorInterface *> &updated);

        QVector<SourceEntry> m_sources;
        QVector<OperatorEntry> m_operators;

        Registry m_registry;
};

struct RawDataDisplay
{
    using Histo1DSinkVector = QVector<std::shared_ptr<Histo1DSink>>;

    std::shared_ptr<Extractor> extractor;
    Histo1DSinkVector rawHistoSinks;
    std::shared_ptr<Calibration> calibration;
    Histo1DSinkVector calibratedHistoSinks;
};

RawDataDisplay make_raw_data_display(const MultiWordDataFilter &extractionFilter, double unitMin, double unitMax,
                                     const QString &name, const QString &xAxisTitle, const QString &unitLabel);

void add_raw_data_display(Analysis *analysis, s32 eventIndex, s32 moduleIndex, const RawDataDisplay &display);

}
#endif /* __ANALYSIS_H__ */
