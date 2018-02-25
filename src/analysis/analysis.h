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
#ifndef __ANALYSIS_H__
#define __ANALYSIS_H__

#include "a2/a2.h"
#include "a2/memory.h"
#include "a2/multiword_datafilter.h"
#include "data_filter.h"
#include "histo1d.h"
#include "histo2d.h"
#include "libmvme_export.h"
#include "typedefs.h"
#include "../globals.h"
#include "../rate_monitor_base.h"
#include "../vme_analysis_common.h"

#include <memory>
#include <pcg_random.hpp>
#include <QMutex>
#include <QUuid>
#include <qwt_interval.h>

class QJsonObject;
class VMEConfig;

namespace memory
{
struct Arena;
};

/*
 *   Operators vs Sources vs Sinks:
 *   - Sources have no input but are directly attached to a module.
 *     -> They have eventIndex and moduleIndex whereas operators are only
 *        associated with an event.
 *   - Source have a processModuleData() method, Operators have a step() method
 *   - Sinks usually don't have any output but consume input. Histograms could
 *     have output but outputting whole histograms into ParameterVectors doubles
 *     the storage required for historams.
 */

namespace analysis
{
struct A2AdapterState;

struct LIBMVME_EXPORT Parameter
{
    bool valid = false;
    double value = 0.0;
    double lowerLimit = 0.0; // inclusive
    double upperLimit = 0.0; // exclusive
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

struct LIBMVME_EXPORT ParameterVector: public QVector<Parameter>
{
    void invalidateAll()
    {
        for (auto &param: *this)
        {
            param.valid = false;
        }
    }

    // Note: name is currently not used anywhere in the gui
    QString name;
    QString unit;
};

class OperatorInterface;
class Pipe;

/* Interface to indicate that something can the be source of a Pipe. */
class LIBMVME_EXPORT PipeSourceInterface: public QObject, public std::enable_shared_from_this<PipeSourceInterface>
{
    Q_OBJECT
    public:
        PipeSourceInterface(QObject *parent = 0)
            : QObject(parent)
            , m_id(QUuid::createUuid())
        {
            //qDebug() << __PRETTY_FUNCTION__ << reinterpret_cast<void *>(this);
        }

        virtual ~PipeSourceInterface()
        {
            //qDebug() << __PRETTY_FUNCTION__ << reinterpret_cast<void *>(this);
        }

        virtual s32 getNumberOfOutputs() const = 0;
        virtual QString getOutputName(s32 outputIndex) const = 0;
        virtual Pipe *getOutput(s32 index) = 0;

        virtual QString getDisplayName() const = 0;
        virtual QString getShortName() const = 0;

        QUuid getId() const { return m_id; }
        /* Note: setId() should only be used when restoring the object from a
         * config file. Otherwise just keep the id that's generated in the
         * constructor. */
        void setId(const QUuid &id) { m_id = id; }

        /* Use beginRun() to preallocate the outputs and setup internal state.
         * This will also be called by Analysis UI to be able to get array
         * sizes from operator output pipes! */
        virtual void beginRun(const RunInfo &runInfo) = 0;

        std::shared_ptr<PipeSourceInterface> getSharedPointer() { return shared_from_this(); }

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

struct Slot;

class LIBMVME_EXPORT Pipe
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

        Parameter *getParameter(u32 index)
        {
            if (index < static_cast<u32>(parameters.size()))
            {
                return &parameters[index];
            }

            return nullptr;
        }

        const ParameterVector &getParameters() const { return parameters; }
        ParameterVector &getParameters() { return parameters; }

        inline s32 getSize() const
        {
            return parameters.size();
        }

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

struct LIBMVME_EXPORT InputType
{
    static const u32 Invalid = 0;
    static const u32 Array   = 1u << 0;
    static const u32 Value   = 1u << 1;
    static const u32 Both    = (Array | Value);
};

// The destination of a Pipe
struct LIBMVME_EXPORT Slot
{
    static const s32 NoParamIndex = -1; // special paramIndex value for InputType::Array

