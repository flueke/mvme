#ifndef UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f
#define UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f

#include "util.h"
#include <QObject>

class DataBuffer;
class MVMEContext;
class BufferIterator;
class ModuleConfig;
class HistogramCollection;

class MVMEEventProcessor: public QObject
{
    Q_OBJECT
    signals:
        void bufferProcessed(DataBuffer *buffer);
        void logMessage(const QString &);

    public:
        MVMEEventProcessor(MVMEContext *context);

    public slots:
        void newRun();
        void processEventBuffer(DataBuffer *buffer);

    private:
        MVMEContext *m_context;
        QHash<ModuleConfig *, HistogramCollection *> m_mod2hist;
};

#endif
