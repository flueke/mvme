#ifndef UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f
#define UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f

#include "util.h"
#include <QObject>

class DataBuffer;
class MVMEContext;
class BufferIterator;
class ModuleConfig;
class HistogramCollection;

struct EventProcessorCounters
{
    u64 buffers = 0; // number of buffers seen
    u64 events = 0;  // number of event sections seen

    struct ModuleCounters
    {
        u64 events = 0;
        u64 headerWords = 0;
        u64 dataWords = 0;
        u64 eoeWords = 0;
    };

    QHash<ModuleConfig *, ModuleCounters> moduleCounters;
};

class MVMEEventProcessor: public QObject
{
    Q_OBJECT
    signals:
        void bufferProcessed(DataBuffer *buffer);
        void logMessage(const QString &);

    public:
        MVMEEventProcessor(MVMEContext *context);

        EventProcessorCounters getCounters() const { return m_counters; }

    public slots:
        void newRun();
        void processEventBuffer(DataBuffer *buffer);

    private:
        MVMEContext *m_context;
        EventProcessorCounters m_counters;
        QHash<ModuleConfig *, HistogramCollection *> m_mod2hist;
};

#endif