    Slot(OperatorInterface *parentOp, s32 parentSlotIndex, const QString &name, u32 acceptedInputs = InputType::Both)
        : acceptedInputTypes(acceptedInputs)
        , parentOperator(parentOp)
        , parentSlotIndex(parentSlotIndex)
        , name(name)
    {}

    /* Sets inputPipe to be the new input for this Slot. */
    void connectPipe(Pipe *inputPipe, s32 paramIndex);
    /* Clears this slots input. */
    void disconnectPipe();

    inline bool isConnected() const
    {
        return (inputPipe != nullptr);
    }

    inline bool isParamIndexInRange() const
    {
        return (isConnected() && (paramIndex < inputPipe->getSize()));
    }

    inline bool isArrayConnection() const
    {
        return paramIndex == NoParamIndex;
    }

    inline bool isParameterConnection() const
    {
        return !isArrayConnection();
    }

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

/* Data source interface. The analysis feeds complete module event data into
 * this using processModuleData(). */
class LIBMVME_EXPORT SourceInterface: public PipeSourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::PipeSourceInterface)
    public:
        using PipeSourceInterface::PipeSourceInterface;

        /* Use beginRun() to preallocate the outputs and setup internal state.
         * This will also be called by Analysis UI to be able to get array
         * sizes from operator output pipes! */
        virtual void beginRun(const RunInfo &runInfo) override {}

        /* Use beginEvent() to invalidate output parameters if needed. */
        virtual void beginEvent() {}

        virtual void read(const QJsonObject &json) = 0;
        virtual void write(QJsonObject &json) const = 0;

        virtual ~SourceInterface() {}
};

/* Operator interface. Consumes one or multiple input pipes and produces one or
 * multiple output pipes. */
class LIBMVME_EXPORT OperatorInterface: public PipeSourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::PipeSourceInterface)
    public:
        using PipeSourceInterface::PipeSourceInterface;

        /* Use beginRun() to preallocate the outputs and setup internal state. */
        virtual void beginRun(const RunInfo &runInfo) override {}

        virtual void step() = 0;

        virtual s32 getNumberOfSlots() const = 0;

        virtual Slot *getSlot(s32 slotIndex) = 0;

        virtual void read(const QJsonObject &json) = 0;
        virtual void write(QJsonObject &json) const = 0;

        /* If paramIndex is Slot::NoParamIndex the operator should use the whole array. */
        void connectInputSlot(s32 slotIndex, Pipe *inputPipe, s32 paramIndex);

        void connectArrayToInputSlot(s32 slotIndex, Pipe *inputPipe)
        { connectInputSlot(slotIndex, inputPipe, Slot::NoParamIndex); }

        s32 getMaximumInputRank();
        s32 getMaximumOutputRank();

        virtual void slotConnected(Slot *slot) {}
        virtual void slotDisconnected(Slot *slot) {}

        virtual bool hasVariableNumberOfSlots() const { return false; }
        virtual bool addSlot() { return false; }
        virtual bool removeLastSlot() { return false; }

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
class LIBMVME_EXPORT SinkInterface: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        using OperatorInterface::OperatorInterface;

        // PipeSourceInterface
        s32 getNumberOfOutputs() const override { return 0; }
        QString getOutputName(s32 outputIndex) const override { return QString(); }
        Pipe *getOutput(s32 index) override { return nullptr; }

        virtual size_t getStorageSize() const = 0;
};

typedef std::shared_ptr<SinkInterface> SinkPtr;

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
class LIBMVME_EXPORT Extractor: public SourceInterface
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

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void beginEvent() override;

        virtual s32 getNumberOfOutputs() const override;
        virtual QString getOutputName(s32 outputIndex) const override;
        virtual Pipe *getOutput(s32 index) override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Filter Extractor"); }
        virtual QString getShortName() const override { return QSL("FExt"); }

        QVector<double> getHitCounts() const;

        // configuration
        MultiWordDataFilter m_filter;
        a2::data_filter::MultiWordFilter m_fastFilter;
        u32 m_requiredCompletionCount = 1;
        u64 m_rngSeed;

        // state
        u32 m_currentCompletionCount = 0;

        // number of times the filter yielded data for each address
        QVector<double> m_hitCounts;
        mutable QMutex m_hitCountsMutex;

        pcg32_fast m_rng;
        Pipe m_output;
};

