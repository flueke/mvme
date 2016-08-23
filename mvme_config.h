#ifndef UUID_364b82ee_241c_4c09_acbf_f7e36698fb74
#define UUID_364b82ee_241c_4c09_acbf_f7e36698fb74

#include "globals.h"
#include <QObject>
#include <QUuid>

class QJsonObject;
class EventConfig;

class ModuleConfig: public QObject
{
    Q_OBJECT
    signals:
        void nameChanged(const QString &name);

    public:
        ModuleConfig(QObject *parent = 0)
            : QObject(parent)
        {}

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
        QString getFullPath() const;
        int getNumberOfChannels() const;
        int getADCResolution() const;

        void read(const QJsonObject &json);
        void write(QJsonObject &json) const;

        VMEModuleType type = VMEModuleType::Invalid;
        uint32_t baseAddress = 0;
        uint32_t mcstAddress = 0;
        bool useMcst = false;

        // These strings must contain content that's convertible to an InitList
        QString initReset;          // module reset
        QString initParameters;     // module physics parameters
        QString initReadout;        // module readout settings (irq, threshold, event mode)
        QString initStartDaq;       // reset FIFO, counters, start acq
        QString initStopDaq;        // stop acq, clear FIFO

        // vmusb readout stack as a string.
        // TODO: For other controllers the vmusb stack format obviously won't work.
        QString readoutStack;

        void generateReadoutStack();

        EventConfig *event = 0;

        void setId(const QUuid &uuid)
        {
            m_id = uuid;
        }

        QUuid getId() const
        {
            return m_id;
        }

    private:
        QUuid m_id;
        QString m_name;
};

class EventConfig: public QObject
{
    Q_OBJECT
    signals:
        void nameChanged(const QString &name);

    public:
        EventConfig(QObject *parent = 0)
            : QObject(parent)
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

    TriggerCondition triggerCondition;
    uint8_t irqLevel = 0;
    uint8_t irqVector = 0;
    // Maximum time between scaler stack executions in units of 0.5s
    uint8_t scalerReadoutPeriod = 0;
    // Maximum number of events between scaler stack executions
    uint16_t scalerReadoutFrequency = 0;
    QList<ModuleConfig *> modules;

    /* Set by the readout worker and then used by the buffer
     * processor to map from stack ids to event configs. */
    uint8_t stackID;

    void setId(const QUuid &uuid)
    {
        m_id = uuid;
    }

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

        void addEventConfig(EventConfig *config) { m_eventConfigs.push_back(config); }
        bool removeEventConfig(EventConfig *config) { return m_eventConfigs.removeOne(config); }
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
