/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include "analysis/a2/a2.h"
#include "analysis/a2/memory.h"
#include "analysis/a2/multiword_datafilter.h"
#include "analysis/analysis_fwd.h"
#include "analysis/condition_link.h"
#include "data_filter.h"
#include "histo1d.h"
#include "histo2d.h"
#include "libmvme_export.h"
#include "object_factory.h"
#include "typedefs.h"

#include "../globals.h"
#include "../rate_monitor_base.h"
#include "../vme_analysis_common.h"

#include <fstream>
#include <memory>
#include <pcg_random.hpp>
#include <QDir>
#include <QUuid>
#include <qwt_interval.h>

class QJsonObject;
class VMEConfig;

namespace memory
{
class Arena;
};

/*
 *   Operators vs Sources vs Sinks:
 *   - Data Sources have no input but are directly attached to a module.
 *     They have an eventId and a moduleId whereas operators are only associated with an
 *     event.
 *   - Data Sources take module data directly. After all module data has been passed to
 *     all relevant data sources the operators for that event are stepped.
 *   - Sinks usually don't have any output but consume input and accumulate it or write it
 *     to disk.
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
    using QVector<Parameter>::QVector;

    void invalidateAll()
    {
        for (auto &param: *this)
        {
            param.valid = false;
        }
    }

    // Note: name was not used at all but the introduction of the
    // ExpressionOperator might change that!
    QString name;
    QString unit;
};

using Logger = std::function<void (const QString &)>;

class OperatorInterface;
class Pipe;

/** System internal flags for analysis objects. */
namespace ObjectFlags
{
    using Flags = u32;

    static const Flags None            = 0u;
    /* Indicates that a beginRun() step is needed before the object can be used.  */
    static const Flags NeedsRebuild    = 1u << 0;
};

QString to_string(const ObjectFlags::Flags &flags);

class ObjectVisitor;

class LIBMVME_EXPORT AnalysisObject:
    public QObject,
    public std::enable_shared_from_this<AnalysisObject>
{
    Q_OBJECT
    public:
        explicit AnalysisObject(QObject *parent = nullptr)
            : QObject(parent)
            , m_id(QUuid::createUuid())
            , m_userLevel(0)
        { }

        QUuid getId() const { return m_id; }
        /* Note: setId() should only be used when restoring the object from a
         * config file. Otherwise just keep the id that's generated in the
         * constructor. */
        void setId(const QUuid &id) { m_id = id; }

        /* Object flags containing system internal information. */
        ObjectFlags::Flags getObjectFlags() const { return m_flags; }
        void setObjectFlags(ObjectFlags::Flags flags) { m_flags = flags; }
        void clearObjectFlags(ObjectFlags::Flags flagsToClear)
        {
            m_flags &= (~flagsToClear);
        }

        /* User defined level used for UI display structuring. */
        s32 getUserLevel() const { return m_userLevel; }
        void setUserLevel(s32 level);

        /* JSON serialization and cloning */
        virtual void read(const QJsonObject &json) = 0;
        virtual void write(QJsonObject &json) const = 0;
        std::unique_ptr<AnalysisObject> clone() const;

        /* The id of the VME event this object is a member of. */
        QUuid getEventId() const { return m_eventId; }
        void setEventId(const QUuid &id) { m_eventId = id; }

        /* Visitor support */
        virtual void accept(ObjectVisitor &visitor) = 0;

        void setAnalysis(std::shared_ptr<Analysis> analysis) { m_analysis = analysis; }
        std::shared_ptr<Analysis> getAnalysis() { return m_analysis.lock(); }
        std::shared_ptr<Analysis> getAnalysis() const { return m_analysis.lock(); }

    protected:
        /* Invoked by the clone() method on the cloned object. The source of the clone is
         * passed in cloneSource.
         * The purpose of this method is to pull any additional required information from
         * cloneSource and copy it to the clone.
         * Also steps like creating a new random seed have to be performed here. */
        virtual void postClone(const AnalysisObject *cloneSource) { (void) cloneSource; }

    private:
        ObjectFlags::Flags m_flags = ObjectFlags::None;
        QUuid m_id;
        s32 m_userLevel;
        QUuid m_eventId;
        mutable std::weak_ptr<Analysis> m_analysis;
};

/* Interface to indicate that something can the be source of a Pipe.
 * Base for data sources (objects consuming module data and producing output parameter
 * vectors) and for operators (objects consuming and producing parameter vectors.
 */
class LIBMVME_EXPORT PipeSourceInterface: public AnalysisObject
{
    Q_OBJECT
    public:
        explicit PipeSourceInterface(QObject *parent = 0)
            : AnalysisObject(parent)
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
        virtual bool hasVariableNumberOfOutputs() const { return false; }

        virtual QString getDisplayName() const = 0;
        virtual QString getShortName() const = 0;

        /* Use beginRun() to preallocate the outputs and setup internal state.
         * This will also be called by Analysis UI to be able to get array
         * sizes from operator output pipes! */
        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) = 0;
        virtual void endRun() {};

        virtual void clearState() {}

    private:
        PipeSourceInterface() = delete;
};

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
        Pipe();
        Pipe(PipeSourceInterface *sourceObject, s32 outputIndex,
             const QString &paramVectorName = QString());

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

        /* Removes the given slot from this pipes destinations.
         * IMPORTANT: Does not call disconnectPipe() on the slot!. */
        void removeDestination(Slot *dest)
        {
            destinations.removeAll(dest);
        }

        QVector<Slot *> getDestinations() const
        {
            return destinations;
        }

        /* Disconnects and removes all destination slots of this pipe. */
        void disconnectAllDestinationSlots();

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

    Slot(OperatorInterface *parentOp, s32 parentSlotIndex,
         const QString &name, u32 acceptedInputs = InputType::Both)
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
        if (!isConnected())
            return false;

        if (isParameterConnection())
        {
            return ((acceptedInputTypes & InputType::Value)
                    && 0 <= paramIndex && paramIndex < inputPipe->getSize());
        }
        else
        {
            return (acceptedInputTypes & InputType::Array);
        }
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

    /* The name of this Slot in the parentOperator. Set by the parentOperator. */
    QString name;

    /* Set to true if it's ok for the slot to be unconnected and still consider
     * the parent operator to be in a valid state. By default all slots of an
     * operator need to be connected. */
    bool isOptional = false;
};

