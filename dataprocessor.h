#ifndef UUID_6195aef5_54ab_4666_9d1a_3a7620ed54ea
#define UUID_6195aef5_54ab_4666_9d1a_3a7620ed54ea

#include <QObject>

/* TODO: make this an interface for concrete processor, one for each controller
 * type. Right now this is a concrete implementation for VMUSB. */

class MVMEContext;
class DataBuffer;

class DataProcessorPrivate;

class DataProcessor: public QObject
{
    Q_OBJECT
    signals:
        void bufferProcessed(DataBuffer *buffer);

    public:
        DataProcessor(MVMEContext *context, QObject *parent = 0);

    public slots:
        void processBuffer(DataBuffer *buffer);

    private:
        DataProcessorPrivate *m_d;
};

#endif
