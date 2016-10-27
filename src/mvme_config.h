#ifndef UUID_364b82ee_241c_4c09_acbf_f7e36698fb74
#define UUID_364b82ee_241c_4c09_acbf_f7e36698fb74

#include "globals.h"
#include "vme_script.h"
#include "data_filter.h"
#include <QObject>
#include <QUuid>

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

        void addEventConfig(EventConfig *config)
        {
            config->setParent(this);
            eventConfigs.push_back(config);
            emit eventAdded(config);
            setModified();
        }

        bool removeEventConfig(EventConfig *config)
        {
            bool ret = eventConfigs.removeOne(config);
            if (ret)
            {
                emit eventAboutToBeRemoved(config);
                setModified();
            }

            return ret;
        }

        bool contains(EventConfig *config)
        {
            return eventConfigs.indexOf(config) >= 0;
        }

        void addGlobalScript(VMEScriptConfig *config, const QString &category)
        {
            config->setParent(this);
            vmeScriptLists[category].push_back(config);
            emit globalScriptAdded(config, category);
            setModified();
        }

        bool removeGlobalScript(VMEScriptConfig *config)
        {
            for (auto category: vmeScriptLists.keys())
            {
                if (vmeScriptLists[category].removeOne(config))
                {
                    emit globalScriptAboutToBeRemoved(config);
                    setModified();
                    return true;
                }
            }

            return false;
        }

        QList<EventConfig *> getEventConfigs() const { return eventConfigs; }
        EventConfig *getEventConfig(int eventIndex) { return eventConfigs.value(eventIndex); }
        EventConfig *getEventConfig(const QString &name) const;

        ModuleConfig *getModuleConfig(int eventIndex, int moduleIndex);
        QList<ModuleConfig *> getAllModuleConfigs() const;

        QPair<int, int> getEventAndModuleIndices(ModuleConfig *cfg) const;

        void setListFileOutputDirectory(const QString &dir)
        {
            if (dir != m_listFileOutputDirectory)
            {
                m_listFileOutputDirectory = dir;
                m_listFileOutputEnabled = !dir.isEmpty();
                setModified();
            }
        }

        QString getListFileOutputDirectory() const
        {
            return m_listFileOutputDirectory;
        }

        bool isListFileOutputEnabled() const
        {
            return m_listFileOutputEnabled;
        }

        void setListFileOutputEnabled(bool enabled)
        {
            if (m_listFileOutputEnabled != enabled)
            {
                m_listFileOutputEnabled = enabled;
                setModified();
            }
        }

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

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        DataFilter m_filter;
};

class Hist1DConfig: public ConfigObject
{
    Q_OBJECT
    public:
        using ConfigObject::ConfigObject;

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

class Hist2DConfig: public ConfigObject
{
    Q_OBJECT
    public:
        using ConfigObject::ConfigObject;

        QUuid getXFilterId() const { return m_xFilterId; }
        QUuid getYFilterId() const { return m_yFilterId; }
        u32 getXFilterAddress() const { return m_xAddress; }
        u32 getYFilterAddress() const { return m_yAddress; }

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        u32 m_xBits = 0,
            m_yBits = 0;

        QUuid m_xFilterId,
              m_yFilterId;

        u32 m_xAddress = 0,
            m_yAddress = 0;
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

        QPair<int, int> getEventAndModuleIndices(DataFilterConfig *cfg) const;

        void addHist1DConfig(Hist1DConfig *config);
        void addHist2DConfig(Hist2DConfig *config);

        QList<Hist1DConfig *> get1DHistogramConfigs() const { return m_1dHistograms; }
        QList<Hist2DConfig *> get2DHistogramConfigs() const { return m_2dHistograms; }

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        QMap<int, QMap<int, DataFilterConfigList>> m_filters;
        QList<Hist1DConfig *> m_1dHistograms;
        QList<Hist2DConfig *> m_2dHistograms;
};

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
