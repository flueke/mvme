#ifndef UUID_364b82ee_241c_4c09_acbf_f7e36698fb74
#define UUID_364b82ee_241c_4c09_acbf_f7e36698fb74

#include "globals.h"
#include "vme_script.h"
#include "data_filter.h"
#include <QObject>
#include <QUuid>
#include <qwt_scale_map.h>

class QJsonObject;

class ConfigObject: public QObject
{
    Q_OBJECT
    signals:
        void modifiedChanged(bool);
        void modified(bool);
        void enabledChanged(bool);

    public:
        ConfigObject(QObject *parent = 0);

        QUuid getId() const { return m_id; }

        virtual void setModified(bool b = true);
        bool isModified() const { return m_modified; }

        void setEnabled(bool b);
        bool isEnabled() const { return m_enabled; }

        QString getObjectPath() const;

        void read(const QJsonObject &json);
        void write(QJsonObject &json) const;

        ConfigObject *findChildById(const QUuid &id, bool recurse=true) const
        {
            return findChildById<ConfigObject *>(id, recurse);
        }

        template<typename T> T findChildById(const QUuid &id, bool recurse=true) const
        {
            for (auto child: children())
            {
                auto configObject = qobject_cast<ConfigObject *>(child);

                if (configObject)
                {
                    if (configObject->getId() == id)
                        return qobject_cast<T>(configObject);
                }

                if (recurse)
                {
                    if (auto obj = configObject->findChildById<T>(id, recurse))
                        return obj;
                }
            }

            return {};
        }

        template<typename T, typename Predicate>
        T findChildByPredicate(Predicate p, bool recurse=true) const
        {
            for (auto child: children())
            {
                auto asT = qobject_cast<T>(child);

                if (asT && p(asT))
                    return asT;

                if (recurse)
                {
                    if (auto cfg = qobject_cast<ConfigObject *>(child))
                    {
                        if (auto obj = cfg->findChildByPredicate<T>(p, recurse))
                            return obj;
                    }
                }
            }
            return {};
        }

    protected:
        ConfigObject(QObject *parent, bool watchDynamicProperties);
        bool eventFilter(QObject *obj, QEvent *event) override;
        void setWatchDynamicProperties(bool doWatch);

        virtual void read_impl(const QJsonObject &json) = 0;
        virtual void write_impl(QJsonObject &json) const = 0;


        QUuid m_id;
        bool m_modified = false;
        bool m_enabled = true;
        bool m_eventFilterInstalled = false;
};

class VMEScriptConfig: public ConfigObject
{
    Q_OBJECT
    public:
        using ConfigObject::ConfigObject;

        QString getScriptContents() const
        { return m_script; }

        void setScriptContents(const QString &);

        vme_script::VMEScript getScript(u32 baseAddress = 0) const;

        QString getVerboseTitle() const;

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        QString m_script;
};

class ModuleConfig: public ConfigObject
{
    Q_OBJECT
    public:
        ModuleConfig(QObject *parent = 0);

        uint32_t getBaseAddress() const { return m_baseAddress; }

        void setBaseAddress(uint32_t address)
        {
            if (address != m_baseAddress)
            {
                m_baseAddress = address;
                setModified();
            }
        }

        // TODO: make private
        VMEModuleType type = VMEModuleType::Invalid;

        /** Known keys for a module:
         * "parameters", "readout_settings", "readout", "reset" */
        // TODO: make private
        QMap<QString, VMEScriptConfig *> vmeScripts;

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        uint32_t m_baseAddress = 0;
        QHash<u32, u32> m_registerCache;
};

class EventConfig: public ConfigObject
{
    Q_OBJECT
    signals:
    void moduleAdded(ModuleConfig *module);
    void moduleAboutToBeRemoved(ModuleConfig *module);

    public:
        EventConfig(QObject *parent = nullptr);

        void addModuleConfig(ModuleConfig *config)
        {
            config->setParent(this);
            modules.push_back(config);
            emit moduleAdded(config);
            setModified();
        }

        bool removeModuleConfig(ModuleConfig *config)
        {
            bool ret = modules.removeOne(config);
            if (ret)
            {
                emit moduleAboutToBeRemoved(config);
                setModified();
            }

            return ret;
        }

        QList<ModuleConfig *> getModuleConfigs() const { return modules; }

        TriggerCondition triggerCondition = TriggerCondition::NIM1;
        uint8_t irqLevel = 0;
        uint8_t irqVector = 0;
        // Maximum time between scaler stack executions in units of 0.5s
        uint8_t scalerReadoutPeriod = 0;
        // Maximum number of events between scaler stack executions
        uint16_t scalerReadoutFrequency = 0;

