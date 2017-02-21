#ifndef __ANALYSIS_H__
#define __ANALYSIS_H__

#include "typedefs.h"
#include "data_filter.h"
#include "histo1d.h"
#include "histo2d.h"
#include "../util.h"

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

namespace analysis
{

struct Parameter
{
    bool valid = false;
    double value = 0.0;
    double lowerLimit = 0.0; // inclusive
    double upperLimit = 0.0; // inclusive
};

inline bool isParameterValid(const Parameter *param)
{
    return (param && param->valid);
}

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
        {
            qDebug() << __PRETTY_FUNCTION__ << reinterpret_cast<void *>(this);
        }

        virtual ~PipeSourceInterface()
        {
            qDebug() << __PRETTY_FUNCTION__ << reinterpret_cast<void *>(this);
        }

        virtual s32 getNumberOfOutputs() const = 0;
        virtual QString getOutputName(s32 outputIndex) const = 0;
        virtual Pipe *getOutput(s32 index) = 0;

        virtual QString getDisplayName() const = 0;

        QUuid getId() const { return m_id; }
        /* Note: setId() should only be used when restoring the object from a
         * config file. Otherwise just keep the id that's generated in the
         * constructor. */
        void setId(const QUuid &id) { m_id = id; }

    private:
        PipeSourceInterface() = delete;
        QUuid m_id;
};

typedef std::shared_ptr<PipeSourceInterface> PipeSourcePtr;

} // end namespace analysis

// This needs to happen outside any namespace
#define PipeSourceInterface_iid "com.mesytec.mvme.analysis.PipeSourceInterface.1"
Q_DECLARE_INTERFACE(analysis::PipeSourceInterface, PipeSourceInterface_iid);

namespace analysis
{

struct InputType
{
    static const u32 Invalid = 0;
    static const u32 Array   = 1u << 0;
    static const u32 Value   = 1u << 1;
    static const u32 Both    = (Array | Value);
};


// The destination of a Pipe
struct Slot
{
    Slot(OperatorInterface *parentOp, s32 parentSlotIndex, const QString &name, u32 acceptedInputs = InputType::Both)
        : parentOperator(parentOp)
        , parentSlotIndex(parentSlotIndex)
        , name(name)
        , acceptedInputTypes(acceptedInputs)
    {}

    /* Sets inputPipe to be the new input for this Slot. */
    void connectPipe(Pipe *inputPipe, s32 paramIndex);
    /* Clears this slots input. */
    void disconnectPipe();

    static const s32 NoParamIndex = -1; // special paramIndex value for InputType::Array

    u32 acceptedInputTypes = InputType::Both;
    s32 paramIndex = NoParamIndex; // parameter index for InputType::Value or NoParamIndex
    Pipe *inputPipe = nullptr;
    
    // The owner of this Slot.
    OperatorInterface *parentOperator = nullptr;

    /* The index of this Slot in parentOperator. If correctly setup the
     * following should be true:
     * (parentOperator->getSlot(parentSlotIndex) == this)
     */
    s32 parentSlotIndex = -1;
    /* The name if this Slot in the parentOperator. Set by the parentOperator. */
    QString name;
};

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
        void setSource(PipeSourceInterface *theSource) { source = theSource; }

        void addDestination(Slot *dest)
        {
            if (!destinations.contains(dest))
            {
                destinations.push_back(dest);
            }
        }

        void removeDestination(Slot *dest)
        {
            destinations.removeAll(dest);
        }

        QVector<Slot *> getDestinations() const
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
        /* The index of this Pipe in source. If correctly setup the following
         * should be true:
         * (this->source->getOutput(sourceOutputIndex) == this)
         */
        s32 sourceOutputIndex = 0;
        QVector<Slot *> destinations;
        s32 rank = 0;
};

/* Data source interface. The analysis feeds single data words into this using
 * processDataWord(). */
