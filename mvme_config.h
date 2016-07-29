#ifndef UUID_364b82ee_241c_4c09_acbf_f7e36698fb74
#define UUID_364b82ee_241c_4c09_acbf_f7e36698fb74

#include "globals.h"

class QJsonObject;

struct ModuleConfig
{
    VMEModuleType type = VMEModuleType::Invalid;
    QString name;
    uint32_t baseAddress = 0;
    uint32_t mcstAddress = 0;
    bool useMcst = false;

    // These strings must contain content that's convertible to an InitList
    QString initReset;          // module reset
    QString initParameters;     // module physics parameters
    QString initReadout;        // module readout settings (irq, threshold, event mode)
    QString initStartDaq;       // reset FIFO, counters, start acq
    QString initStopDaq;        // stop acq, clear FIFO

    // vmusb readout stack as a string. Only stored if it is modified by the
    // user, otherwise generated on the fly.
    // TODO: For other controllers the vmusb stack format obviously won't work.
    QString readoutStack;

    void read(const QJsonObject &json);
    void write(QJsonObject &json) const;
};

struct EventConfig
{
    QString name;
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

    void read(const QJsonObject &json);
    void write(QJsonObject &json) const;

    ~EventConfig()
    {
        qDeleteAll(modules);
    }
};

struct DAQConfig
{
    QList<EventConfig *> eventConfigs;

    void read(const QJsonObject &json);
    void write(QJsonObject &json) const;

    ~DAQConfig()
    {
        qDeleteAll(eventConfigs);
    }
};

#endif
