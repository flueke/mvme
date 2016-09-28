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

        virtual void setModified(bool b);
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

    private:
        QString m_script;
};

class ModuleConfig: public ConfigObject
{
    Q_OBJECT
    public:
        using ConfigObject::ConfigObject;

        int getNumberOfChannels() const;
        int getDataBits() const;
        u32 getDataExtractMask();

        void updateRegisterCache();

        VMEModuleType type = VMEModuleType::Invalid;
        uint32_t baseAddress = 0;

        /** Known keys: "parameters", "readout" */
        QMap<QString, VMEScriptConfig *> vmeScripts;

    protected:
        virtual void read_impl(const QJsonObject &json) override;
        virtual void write_impl(QJsonObject &json) const override;

    private:
        QHash<u32, u32> m_registerCache;
};

class EventConfig: public QObject
{
    Q_OBJECT
    signals:
        void nameChanged(const QString &name);
        void modified();

    public:
        EventConfig(QObject *parent = 0)
            : QObject(parent)
            , m_id(QUuid::createUuid())
        {}

        ~EventConfig() { qDeleteAll(modules); }

        void setModified();

        void setName(const QString &name)
        {
            if (m_name != name)
            {
                m_name = name;
                emit nameChanged(m_name);
            }
        }

        QString getName() const { return m_name; }

        void read(const QJsonObject &json);
        void write(QJsonObject &json) const;

    TriggerCondition triggerCondition = TriggerCondition::NIM1;
    uint8_t irqLevel = 0;
    uint8_t irqVector = 0;
    // Maximum time between scaler stack executions in units of 0.5s
    uint8_t scalerReadoutPeriod = 0;
    // Maximum number of events between scaler stack executions
    uint16_t scalerReadoutFrequency = 0;
    // Readout trigger delay (global for NIM and IRQ triggers) in microseconds
    uint8_t readoutTriggerDelay = 0;

    QList<ModuleConfig *> modules;

    /* Set by the readout worker and then used by the buffer
     * processor to map from stack ids to event configs. */
    uint8_t stackID;

    QUuid getId() const
    {
        return m_id;
    }

    private:
        QUuid m_id;
        QString m_name;
};

class DAQConfig: public QObject
{
    Q_OBJECT
    signals:
        void modifiedChanged(bool);

    public:
        DAQConfig(QObject *parent = 0)
            : QObject(parent)
        {}

        ~DAQConfig() { qDeleteAll(m_eventConfigs); }

        void setModified(bool b=true);
        bool isModified() const { return m_isModified; }

        void addEventConfig(EventConfig *config)
        {
            m_eventConfigs.push_back(config); setModified();
        }

        bool removeEventConfig(EventConfig *config)
        {
            bool ret = m_eventConfigs.removeOne(config);
            if (ret)
            {
                setModified();
            }

            return ret;
        }

        bool contains(EventConfig *config)
        {
            return m_eventConfigs.indexOf(config) >= 0;
        }

        QList<EventConfig *> getEventConfigs() const { return m_eventConfigs; }
        EventConfig *getEventConfig(int eventID) { return m_eventConfigs.value(eventID); }
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

        void read(const QJsonObject &json);
        void write(QJsonObject &json) const;
        QByteArray toJson() const;

    private:
        bool m_isModified = false;
        QList<EventConfig *> m_eventConfigs;
        QString m_listFileOutputDirectory;
        bool m_listFileOutputEnabled = false;
};

#endif

/*
 
DAQConfig
    - Lists of named vme scripts for the following hooks:
      "init", "shutdown", "manual"
      Each script should have an `enabled' flag
    - List of EventConfig
    - Additional parameters

EventConfig
    - TriggerCondition
    - List of ModuleConfigs
    - vme scripts for the following hooks:
      "daqstart", "daqstop"
    - Named vme scripts for the following hooks:
      "init", "shutdown", "manual"

ModuleConfig
    - baseAddress
    - moduleType
    - vme scripts for the following hooks:
      "parameters", "readout-settings"


Event
    - vme scripts

        class VMEScriptConfig:
        {
            QPair<QString, QString>;
        };

        * Known keys:
         * vme_init, vme_shutdown
        QMap<QString, QList<VMEScriptConfig *>> vmeScripts;
*/