class SourceInterface: public PipeSourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::PipeSourceInterface)
    public:
        using PipeSourceInterface::PipeSourceInterface;

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
        using PipeSourceInterface::PipeSourceInterface;
        //OperatorInterface(QObject *parent = 0):
        //    PipeSourceInterface(parent)
        //{ qDebug() << __PRETTY_FUNCTION__ << reinterpret_cast<void *>(this); }

        //~OperatorInterface()
        //{ qDebug() << __PRETTY_FUNCTION__ << reinterpret_cast<void *>(this); }

        /* Use beginRun() to preallocate the outputs and setup internal state. */
        virtual void beginRun() {}

        virtual void step() = 0;

        virtual s32 getNumberOfSlots() const = 0;

        virtual Slot *getSlot(s32 slotIndex) = 0;

        // FIXME: Not sure if this is needed or disconnectSlot(s32 slotIndex) is needed or both
        //virtual void disconnectSlot(Pipe *sourcePipe) = 0;

        virtual void read(const QJsonObject &json) = 0;
        virtual void write(QJsonObject &json) const = 0;

        /* If paramIndex is Slot::NoParamIndex the operator should use the whole array. */
        bool connectInputSlot(s32 slotIndex, Pipe *inputPipe, s32 paramIndex);

        void connectArrayToInputSlot(s32 slotIndex, Pipe *inputPipe)
        { connectInputSlot(slotIndex, inputPipe, Slot::NoParamIndex); }

        //void connectValueToInputSlot(s32 slotIndex, Pipe *inputPipe, s32 paramIndex)
        //{ connectInputSlot(slotIndex, inputPipe, paramIndex); }

        s32 getMaximumInputRank();
        s32 getMaximumOutputRank();
};

typedef std::shared_ptr<OperatorInterface> OperatorPtr;

} // end namespace analysis

#define SourceInterface_iid "com.mesytec.mvme.analysis.SourceInterface.1"
Q_DECLARE_INTERFACE(analysis::SourceInterface, SourceInterface_iid);

#define OperatorInterface_iid "com.mesytec.mvme.analysis.OperatorInterface.1"
Q_DECLARE_INTERFACE(analysis::OperatorInterface, OperatorInterface_iid);

namespace analysis
{
/* Base class for sinks. Sinks are operators with no output. In the UI these
 * operators are shown in the data display section */
class SinkInterface: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        using OperatorInterface::OperatorInterface;
        //SinkInterface(QObject *parent = 0): OperatorInterface(parent) {}
        //~SinkInterface() {}

        // PipeSourceInterface
        s32 getNumberOfOutputs() const override { return 0; }
        QString getOutputName(s32 outputIndex) const override { return QString(); }
        Pipe *getOutput(s32 index) override { return nullptr; }
};

} // end namespace analysis

#define SinkInterface_iid "com.mesytec.mvme.analysis.SinkInterface.1"
Q_DECLARE_INTERFACE(analysis::SinkInterface, SinkInterface_iid);

namespace analysis
{

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

/* An operator with one input slot and one output pipe. Only step() needs to be
 * implemented in subclasses. The input slot by default accepts both
 * InputType::Array and InputType::Value.  */
class BasicOperator: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        BasicOperator(QObject *parent = 0);
        ~BasicOperator();

        // PipeSourceInterface
        s32 getNumberOfOutputs() const override;
        QString getOutputName(s32 outputIndex) const override;
        Pipe *getOutput(s32 index) override;

        // OperatorInterface
        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

    protected:
        Pipe m_output;
        Slot m_inputSlot;
};

/* An operator with one input and no output. The input slot by default accepts
 * both InputType::Array and InputType::Value. */
class BasicSink: public SinkInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SinkInterface)
    public:
        BasicSink(QObject *parent = 0);
        ~BasicSink();

        // OperatorInterface
        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

    protected:
        Slot m_inputSlot;
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

class IndexSelector: public BasicOperator
{
    Q_OBJECT
    public:
        IndexSelector(QObject *parent = 0);

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

class PreviousValue: public BasicOperator
{
    Q_OBJECT
    public:
        PreviousValue(QObject *parent = 0);

        virtual void beginRun() override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Previous Value"); }

    private:
        ParameterVector m_previousInput;
};

class RetainValid: public BasicOperator
{
    Q_OBJECT
    public:
        RetainValid(QObject *parent = 0);

