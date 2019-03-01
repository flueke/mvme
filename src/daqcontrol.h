#ifndef __DAQCONTROL_H__
#define __DAQCONTROL_H__

#include <QObject>
#include "mvme_context.h"

class DAQControl: public QObject
{
    Q_OBJECT
    public:
        DAQControl(MVMEContext *context, QObject *parent = nullptr);

    public slots:
        void startDAQ(u32 nCycles, bool keepHistoContents);
        void stopDAQ();
        void pauseDAQ();
        void resumeDAQ(u32 nCycles);

    private:
        MVMEContext *m_context;
};

#endif /* __DAQCONTROL_H__ */
