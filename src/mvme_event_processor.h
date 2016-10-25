#ifndef UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f
#define UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f

#include <QObject>

class DataBuffer;
class MVMEContext;

class MVMEEventProcessorPrivate;

class MVMEEventProcessor: public QObject
{
    Q_OBJECT
    signals:
        void bufferProcessed(DataBuffer *buffer);
        void logMessage(const QString &);

    public:
        MVMEEventProcessor(MVMEContext *context);
        ~MVMEEventProcessor();

    public slots:
        void newRun();
        void processDataBuffer(DataBuffer *buffer);

    private:
        MVMEEventProcessorPrivate *m_d;
};

#endif