class LIBMVME_EXPORT ListFilterExtractor: public SourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SourceInterface)

    public:
        ListFilterExtractor(QObject *parent = nullptr);

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void beginEvent() override;

        virtual s32 getNumberOfOutputs() const override { return 1; }
        virtual QString getOutputName(s32 outputIndex) const override { return QSL("Combined and extracted data array"); }
        virtual Pipe *getOutput(s32 index) override { return index == 0 ? &m_output : nullptr; }

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("ListFilter Extractor"); }
        virtual QString getShortName() const override { return QSL("RExt"); }

        a2::ListFilterExtractor getExtractor() const { return m_a2Extractor; }
        void setExtractor(const a2::ListFilterExtractor &ex) { m_a2Extractor = ex; }
        u64 getRngSeed() const { return m_rngSeed; }
        void setRngSeed(u64 seed) { m_rngSeed = seed; }

    private:
        Pipe m_output;
        /* This only serves to hold data. It's not passed into the a2 system.
         * The members .rng and .moduleIndex are not set up as that information
         * is not available and not required when serializing this
         * ListFilterExtractor. */
        a2::ListFilterExtractor m_a2Extractor;
        u64 m_rngSeed;
};

using ListFilterExtractorPtr = std::shared_ptr<ListFilterExtractor>;

//
// Operators
//

/* An operator with one input slot and one output pipe. Only step() needs to be
 * implemented in subclasses. The input slot by default accepts both
 * InputType::Array and InputType::Value.  */
class LIBMVME_EXPORT BasicOperator: public OperatorInterface
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
class LIBMVME_EXPORT BasicSink: public SinkInterface
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

struct LIBMVME_EXPORT CalibrationMinMaxParameters
{
    CalibrationMinMaxParameters()
    {}

    CalibrationMinMaxParameters(double unitMin, double unitMax)
        : unitMin(unitMin)
        , unitMax(unitMax)
    {}

    bool isValid() const
    {
        return !(std::isnan(unitMin) || std::isnan(unitMax));
    }

    double unitMin = make_quiet_nan();
    double unitMax = make_quiet_nan();
};

class LIBMVME_EXPORT CalibrationMinMax: public BasicOperator
{
    Q_OBJECT
    public:
        CalibrationMinMax(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        void setCalibration(s32 address, const CalibrationMinMaxParameters &params);
        void setCalibration(s32 address, double unitMin, double unitMax)
        {
            setCalibration(address, CalibrationMinMaxParameters(unitMin, unitMax));
        }

        CalibrationMinMaxParameters getCalibration(s32 address) const;
        QVector<CalibrationMinMaxParameters> getCalibrations() const
        {
            return m_calibrations;
        }

        s32 getCalibrationCount() const
        {
            return m_calibrations.size();
        }

        QString getUnitLabel() const { return m_unit; }
        void setUnitLabel(const QString &label) { m_unit = label; }

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Calibration"); }
        virtual QString getShortName() const override { return QSL("Cal"); }

    private:
        QVector<CalibrationMinMaxParameters> m_calibrations;
        QString m_unit;

        // Obsolete but kept to be able to load old analysis files. Only read
        // from files, never written.
        double m_oldGlobalUnitMin = make_quiet_nan();
        double m_oldGlobalUnitMax = make_quiet_nan();
};

class LIBMVME_EXPORT IndexSelector: public BasicOperator
{
    Q_OBJECT
    public:
        IndexSelector(QObject *parent = 0);

        void setIndex(s32 index) { m_index = index; }
        s32 getIndex() const { return m_index; }

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Index Selector"); }
        virtual QString getShortName() const override { return QSL("Idx"); }

