#ifndef __DAQCONTROL_H__
#define __DAQCONTROL_H__

#include <chrono>
#include <QObject>
#include <QTimer>

#include "mvme_context.h"

class TimedRunControl;

// DAQ control abstraction: start, stop, pause, resume and status queries.
// Currently builds on top the central MVMEContext object.
class DAQControl: public QObject
{
    Q_OBJECT
    signals:
        void daqStateChanged(const DAQState &state);

    public:
        DAQControl(MVMEContext *context, QObject *parent = nullptr);
        ~DAQControl() override;

        DAQState getDAQState() const;

    public slots:
        void startDAQ(
            u32 nCycles, bool keepHistoContents,
            const std::chrono::milliseconds &runDuration = std::chrono::milliseconds::zero());
        void stopDAQ();
        void pauseDAQ();
        void resumeDAQ(u32 nCycles);

    private:
        MVMEContext *m_context;
        std::unique_ptr<TimedRunControl> m_timedRunControl;
};

// Helper to limit the duration of a DAQ run.
// Note: paused state does not cause this object to pause its internal timer.
// Instead after the timer expires the DAQ is stopped even if it is in paused
// state.
class TimedRunControl: public QObject
{
    Q_OBJECT

    public:
        explicit TimedRunControl(
            DAQControl *ctrl,
            const std::chrono::milliseconds &runDuration,
            QObject *parent = nullptr);

    private slots:
        void onDAQStateChanged(const DAQState &newState);
        void onTimerTimeout();

    private:
        DAQControl *m_ctrl;
        QTimer m_timer;
        bool m_shouldStop;
};

#endif /* __DAQCONTROL_H__ */
