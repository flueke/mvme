#ifndef UUID_364b82ee_241c_4c09_acbf_f7e36698fb74
#define UUID_364b82ee_241c_4c09_acbf_f7e36698fb74

#include "globals.h"
#include "vme_script.h"
#include <QObject>
#include <QUuid>

class QJsonObject;
class EventConfig;

class ConfigObject: public QObject
{
    Q_OBJECT
    signals:
        void modifiedChanged(bool);
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

    protected:
        virtual void read_impl(const QJsonObject &json) = 0;
        virtual void write_impl(QJsonObject &json) const = 0;

        QUuid m_id;
        bool m_modified = false;
        bool m_enabled = true;
};

class VMEScriptConfig: public ConfigObject
{
    Q_OBJECT
    public:
        using ConfigObject::ConfigObject;

        QString getScriptContents() const
        { return m_script; }

        void setScriptContents(const QString &);

        vme_script::VMEScript getScript() const
        { return vme_script::parse(m_script); }

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

        int getNumberOfChannels() const;
        int getDataBits() const;
        u32 getDataExtractMask();

        void updateRegisterCache();

        VMEModuleType type = VMEModuleType::Invalid;
        uint32_t baseAddress = 0;

        /** Known keys for a module:
         * "parameters", "readout_settings", "readout" */
        QMap<QString, VMEScriptConfig *> vmeScripts;

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
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
        EventConfig *getEventConfig(int eventID) { return eventConfigs.value(eventID); }
        EventConfig *getEventConfig(const QString &name) const;

        ModuleConfig *getModuleConfig(int eventID, int moduleIndex);
        QVector<ModuleConfig *> getAllModuleConfigs() const;

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

#endif