/* Data source interface. */
class LIBMVME_EXPORT SourceInterface: public PipeSourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::PipeSourceInterface)
    public:
        using PipeSourceInterface::PipeSourceInterface;

        virtual ~SourceInterface() {}

        /** The id of the VME module this object is attached to. Only relevant for data
         * sources as these are directly attached to modules. */
        QUuid getModuleId() const { return m_moduleId; }
        void setModuleId(const QUuid &id) { m_moduleId = id; }

        virtual void accept(ObjectVisitor &visitor) override;

    protected:
        virtual void postClone(const AnalysisObject *cloneSource) override;

    private:
        QUuid m_moduleId;
};

/* Operator interface. Consumes one or multiple input pipes and produces one or
 * multiple output pipes. */
class LIBMVME_EXPORT OperatorInterface: public PipeSourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::PipeSourceInterface)
    public:
        using PipeSourceInterface::PipeSourceInterface;

        virtual s32 getNumberOfSlots() const = 0;

        virtual Slot *getSlot(s32 slotIndex) = 0;

        QVector<Slot *> getSlots();

        /* If paramIndex is Slot::NoParamIndex the operator should use the whole array. */
        void connectInputSlot(s32 slotIndex, Pipe *inputPipe, s32 paramIndex);

        void connectArrayToInputSlot(s32 slotIndex, Pipe *inputPipe)
        { connectInputSlot(slotIndex, inputPipe, Slot::NoParamIndex); }

        s32 getMaximumInputRank();
        s32 getMaximumOutputRank();

        void setRank(s32 rank) { m_rank = rank; }
        s32 getRank() const { return m_rank; }

        virtual void slotConnected(Slot *slot) { (void) slot; }
        virtual void slotDisconnected(Slot *slot) { (void) slot; }

        virtual bool hasVariableNumberOfSlots() const { return false; }
        virtual bool addSlot() { return false; }
        virtual bool removeLastSlot() { return false; }

        virtual void accept(ObjectVisitor &visitor) override;

    private:
        s32 m_rank = 0;
};

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
        QString getOutputName(s32 outputIndex) const override { (void) outputIndex; return QString(); }
        Pipe *getOutput(s32 index) override { (void) index; return nullptr; }

        virtual size_t getStorageSize() const = 0;

        /* Enable/disable functionality for Sinks only. In the future this can
         * be moved into PipeSourceInterface so that it's available for
         * Operators aswell as Sinks but disabling operators needs additional
         * work as dependencies have to be managed. */
        void setEnabled(bool b) { m_enabled = b; }
        bool isEnabled() const  { return m_enabled; }

        virtual void accept(ObjectVisitor &visitor) override;

    protected:
        virtual void postClone(const AnalysisObject *cloneSource) override;

    private:
        bool m_enabled = true;
};

class LIBMVME_EXPORT ConditionInterface: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface);
    public:
        using OperatorInterface::OperatorInterface;

        // PipeSourceInterface
        s32 getNumberOfOutputs() const override { return 0; }
        QString getOutputName(s32 outputIndex) const override { (void) outputIndex; return QString(); }
        Pipe *getOutput(s32 index) override { (void) index; return nullptr; }

        virtual s32 getNumberOfBits() const = 0;
};


enum class DisplayLocation
{
    Any,
    Operator,
    Sink
};

QString to_string(const DisplayLocation &loc);
DisplayLocation displayLocation_from_string(const QString &str);

/** Contains a list of analysis object ids.
 *
 * IMPORTANT: The list of members of a directory is stored as a vector of object ids.
 * Creating a clone of this directory will clone the memberlist and result in all member
 * objects being part of this directory and the cloned one.
 *
 * To keep the analysis in a consistent state the member id list has to be regenerated
 * using the object ids of the newly created clones of member objects. This is done in
 * generate_new_object_ids().
 */
class LIBMVME_EXPORT Directory: public AnalysisObject
{
    Q_OBJECT
    public:
        using MemberContainer = QVector<QUuid>;
        using iterator        = MemberContainer::iterator;
        using const_iterator  = MemberContainer::const_iterator;
        using MemberSet       = QSet<QUuid>;

        Q_INVOKABLE explicit Directory(QObject *parent = nullptr);

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        MemberContainer getMembers() const { return m_members; }
        void setMembers(const MemberContainer &members) { m_members = members; }
        MemberSet getMemberSet() const;

        void push_back(const AnalysisObjectPtr &obj)
        {
            obj->setUserLevel(getUserLevel());
            m_members.push_back(obj->getId());
        }

        void push_back(AnalysisObjectPtr &&obj)
        {
            obj->setUserLevel(getUserLevel());
            m_members.push_back(obj->getId());
        }

        const_iterator begin() const { return m_members.begin(); }
        const_iterator end() const { return m_members.end(); }
        iterator begin() { return m_members.begin(); }
        iterator end() { return m_members.end(); }

        int indexOf(const AnalysisObjectPtr &obj, int from = 0) const
        {
            return m_members.indexOf(obj->getId(), from);
        }

        int indexOf(const QUuid &id, int from = 0) const
        {
            return m_members.indexOf(id, from);
        }

        bool contains(const AnalysisObjectPtr &obj) const { return m_members.contains(obj->getId()); }
        bool contains(const QUuid &id) const { return m_members.contains(id); }
        int size() const { return m_members.size(); }

        DisplayLocation getDisplayLocation() const { return m_displayLocation; }
        void setDisplayLocation(const DisplayLocation &loc)
        {
            m_displayLocation = loc;
        }

        void remove(int index) { m_members.removeAt(index); }
        void remove(const AnalysisObjectPtr &obj) { remove(obj->getId()); }
        void remove(const QUuid &id) { m_members.removeAll(id); }

        virtual void accept(ObjectVisitor &visitor) override;

    protected:
        // Empties the newly cloned directory. This is to avoid objects having multiple
        // parent directories after the parent was cloned.
        virtual void postClone(const AnalysisObject *cloneSource) override;

