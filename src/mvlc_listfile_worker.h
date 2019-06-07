#ifndef __MVLC_LISTFILE_WORKER_H__
#define __MVLC_LISTFILE_WORKER_H__

#include "vme_readout_worker.h"

class LIBMVME_EXPORT MVLCListfileWorker: public QObject
{
    Q_OBJECT
    signals:
        void stateChanged(const DAQState &);
        void replayStopped();
        void replayPaused();

    public:
        MVLCListfileWorker(QObject *parent = nullptr);
        ~MVLCListfileWorker() override;

        void setListfile(std::unique_ptr<QIODevice> listfile);
        DAQStats getStats() const;
        bool isRunning() const;
        DAQState getState() const;

    public slots:
        // Blocking call which will perform the work
        void start();

        // Thread-safe calls, setting internal flags to do the state transition
        void stop();
        void pause();
        void resume();

    private:
        void setState(DAQState state);

        struct Private;
        std::unique_ptr<Private> d;
};

#endif /* __MVLC_LISTFILE_WORKER_H__ */
