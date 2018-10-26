#ifndef __MVME_ANALYSIS_DATA_SERVER_H__
#define __MVME_ANALYSIS_DATA_SERVER_H__

#include "mvme_stream_processor.h"
#include <QHostAddress>

// Server specific stuff
// NOTE: the interface is not great for this kind of object: The
// object needs to be created, then configured and then be told to
// start listening.  At least this last step has to happen in the
// thread where the server actually should run.
// Also we want to listen before any run has been started and thus
// using beginRun() to start listening for connections is too late.
// Procedure as I think it could work:
// MVMEContext is created, creates a server object, configures the
// listening port and interface, moves the object to the analysis
// thread, starts the analysis thread and then invokes a
// startListening() method on this object.
// Additional issue: what about the logger? On every beginRun a logger
// is passed in but we'd like to have one even before that.

class AnalysisDataServer: public QObject, public IMVMEStreamModuleConsumer
{
    Q_OBJECT
    public:
        AnalysisDataServer(QObject *parent = nullptr);
        AnalysisDataServer(Logger logger, QObject *parent = nullptr);
        virtual ~AnalysisDataServer();

        virtual void startup() override;
        virtual void shutdown() override;

        virtual void beginRun(const RunInfo &runInfo,
                              const VMEConfig *vmeConfig,
                              const analysis::Analysis *analysis,
                              Logger logger) override;

        virtual void endRun(const std::exception *e = nullptr) override;

        virtual void beginEvent(s32 eventIndex) override;
        virtual void endEvent(s32 eventIndex) override;
        virtual void processModuleData(s32 eventIndex, s32 moduleIndex,
                                       const u32 *data, u32 size) override;
        virtual void processTimetick() override;

        // Server specific settings and info
        void setLogger(Logger logger);
        void setListeningInfo(const QHostAddress &address, quint16 port);
        bool isListening() const;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

#endif /* __MVME_ANALYSIS_DATA_SERVER_H__ */