    private:
        s32 m_index = 0;
};

/* This operator has the value array from the previous cycle as its output.
 * Optionally if m_keepValid is set values from the array that where valid are
 * kept and not replaced by new invalid input values. */
class LIBMVME_EXPORT PreviousValue: public BasicOperator
{
    Q_OBJECT
    public:
        PreviousValue(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Previous Value"); }
        virtual QString getShortName() const override { return QSL("Prev"); }

        bool m_keepValid;

    private:
        ParameterVector m_previousInput;
};

class LIBMVME_EXPORT RetainValid: public BasicOperator
{
    Q_OBJECT
    public:
        RetainValid(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Retain Valid"); }
        virtual QString getShortName() const override { return QSL("Ret"); }

    private:
        ParameterVector m_lastValidInput;
};

class LIBMVME_EXPORT Difference: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        Difference(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo) override;
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

        virtual void slotConnected(Slot *slot) override;
        virtual void slotDisconnected(Slot *slot) override;

        virtual QString getDisplayName() const override { return QSL("Difference"); }
        virtual QString getShortName() const override { return QSL("Diff"); }

        Slot m_inputA;
        Slot m_inputB;
        Pipe m_output;
};

class LIBMVME_EXPORT Sum: public BasicOperator
{
    Q_OBJECT
    public:
        Sum(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override
        {
            if (m_calculateMean)
                return QSL("Mean");
            return QSL("Sum");
        }

        virtual QString getShortName() const override
        {
            if (m_calculateMean)
                return QSL("Mean");
            return QSL("Sum");
        }

        bool m_calculateMean = false;
};

class LIBMVME_EXPORT AggregateOps: public BasicOperator
{
    Q_OBJECT
    public:
        enum Operation
        {
            Op_Sum,
            Op_Mean,
            Op_Sigma,
            Op_Min,
            Op_Max,
            Op_Multiplicity,
            Op_MinX,
            Op_MaxX,
            Op_MeanX,
            Op_SigmaX,

            NumOps
        };

        static QString getOperationName(Operation op);

        AggregateOps(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override;
        virtual QString getShortName() const override;

        void setOperation(Operation op);
        Operation getOperation() const;

        /* Thresholds to check each input parameter against. If the parameter
         * is not inside [min_threshold, max_threshold] it is considered
         * invalid. If set to NaN the threshold is not used. */
        void setMinThreshold(double t);
        double getMinThreshold() const;
        void setMaxThreshold(double t);
        double getMaxThreshold() const;

        // The unit label to set on the output. If it's an empty string the
        // input unit label is passed through.
        void setOutputUnitLabel(const QString &label) { m_outputUnitLabel = label; }
        QString getOutputUnitLabel() const { return m_outputUnitLabel; }

    private:
        Operation m_op = Op_Sum;
        double m_minThreshold = make_quiet_nan();
        double m_maxThreshold = make_quiet_nan();
        QString m_outputUnitLabel;
};

/**
 * Map elements of one or more input arrays to an output array.
 *
 * Can be used to concatenate multiple arrays and/or change the order of array members.
 */
class LIBMVME_EXPORT ArrayMap: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        struct IndexPair
        {
            s32 slotIndex;
            s32 paramIndex;

            bool operator==(const IndexPair &o) const
            {
                return slotIndex == o.slotIndex && paramIndex == o.paramIndex;
            }
        };

        ArrayMap(QObject *parent = 0);

        virtual bool hasVariableNumberOfSlots() const override { return true; }
        virtual bool addSlot() override;
        virtual bool removeLastSlot() override;

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        // Inputs
        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        // Outputs
        virtual s32 getNumberOfOutputs() const override;
        virtual QString getOutputName(s32 outputIndex) const override;
        virtual Pipe *getOutput(s32 index) override;

        // Serialization
        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        // Info
        virtual QString getDisplayName() const override;
        virtual QString getShortName() const override;

        // Maps input slot and param indices to the output vector.
        QVector<IndexPair> m_mappings;