        QList<ModuleConfig *> modules;
        /** Known keys for an event:
         * "daq_start", "daq_stop", "readout_start", "readout_end"
         */
        QMap<QString, VMEScriptConfig *> vmeScripts;

        /* Set by the readout worker and then used by the buffer
         * processor to map from stack ids to event configs. */
        // TODO: move this elsewhere as it is vmusb specific
        uint8_t stackID;

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;
};

class DAQConfig: public ConfigObject
{
    Q_OBJECT
    signals:
        void eventAdded(EventConfig *config);
        void eventAboutToBeRemoved(EventConfig *config);

        void globalScriptAdded(VMEScriptConfig *config, const QString &category);
        void globalScriptAboutToBeRemoved(VMEScriptConfig *config);

    public:
        using ConfigObject::ConfigObject;

        void addEventConfig(EventConfig *config);
        bool removeEventConfig(EventConfig *config);
        bool contains(EventConfig *config);
        QList<EventConfig *> getEventConfigs() const { return eventConfigs; }
        EventConfig *getEventConfig(int eventIndex) { return eventConfigs.value(eventIndex); }
        EventConfig *getEventConfig(const QString &name) const;

        ModuleConfig *getModuleConfig(int eventIndex, int moduleIndex);
        QList<ModuleConfig *> getAllModuleConfigs() const;
        QPair<int, int> getEventAndModuleIndices(ModuleConfig *cfg) const;

        void addGlobalScript(VMEScriptConfig *config, const QString &category);
        bool removeGlobalScript(VMEScriptConfig *config);

        void setListFileOutputDirectory(const QString &dir);
        QString getListFileOutputDirectory() const { return m_listFileOutputDirectory; }
        bool isListFileOutputEnabled() const { return m_listFileOutputEnabled; }
        void setListFileOutputEnabled(bool enabled);

        /** Known keys for a DAQConfig:
         * "daq_start", "daq_stop", "manual"
         */
        QMap<QString, QList<VMEScriptConfig *>> vmeScriptLists;
        QList<EventConfig *> eventConfigs;

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        QString m_listFileOutputDirectory;
        bool m_listFileOutputEnabled = false;
};

class DataFilterConfig: public ConfigObject
{
    Q_OBJECT
    public:
        DataFilterConfig(QObject *parent = 0)
            : ConfigObject(parent, true)
        { }

        DataFilterConfig(const DataFilter &filter, QObject *parent = 0)
            : ConfigObject(parent, true)
            , m_filter(filter)
        {}

        const DataFilter &getFilter() const { return m_filter; }
        void setFilter(const DataFilter &filter);

        QString getAxisTitle() const { return m_axisTitle; }
        void setAxisTitle(const QString &title);

        QString getUnitString() const { return m_unitString; }
        void setUnitString(const QString &unit);

        double getUnitMin(u32 address) const;
        void setUnitMin(u32 address, double value);

        double getUnitMax(u32 address) const;
        void setUnitMax(u32 address, double value);

        QPair<double, double> getUnitRange(u32 address) const;
        void setUnitRange(u32 address, double min, double max);
        void setUnitRange(u32 address, QPair<double, double> range);

        QPair<double, double> getBaseUnitRange() const;
        void setBaseUnitRange(double min, double max);
        void resetToBaseUnits(u32 address);

        u32 getDataBits() const { return getFilter().getExtractBits('D'); }
        u32 getAddressBits() const { return getFilter().getExtractBits('A'); }
        u32 getAddressCount() const { return (1u << getAddressBits()); }

        QwtScaleMap makeConversionMap(u32 address) const;

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        bool isAddressValid(u32 address);

        DataFilter m_filter;
        QString m_axisTitle;
        QString m_unitString;
        QPair<double, double> m_baseUnitRange;
        QVector<QPair<double, double>> m_unitRanges;
};

class Hist1DConfig: public ConfigObject
{
    Q_OBJECT
    public:
        Hist1DConfig(QObject *parent = 0)
            : ConfigObject(parent, true)
        {}

        u32 getBits() const { return m_bits; }
        void setBits(u32 bits)
        {
            if (m_bits != bits)
            {
                m_bits = bits;
                setModified();
            }
        }

        QUuid getFilterId() const { return m_filterId; }
        void setFilterId(const QUuid &id)
        {
            if (id != m_filterId)
            {
                m_filterId = id;
                setModified();
            }
        }

        u32 getFilterAddress() const { return m_filterAddress; }
        void setFilterAddress(u32 address)
        {
            if (m_filterAddress != address)
            {
                m_filterAddress = address;
                setModified();
            }
        }

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        u32 m_bits = 0;
        QUuid m_filterId;
        u32 m_filterAddress = 0;
};