        virtual void beginRun() override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Retain Valid"); }

    private:
        ParameterVector m_lastValidInput;
};

class Difference: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        Difference(QObject *parent = 0);

        virtual void beginRun() override;
        virtual void step() override;

        // PipeSourceInterface
        s32 getNumberOfOutputs() const override { return 1; }
        QString getOutputName(s32 outputIndex) const override { return QSL("difference"); }
        Pipe *getOutput(s32 index) override { return (index == 0) ? &m_output : nullptr; }

        // OperatorInterface
        virtual s32 getNumberOfSlots() const override { return 2; }
        virtual Slot *getSlot(s32 slotIndex) override
        {
            if (slotIndex == 0) return &m_inputA;
            if (slotIndex == 1) return &m_inputB;
            return nullptr;
        }

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Difference"); }

    private:
        Slot m_inputA;
        Slot m_inputB;
        Pipe m_output;
};

//
// Sinks
//
class Histo1DSink: public BasicSink
{
    Q_OBJECT
    public:
        using BasicSink::BasicSink;

        QVector<std::shared_ptr<Histo1D>> histos;

        virtual void beginRun() override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("1D Histogram"); }

    private:
        u32 fillsSinceLastDebug = 0;
};

class Histo2DSink: public SinkInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SinkInterface)
    public:
        Histo2DSink(QObject *parent = 0);

        // OperatorInterface
        virtual s32 getNumberOfSlots() const override;
        //virtual void connectInputSlot(s32 slotIndex, Pipe *inputPipe, s32 paramIndex) override;
        virtual Slot *getSlot(s32 slotIndex) override;
        //virtual void disconnectSlot(Pipe *sourcePipe) override;

        virtual void beginRun() override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("2D Histogram"); }

    public:
        std::shared_ptr<Histo2D> m_histo;
        Slot m_inputX;
        Slot m_inputY;
};


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

template<typename T>
SinkInterface *createSink()
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

        template<typename T>
        bool registerSink(const QString &name)
        {
            if (m_sinkRegistry.contains(name))
                return false;

            m_sinkRegistry.insert(name, &createSink<T>);

            return true;
        }

        template<typename T>
        bool registerSink()
        {
            QString className = T::staticMetaObject.className();
            return registerSink<T>(className);
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

        SinkInterface *makeSink(const QString &name)
        {
            SinkInterface *result = nullptr;

            if (m_sinkRegistry.contains(name))
            {
                result = m_sinkRegistry[name]();
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

        QStringList getSinkNames() const
        {
            return m_sinkRegistry.keys();
        }

    private:
        QMap<QString, SourceInterface *(*)()> m_sourceRegistry;
        QMap<QString, OperatorInterface *(*)()> m_operatorRegistry;
        QMap<QString, SinkInterface *(*)()> m_sinkRegistry;
};

class Analysis
{
    public:
        struct SourceEntry
        {
            s32 eventIndex; // TODO: eventId
            s32 moduleIndex; // TODO: moduleId
            SourcePtr source;
        };

        struct OperatorEntry
        {
            s32 eventIndex; // TODO: eventId
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

        Registry &getRegistry() { return m_registry; }

    private:
        void updateRank(OperatorInterface *op, QSet<OperatorInterface *> &updated);

        QVector<SourceEntry> m_sources;
        QVector<OperatorEntry> m_operators;

        Registry m_registry;
};

struct RawDataDisplay
{
    std::shared_ptr<Extractor> extractor;
    std::shared_ptr<Histo1DSink> rawHistoSink;
    std::shared_ptr<Calibration> calibration;
    std::shared_ptr<Histo1DSink> calibratedHistoSink;
};

RawDataDisplay make_raw_data_display(const MultiWordDataFilter &extractionFilter, double unitMin, double unitMax,
                                     const QString &name, const QString &xAxisTitle, const QString &unitLabel);

void add_raw_data_display(Analysis *analysis, s32 eventIndex, s32 moduleIndex, const RawDataDisplay &display);

} // end namespace analysis




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























#endif /* __ANALYSIS_H__ */