    private:
        // Using pointer to Slot here to avoid having to deal with changing
        // Slot addresses on resizing the inputs vector.
        QVector<std::shared_ptr<Slot>> m_inputs;
        Pipe m_output;
};

/**
 * Filters parameters based on a numeric inclusive range.
 *
 * Input parameters that do not fall inside the range are marked as invalid in
 * the output pipe. Other parameters are passed through as is.
 * If keepOutside is set, parameters that are outside the range will be kept,
 * others will be passed through.
 */
class LIBMVME_EXPORT RangeFilter1D: public BasicOperator
{
    Q_OBJECT
    public:
        RangeFilter1D(QObject *parent = 0);

        double m_minValue = make_quiet_nan(); // inclusive
        double m_maxValue = make_quiet_nan(); // exclusive
        bool m_keepOutside = false;

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("1D Range Filter"); }
        virtual QString getShortName() const override { return QSL("Range1D"); }
};

/**
 * Data filtering based on a condition input.
 *
 * An operator with two inputs: a data and a condition input.
 *
 * Data is only copied to the output if the corresponding condition input
 * parameter is valid.
 */
class LIBMVME_EXPORT ConditionFilter: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        ConditionFilter(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        // Inputs
        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        // Outputs
        virtual s32 getNumberOfOutputs() const override;
        virtual QString getOutputName(s32 outputIndex) const override;
        virtual Pipe *getOutput(s32 index) override;

        // Serialization
        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        // Info
        virtual QString getDisplayName() const override { return QSL("Condition Filter"); }
        virtual QString getShortName() const override { return QSL("CondFilt"); }

        Slot m_dataInput;
        Slot m_conditionInput;
        Pipe m_output;
};

class LIBMVME_EXPORT RectFilter2D: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        RectFilter2D(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        // Inputs
        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        // Outputs
        virtual s32 getNumberOfOutputs() const override;
        virtual QString getOutputName(s32 outputIndex) const override;
        virtual Pipe *getOutput(s32 index) override;

        // Serialization
        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        // Info
        virtual QString getDisplayName() const override { return QSL("2D Rect Filter"); }
        virtual QString getShortName() const override { return QSL("Rect2D"); }

        enum Op
        {
            OpAnd,
            OpOr
        };

        void setConditionOp(Op op) { m_op = op; }
        Op getConditionOp() const { return m_op; }

        void setXInterval(double x1, double x2) { setXInterval(QwtInterval(x1, x2)); }
        void setXInterval(const QwtInterval &interval)
        {
            m_xInterval = interval.normalized();
            m_xInterval.setBorderFlags(QwtInterval::ExcludeMaximum);
        }
        QwtInterval getXInterval() const { return m_xInterval; }

        void setYInterval(double y1, double y2) { setYInterval(QwtInterval(y1, y2)); }
        void setYInterval(const QwtInterval &interval)
        {
            m_yInterval = interval.normalized();
            m_yInterval.setBorderFlags(QwtInterval::ExcludeMaximum);
        }
        QwtInterval getYInterval() const { return m_yInterval; }


    private:
        Slot m_xInput;
        Slot m_yInput;
        Pipe m_output;

        QwtInterval m_xInterval;
        QwtInterval m_yInterval;
        Op m_op = OpAnd;
};

class LIBMVME_EXPORT BinarySumDiff: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        BinarySumDiff(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        // Inputs
        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        // Outputs
        virtual s32 getNumberOfOutputs() const override;
        virtual QString getOutputName(s32 outputIndex) const override;
        virtual Pipe *getOutput(s32 index) override;

        // Serialization
        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        // Info
        virtual QString getDisplayName() const override { return QSL("Binary Sum/Diff Equations"); }
        virtual QString getShortName() const override { return QSL("BinSumDiff"); }

        s32 getNumberOfEquations() const;
        QString getEquationDisplayString(s32 index) const;
        void setEquation(s32 index) { m_equationIndex = index; }
        s32 getEquation() const { return m_equationIndex; }

        void setOutputUnitLabel(const QString &label) { m_outputUnitLabel = label; }
        QString getOutputUnitLabel() const { return m_outputUnitLabel; }

        void setOutputLowerLimit(double limit) { m_outputLowerLimit = limit; }
        void setOutputUpperLimit(double limit) { m_outputUpperLimit = limit; }

        double getOutputLowerLimit() const { return m_outputLowerLimit; }
        double getOutputUpperLimit() const { return m_outputUpperLimit; }

    private:
        Slot m_inputA;
        Slot m_inputB;
        Pipe m_output;
        s32 m_equationIndex;
        QString m_outputUnitLabel;
        double m_outputLowerLimit;
        double m_outputUpperLimit;
};

//
// Sinks
//
class LIBMVME_EXPORT Histo1DSink: public BasicSink
{
    Q_OBJECT
    public:
        Histo1DSink(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("1D Histogram"); }
        virtual QString getShortName() const override { return QSL("H1D"); }