    private:
        MemberContainer m_members;
        DisplayLocation m_displayLocation;
};

bool check_directory_consistency(const DirectoryVector &dirs,
                                 const Analysis *analysis = nullptr);

} // end namespace analysis

#define SinkInterface_iid "com.mesytec.mvme.analysis.SinkInterface.1"
Q_DECLARE_INTERFACE(analysis::SinkInterface, SinkInterface_iid);

#define ConditionInterface_iid "com.mesytec.mvme.analysis.ConditionInterface.1"
Q_DECLARE_INTERFACE(analysis::ConditionInterface, ConditionInterface_iid);

namespace analysis
{

//
// Sources
//

/* A Source using a MultiWordDataFilter for data extraction. Additionally
 * requiredCompletionCount can be set to only produce output for the nth
 * match (in the current event). */
class LIBMVME_EXPORT Extractor: public SourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SourceInterface)

    public:
        Q_INVOKABLE Extractor(QObject *parent = 0);

        const MultiWordDataFilter &getFilter() const { return m_filter; }
        MultiWordDataFilter &getFilter() { return m_filter; }
        void setFilter(const MultiWordDataFilter &filter) { m_filter = filter; }

        u32 getRequiredCompletionCount() const { return m_requiredCompletionCount; }
        void setRequiredCompletionCount(u32 count) { m_requiredCompletionCount = count; }

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

        virtual s32 getNumberOfOutputs() const override;
        virtual QString getOutputName(s32 outputIndex) const override;
        virtual Pipe *getOutput(s32 index) override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("Filter Extractor"); }
        virtual QString getShortName() const override { return QSL("FExt"); }

        void setParameterNames(const QStringList &names) { m_parameterNames = names; }
        QStringList getParameterNames() const { return m_parameterNames; }
        bool setParameterName(int paramIndex, const QString &name);

        using Options = a2::DataSourceOptions;
        Options::opt_t getOptions() const { return m_options; }
        void setOptions(Options::opt_t options) { m_options = options; }

        // configuration
        // FIXME: make private
        MultiWordDataFilter m_filter;
        a2::data_filter::MultiWordFilter m_fastFilter;
        u32 m_requiredCompletionCount = 1;
        u64 m_rngSeed;

        Pipe m_output;
        Options::opt_t m_options;

    protected:
        virtual void postClone(const AnalysisObject *cloneSource) override;

    private:
        QStringList m_parameterNames;
};

class LIBMVME_EXPORT ListFilterExtractor: public SourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SourceInterface)

    public:
        Q_INVOKABLE ListFilterExtractor(QObject *parent = nullptr);

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

        virtual s32 getNumberOfOutputs() const override { return 1; }
        virtual QString getOutputName(s32 outputIndex) const override { (void) outputIndex; return QSL("Combined and extracted data array"); }
        virtual Pipe *getOutput(s32 index) override { return index == 0 ? &m_output : nullptr; }

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("ListFilter Extractor"); }
        virtual QString getShortName() const override { return QSL("RExt"); }

        a2::ListFilterExtractor getExtractor() const { return m_a2Extractor; }
        void setExtractor(const a2::ListFilterExtractor &ex) { m_a2Extractor = ex; }
        u64 getRngSeed() const { return m_rngSeed; }
        void setRngSeed(u64 seed) { m_rngSeed = seed; }
        u32 getAddressBits() const;
        u32 getDataBits() const;

        void setParameterNames(const QStringList &names) { m_parameterNames = names; }
        QStringList getParameterNames() const { return m_parameterNames; }
        bool setParameterName(int paramIndex, const QString &name);

        using Options = a2::DataSourceOptions;
        Options::opt_t getOptions() const { return m_a2Extractor.options; }
        void setOptions(Options::opt_t options) { m_a2Extractor.options = options; }

    protected:
        virtual void postClone(const AnalysisObject *cloneSource) override;

    private:
        Pipe m_output;
        /* This only serves to hold data. It's not passed into the a2 system.
         * The members .rng and .moduleIndex are not set up as that information
         * is not available and not required when serializing this
         * ListFilterExtractor. */
        a2::ListFilterExtractor m_a2Extractor;
        u64 m_rngSeed;
        QStringList m_parameterNames;
};

class LIBMVME_EXPORT MultiHitExtractor: public SourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SourceInterface)

    public:
        using Shape = a2::MultiHitExtractor::Shape;

        Q_INVOKABLE MultiHitExtractor(QObject *parent = nullptr);

        void setFilter(const DataFilter &f) { m_ex.filter = f; }
        DataFilter getFilter() const { return m_ex.filter; }

        void setMaxHits(u16 maxHits) { m_ex.maxHits = maxHits; }
        u16 getMaxHits() const { return m_ex.maxHits; }

        void setShape(Shape shape) { m_ex.shape = shape; }
        Shape getShape() const { return m_ex.shape; }

        using Options = a2::DataSourceOptions;
        Options::opt_t getOptions() const { return m_ex.options; }
        void setOptions(Options::opt_t options) { m_ex.options = options; }

        QString getDisplayName() const override;
        QString getShortName() const override;

        s32 getNumberOfOutputs() const override;
        QString getOutputName(s32 index) const override;
        Pipe *getOutput(s32 index) override;
        void beginRun(const RunInfo &runInfo, Logger logger = {}) override;
        void read(const QJsonObject &json) override;
        void write(QJsonObject &json) const override;

    protected:
        virtual void postClone(const AnalysisObject *cloneSource) override;

    private:
        // a1 layer output pipes
        std::vector<Pipe> m_outputs;

        // a2 MultiHitExtractor struct for data storage.
        a2::MultiHitExtractor m_ex;

        u64 m_rngSeed;

#if 0
        Shape m_shape = Shape::ArrayPerHit;
        DataFilter m_filter = {};
        u16 m_maxHits = 1u;
        Options::opt_t m_options = {}; // TODO: use the options
#endif
};

using ListFilterExtractorPtr = std::shared_ptr<ListFilterExtractor>;
using ListFilterExtractorVector = QVector<ListFilterExtractorPtr>;