struct Hist2DAxisConfig
{
    QUuid filterId;
    u32 filterAddress = 0;
    u32 bits = 0;
    u32 offset = 0;
    u32 shift = 0;
    QString title;
    QString unit;
    double unitMin = 0.0;
    double unitMax = 0.0;

    bool operator==(const Hist2DAxisConfig &o) const
    {
        return filterId == o.filterId
            && filterAddress == o.filterAddress
            && bits == o.bits
            && offset == o.offset
            && shift == o.shift
            && title == o.title
            && unit == o.unit
            && unitMin == o.unitMin
            && unitMax == o.unitMax
            ;
    }

    bool operator!=(const Hist2DAxisConfig &o) const
    {
        return !operator==(o);
    }
};

class Hist2DConfig: public ConfigObject
{
    Q_OBJECT
    public:
        Hist2DConfig(QObject *parent = 0)
            : ConfigObject(parent, true)
        {}

        Hist2DAxisConfig getAxisConfig(Qt::Axis axis) const { return m_axes[axis]; }
        void setAxisConfig(Qt::Axis axis, const Hist2DAxisConfig &config);

        // Source filter Id and address
        QUuid getFilterId(Qt::Axis axis) const;
        void setFilterId(Qt::Axis axis, const QUuid &id);

        u32 getFilterAddress(Qt::Axis axis) const;
        void setFilterAddress(Qt::Axis axis, u32 address);

        // Axis resolutions
        u32 getBits(Qt::Axis axis) const;
        void setBits(Qt::Axis axis, u32 bits);

        // Bin offsets in source resolution to only accumulate a subset of the
        // source filters data.
        u32 getOffset(Qt::Axis axis) const;
        void setOffset(Qt::Axis axis, u32 offset);

        // Shift amount to scale source value after offset subtraction when
        // filling the histogram.
        u32 getShift(Qt::Axis axis) const;
        void setShift(Qt::Axis axis, u32 shift);

        // Axis meta information: title, unit label and unit range
        QString getAxisTitle(Qt::Axis) const;
        void setAxisTitle(Qt::Axis, const QString &title);

        QString getAxisUnitLabel(Qt::Axis) const;
        void setAxisUnitLabel(Qt::Axis, const QString &unit);

        double getUnitMin(Qt::Axis axis) const;
        void setUnitMin(Qt::Axis axis, double value);

        double getUnitMax(Qt::Axis axis) const;
        void setUnitMax(Qt::Axis axis, double value);

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        Hist2DAxisConfig m_axes[Qt::ZAxis];
};

class AnalysisConfig: public ConfigObject
{
    Q_OBJECT
    signals:
        void objectAdded(ConfigObject *object);
        void objectAboutToBeRemoved(ConfigObject *object);

    public:
        using ConfigObject::ConfigObject;

        using DataFilterConfigList = QList<DataFilterConfig *>;

        DataFilterConfigList getFilters(int eventIndex, int moduleIndex) const;
        QMap<int, QMap<int, DataFilterConfigList>> getFilters() const { return m_filters; }
        void setFilters(int eventIndex, int moduleIndex, const DataFilterConfigList &filters);
        void removeFilters(int eventIndex, int moduleIndex);
        void addFilter(int eventIndex, int moduleIndex, DataFilterConfig *config);
        void removeFilter(int eventIndex, int moduleIndex, DataFilterConfig *config);

        QPair<int, int> getEventAndModuleIndices(DataFilterConfig *config) const;

        void addHist1DConfig(Hist1DConfig *config);
        void addHist2DConfig(Hist2DConfig *config);

        void removeHist1DConfig(Hist1DConfig *config);
        void removeHist2DConfig(Hist2DConfig *config);

        QList<Hist1DConfig *> get1DHistogramConfigs() const { return m_1dHistograms; }
        QList<Hist2DConfig *> get2DHistogramConfigs() const { return m_2dHistograms; }

        /* Update 1d and 2d histograms using the given filter as their source.
         * Axis titles, units and ranges will be updated. */
        void updateHistogramsForFilter(DataFilterConfig *filter);

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        QMap<int, QMap<int, DataFilterConfigList>> m_filters;
        QList<Hist1DConfig *> m_1dHistograms;
        QList<Hist2DConfig *> m_2dHistograms;
};

void updateHistogramConfigFromFilterConfig(Hist1DConfig *histoConfig, DataFilterConfig *filterConfig);

#if 0
class VariantMapConfig: public ConfigObject
{
    Q_OBJECT
    public:
        using ConfigObject::ConfigObject;

        void set(const QString &key, const QVariant &value);
        QVariant get(const QString &key);
        void remove(const QString &key);
        QVariantMap getMap() const { return m_map; }

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        QVariantMap m_map;
};
#endif

#endif