        virtual size_t getStorageSize() const override;

        std::shared_ptr<Histo1D> getHisto(s32 index) const
        {
            return m_histos.value(index, {});
        }

        s32 getNumberOfHistos() const { return m_histos.size(); }

        QVector<std::shared_ptr<Histo1D>> m_histos;
        s32 m_bins = 0;
        QString m_xAxisTitle;

        // Subrange limits
        double m_xLimitMin = make_quiet_nan();
        double m_xLimitMax = make_quiet_nan();

        bool hasActiveLimits() const
        {
            return !(std::isnan(m_xLimitMin) || std::isnan(m_xLimitMax));
        }

    private:
        u32 fillsSinceLastDebug = 0;
        std::shared_ptr<memory::Arena> m_histoArena;
};

class LIBMVME_EXPORT Histo2DSink: public SinkInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SinkInterface)
    public:
        Histo2DSink(QObject *parent = 0);

        // OperatorInterface
        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("2D Histogram"); }
        virtual QString getShortName() const override { return QSL("H2D"); }

        virtual size_t getStorageSize() const override;

        Slot m_inputX;
        Slot m_inputY;

        std::shared_ptr<Histo2D> m_histo;
        s32 m_xBins = 0;
        s32 m_yBins = 0;

        // For subrange selection. Makes it possible to get a high resolution
        // view of a rectangular part of the input without having to add a cut
        // operator. This might be temporary or stay in even after cuts are
        // properly implemented.
        double m_xLimitMin = make_quiet_nan();
        double m_xLimitMax = make_quiet_nan();
        double m_yLimitMin = make_quiet_nan();
        double m_yLimitMax = make_quiet_nan();

        bool hasActiveLimits(Qt::Axis axis) const
        {
            if (axis == Qt::XAxis)
                return !(std::isnan(m_xLimitMin) || std::isnan(m_xLimitMax));
            else
                return !(std::isnan(m_yLimitMin) || std::isnan(m_yLimitMax));
        }

        QString m_xAxisTitle;
        QString m_yAxisTitle;
};

class LIBMVME_EXPORT RateMonitorSink: public BasicSink
{
    Q_OBJECT
    public:
        using Type = a2::RateMonitorType;

        RateMonitorSink(QObject *parent = nullptr);

        virtual void beginRun(const RunInfo &runInfo) override;
        virtual void step() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Rate Monitor"); }

        virtual QString getShortName() const override
        {
            if (getType() == Type::FlowRate)
                return QSL("FlowRate");
            return QSL("Rate");
        }

        virtual size_t getStorageSize() const override;

        s32 rateSamplerCount() const { return m_samplers.size(); }
        QVector<a2::RateSamplerPtr> getRateSamplers() const { return m_samplers; }

        a2::RateSamplerPtr getRateSampler(s32 index) const
        {
            return m_samplers.value(index, {});
        }

        Type getType() const { return m_type; }
        void setType(Type type) { m_type = type; }

    private:
        QVector<a2::RateSamplerPtr> m_samplers;

        /* The desired size of rate history buffers. Analogous to the number of
         * bins for histograms.
         * Default is one day, meaning 86400 bins, which equals a hist
         * resolution of ~16.4 bits. */
        size_t m_rateHistoryCapacity = 3600 * 24;

        Type m_type = Type::CounterDifference;
};