class LIBMVME_EXPORT DataSourceCopy: public SourceInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SourceInterface)

    public:
        Q_INVOKABLE DataSourceCopy(QObject *parent = nullptr);

        virtual s32 getNumberOfOutputs() const override { return 1; }
        virtual QString getOutputName(s32 index) const override
        { assert(index == 0); return QSL("Output"); }
        virtual Pipe *getOutput(s32 index) override { assert(index == 0); return &m_output; }
        virtual QString getDisplayName() const override { return QSL("DataSourceCopy"); }
        virtual QString getShortName() const override { return QSL("DSC"); }
        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;
        virtual void read(const QJsonObject &/*json*/) override {};
        virtual void write(QJsonObject &/*json*/) const override {};

    private:
        Pipe m_output;
};

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
        explicit BasicOperator(QObject *parent = 0);
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
        explicit BasicSink(QObject *parent = 0);
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
        explicit Q_INVOKABLE CalibrationMinMax(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

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
        explicit Q_INVOKABLE IndexSelector(QObject *parent = 0);

        void setIndex(s32 index) { m_index = index; }
        s32 getIndex() const { return m_index; }

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

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
        explicit Q_INVOKABLE PreviousValue(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

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
        explicit Q_INVOKABLE RetainValid(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

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
        Q_INVOKABLE Difference(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

        // PipeSourceInterface
        s32 getNumberOfOutputs() const override { return 1; }
        QString getOutputName(s32 outputIndex) const override { (void) outputIndex; return QSL("difference"); }
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

        void slotConnected(Slot *slot) override;
        void slotDisconnected(Slot *slot) override;

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
        explicit Q_INVOKABLE Sum(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

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

        explicit Q_INVOKABLE AggregateOps(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

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

        explicit Q_INVOKABLE ArrayMap(QObject *parent = 0);

        virtual bool hasVariableNumberOfSlots() const override { return true; }
        virtual bool addSlot() override;
        virtual bool removeLastSlot() override;

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

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
        explicit Q_INVOKABLE RangeFilter1D(QObject *parent = 0);

        double m_minValue = make_quiet_nan(); // inclusive
        double m_maxValue = make_quiet_nan(); // exclusive
        bool m_keepOutside = false;

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

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
 *
 * If the condition input is a single value it is applied to all elements of the data
 * input.
 */
class LIBMVME_EXPORT ConditionFilter: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        Q_INVOKABLE ConditionFilter(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

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
        bool m_invertedCondition;
};

class LIBMVME_EXPORT RectFilter2D: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        Q_INVOKABLE RectFilter2D(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

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
        Q_INVOKABLE BinarySumDiff(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

        // Inputs
        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        void slotConnected(Slot *slot) override;
        void slotDisconnected(Slot *slot) override;

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

class LIBMVME_EXPORT ExpressionOperator: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        Q_INVOKABLE ExpressionOperator(QObject *parent = 0);

        // Init and execute
        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

        // Inputs
        virtual bool hasVariableNumberOfSlots() const override { return true; }
        virtual bool addSlot() override;
        virtual bool removeLastSlot() override;

        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        // Outputs
        virtual bool hasVariableNumberOfOutputs() const override { return true; }
        virtual s32 getNumberOfOutputs() const override;
        virtual QString getOutputName(s32 outputIndex) const override;
        virtual Pipe *getOutput(s32 index) override;

        // Serialization
        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        // Info
        virtual QString getDisplayName() const override { return QSL("Expression"); }
        virtual QString getShortName() const override { return QSL("Expr"); }

        // Clone
        ExpressionOperator *cloneViaSerialization() const;

        // ExpressionOperator specific
        void setBeginExpression(const QString &str) { m_exprBegin = str; }
        QString getBeginExpression() const { return m_exprBegin; }

        void setStepExpression(const QString &str) { m_exprStep = str; }
        QString getStepExpression() const { return m_exprStep; }

        /* Variable name prefixes for each of the operators inputs. These
         * prefixes define the exprtk variable names used in both the begin and
         * step expressions. */
        QString getInputPrefix(s32 inputIndex) const { return m_inputPrefixes.value(inputIndex); }
        QStringList getInputPrefixes() const { return m_inputPrefixes; }
        void setInputPrefixes(const QStringList &prefixes) { m_inputPrefixes = prefixes; }

        a2::Operator buildA2Operator(memory::Arena *arena);
        a2::Operator buildA2Operator(memory::Arena *arena,
                                     a2::ExpressionOperatorBuildOptions buildOptions);

    private:
        QString m_exprBegin;
        QString m_exprStep;
        QStringList m_inputPrefixes;

        QVector<std::shared_ptr<Slot>> m_inputs;
        QVector<std::shared_ptr<Pipe>> m_outputs;
};

class LIBMVME_EXPORT ScalerOverflow: public OperatorInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::OperatorInterface)
    public:
        Q_INVOKABLE ScalerOverflow(QObject *parent = 0);

        // Init and execute
        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

        // Inputs
        virtual bool hasVariableNumberOfSlots() const override { return false; }
        virtual bool addSlot() override { return false; }
        virtual bool removeLastSlot() override { return false; }

        virtual s32 getNumberOfSlots() const override { return 1; }
        virtual Slot *getSlot(s32 slotIndex) override
        {
            if (slotIndex == 0)
                return &m_input;
            return nullptr;
        }

        // Outputs
        virtual bool hasVariableNumberOfOutputs() const override { return false; }
        virtual s32 getNumberOfOutputs() const override { return 2; };
        virtual QString getOutputName(s32 outputIndex) const override
        {
            if (outputIndex == 0)
                return QSL("value");
            if (outputIndex == 1)
                return QSL("overflows");
            return {};
        }

        virtual Pipe *getOutput(s32 outputIndex) override
        {
            if (outputIndex == 0)
                return &m_valueOutput;
            if (outputIndex == 1)
                return &m_overflowCountOutput;
            return nullptr;
        }

        // Serialization
        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        // Info
        virtual QString getDisplayName() const override { return QSL("Scaler Overflow"); }
        virtual QString getShortName() const override { return QSL("ScalerOverflow"); }

    private:
        Slot m_input;
        Pipe m_valueOutput;
        Pipe m_overflowCountOutput;
};

//
// Conditions
//

class LIBMVME_EXPORT ConditionInterval: public ConditionInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::ConditionInterface)
    public:
        Q_INVOKABLE ConditionInterval(QObject *parent = 0);

        virtual QString getDisplayName() const override { return QSL("Interval Condition"); }
        virtual QString getShortName() const override { return QSL("CondInter"); }

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

        void setIntervals(const QVector<QwtInterval> &intervals);
        QVector<QwtInterval> getIntervals() const;

        void setInterval(s32 address, const QwtInterval &interval);
        QwtInterval getInterval(s32 address) const;

        virtual s32 getNumberOfBits() const override;

    private:
        Slot m_input;
        QVector<QwtInterval> m_intervals;
};

class LIBMVME_EXPORT ConditionRectangle: public ConditionInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::ConditionInterface)
    public:
        Q_INVOKABLE ConditionRectangle(QObject *parent = 0);

        virtual QString getDisplayName() const override { return QSL("Rectangle Condition"); }
        virtual QString getShortName() const override { return QSL("CondRect"); }

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

        void setRectangle(const QRectF &rect);
        QRectF getRectangle() const;

        virtual s32 getNumberOfBits() const override { return 1; }

    private:
        Slot m_inputX;
        Slot m_inputY;
        QRectF m_rectangle;
};

class LIBMVME_EXPORT ConditionPolygon: public ConditionInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::ConditionInterface)
    public:
        Q_INVOKABLE ConditionPolygon(QObject *parent = 0);

        virtual QString getDisplayName() const override { return QSL("Polygon Condition"); }
        virtual QString getShortName() const override { return QSL("CondPoly"); }

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

        void setPolygon(const QPolygonF &polygon);
        QPolygonF getPolygon() const;

        virtual s32 getNumberOfBits() const override { return 1; }

    private:
        Slot m_inputX;
        Slot m_inputY;
        QPolygonF m_polygon;
};

