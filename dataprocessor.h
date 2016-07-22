#ifndef UUID_6195aef5_54ab_4666_9d1a_3a7620ed54ea
#define UUID_6195aef5_54ab_4666_9d1a_3a7620ed54ea

#include <QObject>
#include "databuffer.h"

class DataProcessor: public QObject
{
    Q_OBJECT
    signals:
        void bufferProcessed(DataBuffer *buffer);

    public:
        DataProcessor(QObject *parent = 0);

    public slots:
        void processBuffer(DataBuffer *buffer);
};

#endif