/* Note: The qobject_cast()s in the createXXX() functions are there to ensure
 * that the types properly implement the declared interface. They need to have
 * the Q_INTERFACES() macro either directly in their declaration or inherit it
 * from a parent class.
 */

template<typename T>
SourceInterface *createSource()
{
    auto result = new T;
    Q_ASSERT(qobject_cast<SourceInterface *>(result));
    return result;
}

template<typename T>
OperatorInterface *createOperator()
{
    auto result = new T;
    Q_ASSERT(qobject_cast<OperatorInterface *>(result));
    return result;
}

template<typename T>
SinkInterface *createSink()
{
    auto result = new T;
    Q_ASSERT(qobject_cast<SinkInterface *>(result));
    return result;
}

class LIBMVME_EXPORT Registry
{
    public:
        template<typename T>
        bool registerSource(const QString &name)
        {
            if (m_sourceRegistry.contains(name))
                return false;

            m_sourceRegistry.insert(name, &createSource<T>);

#ifndef QT_NO_DEBUG
            // Force using the creator function to possibly trigger its assertion.
            delete makeSource(name);
#endif

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

#ifndef QT_NO_DEBUG
            // Force using the creator function to possibly trigger its assertion.
            delete makeOperator(name);
#endif

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

#ifndef QT_NO_DEBUG
            // Force using the creator function to possibly trigger its assertion.
            delete makeSink(name);
#endif

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

class LIBMVME_EXPORT Analysis: public QObject
{
    Q_OBJECT
    signals:
        void modified(bool);
        void modifiedChanged(bool);

    public:
        struct SourceEntry
        {
            QUuid eventId;
            QUuid moduleId;
            SourcePtr source;
        };

        struct OperatorEntry
        {
            QUuid eventId;
            OperatorPtr op;

            // A user defined level used for UI display structuring.
            s32 userLevel;
        };

        Analysis(QObject *parent = nullptr);
        virtual ~Analysis();

        /* Important: only the overload taking the hash of index pairs prepares
         * the a2 system!
         */
        void beginRun(const RunInfo &runInfo, const vme_analysis_common::VMEIdToIndex &vmeMap);
        void beginRun();
        void beginEvent(int eventIndex);
        void processModuleData(int eventIndex, int moduleIndex, u32 *data, u32 size);
        void endEvent(int eventIndex);
        // Called once for every SectionType_Timetick section
        void processTimetick();

        const QVector<SourceEntry> &getSources() const { return m_sources; }
        QVector<SourceEntry> &getSources() { return m_sources; }

        QVector<SourceEntry> getSources(const QUuid &eventId, const QUuid &moduleId) const
        {
            QVector<SourceEntry> result;

            for (const auto &e: m_sources)
            {
                if (e.eventId == eventId && e.moduleId == moduleId)
                {
                    result.push_back(e);
                }
            }

            return result;
        }

        void addSource(const QUuid &eventId, const QUuid &moduleId, const SourcePtr &source);
        void removeSource(const SourcePtr &source);
        void removeSource(SourceInterface *source);

        QVector<ListFilterExtractorPtr> getListFilterExtractors(ModuleConfig *module) const;
        void setListFilterExtractors(ModuleConfig *module, const QVector<ListFilterExtractorPtr> &extractors);

        const QVector<OperatorEntry> &getOperators() const { return m_operators; }
        QVector<OperatorEntry> &getOperators() { return m_operators; }

        const QVector<OperatorEntry> getOperators(const QUuid &eventId) const
        {
            QVector<OperatorEntry> result;

            for (const auto &e: m_operators)
            {
                if (e.eventId == eventId)
                {
                  result.push_back(e);
                }
            }

            return result;
        }

        const QVector<OperatorEntry> getOperators(const QUuid &eventId, s32 userLevel) const
        {
            QVector<OperatorEntry> result;

            for (const auto &e: m_operators)
            {
                if (e.eventId == eventId && e.userLevel == userLevel)
                {
                  result.push_back(e);
                }
            }

            return result;
        }

        OperatorEntry *getOperatorEntry(const QUuid &operatorId)
        {
            for (auto &entry: m_operators)
            {
                if (entry.op->getId() == operatorId)
                    return &entry;
            }

            return nullptr;
        }

        OperatorPtr getOperator(const QUuid &operatorId)
        {
            OperatorPtr result;

            auto entryPtr = getOperatorEntry(operatorId);
            if (entryPtr)
            {
                result = entryPtr->op;
            }
            return result;
        }

        void addOperator(const QUuid &eventId, const OperatorPtr &op, s32 userLevel);
        void removeOperator(const OperatorPtr &op);
        void removeOperator(OperatorInterface *op);

        void updateRanks();

        void clear();
        bool isEmpty() const;
        s32 getNumberOfSources() const { return m_sources.size(); }
        s32 getNumberOfOperators() const { return m_operators.size(); }
        s32 getNumberOfSinks() const;
        s32 getMaxUserLevel() const;
        s32 getMaxUserLevel(const QUuid &eventId) const;
        size_t getTotalSinkStorageSize() const;
        double getTimetickCount() const;

        Registry &getRegistry() { return m_registry; }

        enum ReadResultCodes
        {
            NoError = 0,
            VersionTooNew
        };

        using ReadResult = ReadResultBase<ReadResultCodes>;

        ReadResult read(const QJsonObject &json, VMEConfig *vmeConfig = nullptr);
        void write(QJsonObject &json) const;

        bool isModified() const { return m_modified; }
        void setModified(bool b = true);

        A2AdapterState *getA2AdapterState() { return m_a2State.get(); }

        RunInfo getRunInfo() const { return m_runInfo; }

    private:
        void updateRank(OperatorInterface *op, QSet<OperatorInterface *> &updated);

        QVector<SourceEntry> m_sources;
        QVector<OperatorEntry> m_operators;

        Registry m_registry;

        bool m_modified;
        RunInfo m_runInfo;
        double m_timetickCount;

        vme_analysis_common::VMEIdToIndex m_vmeMap;
        std::array<std::unique_ptr<memory::Arena>, 2> m_a2Arenas;
        u8 m_a2ArenaIndex;
        std::unique_ptr<memory::Arena> m_a2WorkArena;
        std::unique_ptr<A2AdapterState> m_a2State;
};

struct LIBMVME_EXPORT RawDataDisplay
{
    std::shared_ptr<Extractor> extractor;
    std::shared_ptr<Histo1DSink> rawHistoSink;
    std::shared_ptr<CalibrationMinMax> calibration;
    std::shared_ptr<Histo1DSink> calibratedHistoSink;
};

RawDataDisplay LIBMVME_EXPORT make_raw_data_display(std::shared_ptr<Extractor> extractor, double unitMin, double unitMax,
                                     const QString &xAxisTitle, const QString &unitLabel);

RawDataDisplay LIBMVME_EXPORT make_raw_data_display(const MultiWordDataFilter &extractionFilter, double unitMin, double unitMax,
                                     const QString &name, const QString &xAxisTitle, const QString &unitLabel);

void LIBMVME_EXPORT add_raw_data_display(Analysis *analysis, const QUuid &eventId, const QUuid &moduleId, const RawDataDisplay &display);

void LIBMVME_EXPORT do_beginRun_forward(PipeSourceInterface *pipeSource);

QString LIBMVME_EXPORT make_unique_operator_name(Analysis *analysis, const QString &prefix);

bool LIBMVME_EXPORT all_inputs_connected(OperatorInterface *op);
bool LIBMVME_EXPORT no_input_connected(OperatorInterface *op);

/** Generate new unique IDs for all sources and operators.
 * Note: Does not update the ModuleProperties information! */
void LIBMVME_EXPORT generate_new_object_ids(Analysis *analysis);

QString LIBMVME_EXPORT info_string(const Analysis *analysis);

void LIBMVME_EXPORT adjust_userlevel_forward(QVector<Analysis::OperatorEntry> &opEntries, OperatorInterface *op, s32 levelDelta);

} // end namespace analysis

#endif /* __ANALYSIS_H__ */