//
// Sinks
//
class LIBMVME_EXPORT Histo1DSink: public BasicSink
{
    Q_OBJECT
    public:
        explicit Q_INVOKABLE Histo1DSink(QObject *parent = 0);

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;
        virtual void clearState() override;

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
        s32 getHistoBins() const { return m_bins; }

        QVector<std::shared_ptr<Histo1D>> getHistos() { return m_histos; }

        // FIXME: move to private vars
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

        void setResolutionReductionFactor(u32 rrf) { m_rrf = rrf; }
        u32 getResolutionReductionFactor() const { return m_rrf; }

    private:
        u32 fillsSinceLastDebug = 0;
        std::shared_ptr<memory::Arena> m_histoArena;
        u32 m_rrf;
};

class LIBMVME_EXPORT Histo2DSink: public SinkInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SinkInterface)
    public:
        Q_INVOKABLE Histo2DSink(QObject *parent = 0);

        // OperatorInterface
        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;
        virtual void clearState() override;

        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        virtual QString getDisplayName() const override { return QSL("2D Histogram"); }
        virtual QString getShortName() const override { return QSL("H2D"); }

        virtual size_t getStorageSize() const override;

        Slot m_inputX;
        Slot m_inputY;

        Histo2DPtr getHisto() const { return m_histo; }

        std::shared_ptr<Histo2D> m_histo; // FIXME: move to private
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

        void setResolutionReductionFactors(const ResolutionReductionFactors &rrf)
        {
            m_rrf = rrf;
        }

        void setResolutionReductionFactors(u32 rrfX, u32 rrfY)
        {
            m_rrf = { rrfX, rrfY };
        }

        ResolutionReductionFactors getResolutionReductionFactors() const
        {
            return m_rrf;
        }

        s32 getHistoBinsX() const { return m_xBins; }
        s32 getHistoBinsY() const { return m_yBins; }

    private:
        ResolutionReductionFactors m_rrf;
};

class LIBMVME_EXPORT RateMonitorSink: public SinkInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SinkInterface)
    public:
        using Type = a2::RateMonitorType;

        explicit Q_INVOKABLE RateMonitorSink(QObject *parent = nullptr);

        virtual bool hasVariableNumberOfSlots() const override { return true; }
        virtual bool addSlot() override;
        virtual bool removeLastSlot() override;

        // Inputs
        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;
        virtual void clearState() override;

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

        QVector<s32> getSamplerToInputMapping() const { return m_samplerInputMapping; }

        s32 getInputIndexForSamplerIndex(s32 samplerIndex) const {
            return m_samplerInputMapping.value(samplerIndex, -1);
        }

        s32 getSamplerStartOffset(s32 inputIndex) const {
            return m_inputSamplerOffsets.value(inputIndex, -1);
        };

        Type getType() const { return m_type; }
        void setType(Type type) { m_type = type; }

        size_t getRateHistoryCapacity() const { return m_rateHistoryCapacity; }
        void setRateHistoryCapacity(size_t capacity) { m_rateHistoryCapacity = capacity; }

        QString getUnitLabel() const { return m_unitLabel; }
        void setUnitLabel(const QString &label) { m_unitLabel = label; }

        double getCalibrationFactor() const { return m_calibrationFactor; }
        void setCalibrationFactor(double d) { m_calibrationFactor = d; }

        double getCalibrationOffset() const { return m_calibrationOffset; }
        void setCalibrationOffset(double d) { m_calibrationOffset = d; }

        double getSamplingInterval() const { return m_samplingInterval; }
        void setSamplingInterval(double d) { m_samplingInterval = d; }

        RateMonitorXScaleType getXScaleType() const { return m_xScaleType; }
        void setXScaleType(RateMonitorXScaleType scaleType) { m_xScaleType = scaleType; }

        // If true the plot widget should show all rates in a single plot.
        // Saved in the analysis to persist across sessions.
        void setUseCombinedView(bool b) { m_useCombinedView = b; }
        bool getUseCombinedView() const { return m_useCombinedView; }

    private:
        QVector<std::shared_ptr<Slot>> m_inputs;
        QVector<a2::RateSamplerPtr> m_samplers;

        // [samplerIndex] -> inputIndex; size == m_samplers.size()
        QVector<s32> m_samplerInputMapping;

        // [inputIndex] -> start offset into samplers; size == m_inputs.size()
        // The number of samplers for each input is known by looking at the inputs
        // connection type and size.
        QVector<s32> m_inputSamplerOffsets;

        /* The desired size of rate history buffers. Analogous to the number of
         * bins for histograms.
         * Default is one day, meaning 86400 bins, which equals a hist
         * resolution of ~16.4 bits. */
        size_t m_rateHistoryCapacity = 3600 * 24;

        Type m_type = Type::FlowRate;
        QString m_unitLabel;
        double m_calibrationFactor = 1.0;
        double m_calibrationOffset = 0.0;
        double m_samplingInterval  = 1.0;
        bool m_useCombinedView = false;
        RateMonitorXScaleType m_xScaleType;
};

