#ifndef UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f
#define UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f

#include <QObject>

class DataBuffer;
class MVMEContext;
class BufferIterator;
class ModuleConfig;

class MVMEEventProcessor: public QObject
{
    Q_OBJECT
    signals:
        void bufferProcessed(DataBuffer *buffer);

    public:
        MVMEEventProcessor(MVMEContext *context);

    public slots:
        void processEventBuffer(DataBuffer *buffer);

    private:
        MVMEContext *m_context;
};

#endif