class LIBMVME_EXPORT ExportSink: public SinkInterface
{
    Q_OBJECT
    Q_INTERFACES(analysis::SinkInterface)

    public:
        Q_INVOKABLE ExportSink(QObject *parent = nullptr);

        virtual bool hasVariableNumberOfSlots() const override { return true; }
        virtual bool addSlot() override;
        virtual bool removeLastSlot() override;

        virtual void beginRun(const RunInfo &runInfo, Logger logger = {}) override;

        // Inputs
        virtual s32 getNumberOfSlots() const override;
        virtual Slot *getSlot(s32 slotIndex) override;

        // Serialization
        virtual void read(const QJsonObject &json) override;
        virtual void write(QJsonObject &json) const override;

        // Info
        virtual QString getDisplayName() const override { return QSL("File Export"); }
        virtual QString getShortName() const override { return QSL("Export"); }

        // SinkInterface specific. This operator doesn't use storage like
        // histograms do, so it always returns 0 here.
        virtual size_t getStorageSize() const override { return 0u; }

        void setCompressionLevel(int level) { m_compressionLevel = level; }
        int getCompressionLevel() const { return m_compressionLevel; }

        using Format = a2::ExportSinkFormat;

        void setFormat(Format fmt) { m_format = fmt; }
        Format getFormat() const { return m_format; }

        void setOutputPrefixPath(const QString &prefixPath) { m_outputPrefixPath = prefixPath; }
        QString getOutputPrefixPath() const { return m_outputPrefixPath; } // exports/sums_and_coords

        QString getDataFilePath(const RunInfo &runInfo) const; // exports/sums_and_coords/data_<runInfo.runid>.bin.gz
        QString getDataFileName(const RunInfo &runInfo) const; // data_<runInfo.runId>.bin.gz
        QString getExportFileBasename() const;  // sums_and_coords
        QString getDataFileExtension() const;   // .bin.gz / .bin

        QVector<std::shared_ptr<Slot>> getDataInputs() const { return m_dataInputs; }

        QStringList getOutputFilenames();

        void generateCode(Logger logger);

    protected:
        virtual void postClone(const AnalysisObject *cloneSource) override;

    private:
        // Optional single value condition input. If invalid no data will be
        // exported in that event cycle. If unconnected all occurrences of the
        // event will produce exported data.
        Slot m_conditionInput;

        // Data inputs to be exported
        QVector<std::shared_ptr<Slot>> m_dataInputs;

        // Output prefix path.
        // Is relative to the application working directory which usually is
        // the current workspace directory.
        // Subdirectories will be created as needed to generate the output
        // files inside the prefix path.
        QString m_outputPrefixPath;

        //  0:  turn of compression; makes this operator write directly to the output file
        // -1:  Z_DEFAULT_COMPRESSION
        //  1:  Z_BEST_SPEED
        //  9:  Z_BEST_COMPRESSION
        int m_compressionLevel = 1;

        Format m_format = Format::Sparse;
};

struct AnalysisObjectStore;

using ConditionLinks = QHash<OperatorPtr, ConditionLink>;

enum class AnalysisReadResult
{
    NoError,
    VersionTooOld, // must be migrated using convert_to_current_version()
    VersionTooNew,
};

LIBMVME_EXPORT std::error_code make_error_code(AnalysisReadResult r);

} // end namespace analysis

namespace std
{
    template<> struct is_error_code_enum<analysis::AnalysisReadResult>: true_type {};
} // end namespace std

namespace analysis
{

class LIBMVME_EXPORT Analysis:
    public QObject,
    public std::enable_shared_from_this<Analysis>
{
    Q_OBJECT
    signals:
        void modified(bool);
        void modifiedChanged(bool);

        void dataSourceAdded(const SourcePtr &src);
        void dataSourceRemoved(const SourcePtr &src);
        void dataSourceEdited(const SourcePtr &src);

        void operatorAdded(const OperatorPtr &op);
        void operatorRemoved(const OperatorPtr &op);
        void operatorEdited(const OperatorPtr &op);

        void directoryAdded(const DirectoryPtr &ptr);
        void directoryRemoved(const DirectoryPtr &ptr);

        void conditionLinkApplied(const OperatorPtr &op, const ConditionLink &cl);
        void conditionLinkCleared(const OperatorPtr &op, const ConditionLink &cl);


    public:
        explicit Analysis(QObject *parent = nullptr);
        virtual ~Analysis();

        //
        // Data Sources
        //
        const SourceVector &getSources() const { return m_sources; }
        SourceVector &getSources() { return m_sources; }
        SourceVector getSources(const QUuid &eventId, const QUuid &moduleId) const;
        SourceVector getSourcesByModule(const QUuid &moduleId) const;
        SourceVector getSourcesByEvent(const QUuid &eventId) const;
        SourcePtr getSource(const QUuid &sourceId) const;

        void addSource(const SourcePtr &source);
        void removeSource(const SourcePtr &source);
        void removeSource(SourceInterface *source);
        void setSourceEdited(const SourcePtr &source);

        s32 getNumberOfSources() const { return m_sources.size(); }

        // Special handling for listfilter extractors as they only make sense when grouped
        // up as each consumes a certain amount of input words and the next filter
        // continues with the remaining input data.

        /** Returns the ListFilterExtractors attached to the module with the given id. */
        ListFilterExtractorVector getListFilterExtractors(const QUuid &eventId,
                                                          const QUuid &moduleId) const;

        /** Replaces the ListFilterExtractors for the module identified by the given
         * moduleid with the given extractors. */
        void setListFilterExtractors(const QUuid &eventId, const QUuid &moduleId,
                                     const ListFilterExtractorVector &extractors);

        //
        // Operators
        //
        const OperatorVector &getOperators() const { return m_operators; }
        OperatorVector &getOperators() { return m_operators; }
        OperatorVector getOperators(const QUuid &eventId) const;
        OperatorVector getOperators(const QUuid &eventId, s32 userLevel) const;
        OperatorVector getOperators(s32 userLevel) const;
        OperatorPtr getOperator(const QUuid &operatorId) const;
        OperatorVector getNonSinkOperators() const;
        OperatorVector getSinkOperators() const;

        template <typename T>
        QVector<T> getSinkOperators() const
        {
            QVector<T> result;

            for (const auto &op: m_operators)
            {
                if (qobject_cast<SinkInterface *>(op.get()))
                {
                    result.push_back(std::dynamic_pointer_cast<typename T::element_type>(op));
                }
            }

            return result;
        }

        void addOperator(const QUuid &eventId, s32 userLevel, const OperatorPtr &op);
        void addOperator(const OperatorPtr &op);
        void removeOperator(const OperatorPtr &op);
        void removeOperator(OperatorInterface *op);
        void setOperatorEdited(const OperatorPtr &op);

        s32 getNumberOfOperators() const { return m_operators.size(); }

        //
        // Conditions
        //
        ConditionVector getConditions() const;
        ConditionVector getConditions(const QUuid &eventId) const;
        ConditionPtr getCondition(const OperatorPtr &op) const;
        ConditionLink getConditionLink(const OperatorPtr &op) const;
        ConditionLink getConditionLink(const OperatorInterface *op) const;
        ConditionLinks getConditionLinks() const;
        bool hasActiveCondition(const OperatorPtr &op) const;

        /* Links the given operator to the given condition and subindex. Any
         * existing condition link will be replaced. */
        //bool setConditionLink(const OperatorPtr &op, ConditionInterface *cond, int subIndex);
        bool setConditionLink(const OperatorPtr &op, const ConditionLink &cl);

        /* Clears the condition link of the given operator if it was linked to
         * the given condition and subIndex. */
        //bool clearConditionLink(const OperatorPtr &op, ConditionInterface *cond, int subIndex);
        bool clearConditionLink(const OperatorPtr &op, const ConditionLink &cl);

        /* Clears the condition link of the given operator no matter which
         * condition it is using. */
        bool clearConditionLink(const OperatorPtr &op);

        /* Clears all conditions links of any objects using the given
         * condition.
         * Returns the number of condition links cleared. */
        size_t clearConditionLinksUsing(const ConditionInterface *cond);

        //
        // Directory Objects
        //
        const DirectoryVector &getDirectories() const { return m_directories; }
        DirectoryVector &getDirectories() { return m_directories; }

        const DirectoryVector getDirectories(const QUuid &eventId,
                                             const DisplayLocation &loc = DisplayLocation::Any) const;

        const DirectoryVector getDirectories(const QUuid &eventId,
                                             s32 userLevel,
                                             const DisplayLocation &loc = DisplayLocation::Any) const;

        const DirectoryVector getDirectories(
            s32 userLevel, const DisplayLocation &loc = DisplayLocation::Any) const;

        DirectoryPtr getDirectory(const QUuid &id) const;

        // Matches on (eventId, displayLocation and directoryName). The first match is returned.
        DirectoryPtr getDirectory(
            const QUuid &eventId, const DisplayLocation &loc, const QString &name) const;

        void setDirectories(const DirectoryVector &dirs);
        void addDirectory(const DirectoryPtr &dir);
        void removeDirectory(const DirectoryPtr &dir);
        void removeDirectory(int index);

        int directoryCount() const { return m_directories.size(); }

        DirectoryPtr getParentDirectory(const AnalysisObjectPtr &obj) const;
        QVector<DirectoryPtr> getParentDirectories(const AnalysisObjectPtr &obj) const;
        AnalysisObjectVector getDirectoryContents(const QUuid &directoryId) const;
        AnalysisObjectVector getDirectoryContents(const DirectoryPtr &directory) const;
        AnalysisObjectVector getDirectoryContents(const Directory *directory) const;

        AnalysisObjectVector getDirectoryContentsRecursively(const QUuid &directoryId) const;
        AnalysisObjectVector getDirectoryContentsRecursively(const DirectoryPtr &directory) const;
        AnalysisObjectVector getDirectoryContentsRecursively(const Directory *directory) const;

        int removeDirectoryRecursively(const DirectoryPtr &dir);

        //
        // Untyped Object access
        //

        AnalysisObjectPtr getObject(const QUuid &id) const;
        int removeObjectsRecursively(const AnalysisObjectVector &objects);
        AnalysisObjectVector getAllObjects() const;
        int objectCount() const;
        void addObjects(const AnalysisObjectStore &objects);
        void addObjects(const AnalysisObjectVector &objects);

        //
        // VME Module Properties
        //
        QVariantList getModulePropertyList() const
        {
            return property("ModuleProperties").toList();
        }

        void setModulePropertyList(const QVariantList &props)
        {
            setProperty("ModuleProperties", props);
        }

        //
        // Pre and post run work
        //

        void updateRanks();

        void beginRun(const RunInfo &runInfo,
                      const VMEConfig *vmeConfig,
                      Logger logger = {});

    private:
        void beginRun(const RunInfo &runInfo,
                      const vme_analysis_common::VMEIdToIndex &vmeMap,
                      Logger logger = {});

    public:

        enum BeginRunOption
        {
            ClearState,
            KeepState,
        };

        // This overload of beginRun() reuses information that was previously
        // passed in one of the other beginRun() overloads. Bad design
        // everywhere.
        void beginRun(BeginRunOption option, const VMEConfig *vmeConfig, Logger logger = {});

        void endRun();

        //
        // Processing
        //
        void beginEvent(int eventIndex);
        void processModuleData(int eventIndex, int moduleIndex, const u32 *data, u32 size);
        void endEvent(int eventIndex);
        // Called once for every SectionType_Timetick section
        void processTimetick();
        double getTimetickCount() const;

        //
        // Serialization
        //

        std::error_code read(const QJsonObject &json, const VMEConfig *vmeConfig = nullptr);
        void write(QJsonObject &json) const;

        /* Object flags containing system internal information. */
        ObjectFlags::Flags getObjectFlags() const { return m_flags; }
        void setObjectFlags(ObjectFlags::Flags flags) { m_flags = flags; }
        void clearObjectFlags(ObjectFlags::Flags flagsToClear)
        {
            m_flags &= (~flagsToClear);
        }

        //
        // Misc
        //
        s32 getNumberOfSinks() const;
        size_t getTotalSinkStorageSize() const;
        s32 getMaxUserLevel() const;
        s32 getMaxUserLevel(const QUuid &eventId) const;

        void clear();
        bool isEmpty() const;

        bool isModified() const { return m_modified; }
        void setModified(bool b = true);

        A2AdapterState *getA2AdapterState() { return m_a2State.get(); }
        const A2AdapterState *getA2AdapterState() const { return m_a2State.get(); }

        RunInfo getRunInfo() const { return m_runInfo; }
        void setRunInfo(const RunInfo &ri) { m_runInfo = ri; }

        /* Additional settings tied to VME objects but stored in the analysis
         * due to logical and convenience reasons.
         * Contains things like: the MultiEventProcessing flag for VME event
         * configs and the ModuleHeaderFilter string for module configs.
         */

        using VMEObjectSettings = QHash<QUuid, QVariantMap>;

        // These overloads set/get the settings stored for a specific VME object id.
        void setVMEObjectSettings(const QUuid &objectId, const QVariantMap &settings);
        QVariantMap getVMEObjectSettings(const QUuid &objectId) const;

        // These overloads set/return all settings, not the subtree tied to a specific id.
        void setVMEObjectSettings(const VMEObjectSettings &settings);
        VMEObjectSettings getVMEObjectSettings() const;

        ObjectFactory &getObjectFactory() { return m_objectFactory; }

        bool anyObjectNeedsRebuild() const;

        static int getCurrentAnalysisVersion();

        void setUserLevelsHidden(const QVector<bool> &hidden);
        QVector<bool> getUserLevelsHidden() const;

        vme_analysis_common::VMEIdToIndex getVMEIdToIndexMapping() const
        {
            return m_vmeMap;
        }

    private:
        void updateRank(OperatorInterface *op,
                        QSet<OperatorInterface *> &updated,
                        QSet<OperatorInterface *> &visited);

        SourceVector m_sources;
        OperatorVector m_operators;
        DirectoryVector m_directories;
        VMEObjectSettings m_vmeObjectSettings;
        ObjectFlags::Flags m_flags = ObjectFlags::None;
        ConditionLinks m_conditionLinks;

        ObjectFactory m_objectFactory;

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

RawDataDisplay LIBMVME_EXPORT make_raw_data_display(
    std::shared_ptr<Extractor> extractor,
    double unitMin, double unitMax,
    const QString &xAxisTitle, const QString &unitLabel);

RawDataDisplay LIBMVME_EXPORT make_raw_data_display(
    const MultiWordDataFilter &extractionFilter,
    double unitMin, double unitMax,
    const QString &name, const QString &xAxisTitle, const QString &unitLabel);

void LIBMVME_EXPORT add_raw_data_display(
    Analysis *analysis,
    const QUuid &eventId,
    const QUuid &moduleId,
    const RawDataDisplay &display);

void LIBMVME_EXPORT do_beginRun_forward(PipeSourceInterface *pipeSource, const RunInfo &runInfo = {});

QString LIBMVME_EXPORT make_unique_operator_name(Analysis *analysis, const QString &prefix);

bool LIBMVME_EXPORT required_inputs_connected_and_valid(OperatorInterface *op);
bool LIBMVME_EXPORT no_input_connected(OperatorInterface *op);

QString LIBMVME_EXPORT info_string(const Analysis *analysis);

/** Adjusts the userlevel of all the dependees of the given operator_ by the specified
 * levelDelta. */
void LIBMVME_EXPORT adjust_userlevel_forward(const OperatorVector &allOperators,
                                             OperatorInterface *operator_,
                                             s32 levelDelta);

/* Recursively expands the given object vector to contain all subobjects inside any
 * directories contained in the original object vector. */
AnalysisObjectVector expand_objects(const AnalysisObjectVector &vec,
                                    const Analysis *analysis);

/* Returns a vector of the objects contained in the given object set but in the same order as
 * the objects are stored in the analysis.
 * Note: directories are not expanded, no recursion is done. */
AnalysisObjectVector order_objects(const AnalysisObjectSet &objects,
                                   const Analysis *analysis);

/* Same as the overload taking an AnalysisObjectSet but internally builds the
 * set from the given object vector. */
AnalysisObjectVector order_objects(const AnalysisObjectVector &objects,
                                   const Analysis *analysis);

namespace read_options
{
    using Opt = u8;
    static const Opt None = 0u;
    // If set Analysis::beginRun() is called after successfully reading the
    // analysis structure. This means the A2 Adapter and the a2 structures will
    // be available right away.
    static const Opt BuildAnalysis = 1u;
};

LIBMVME_EXPORT std::pair<std::unique_ptr<Analysis>, QString>
    read_analysis_config_from_file(const QString &filename, const VMEConfig *vmeConfig,
                                   read_options::Opt = read_options::BuildAnalysis,
                                   Logger logger = {});

// Returns a list of parent directory names of the object.
QStringList LIBMVME_EXPORT
make_parent_path_list(const AnalysisObjectPtr &obj);

QJsonDocument serialize_analysis_to_json_document(const Analysis &analysis);

} // end namespace analysis

#endif /* __ANALYSIS_H__ */
