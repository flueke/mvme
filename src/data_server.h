#ifndef __MVME_ANALYSIS_DATA_SERVER_H__
#define __MVME_ANALYSIS_DATA_SERVER_H__

#include "libmvme_export.h"
#include "mvme_stream_processor.h"
#include <QHostAddress>

class LIBMVME_EXPORT AnalysisDataServer: public QObject, public IMVMEStreamModuleConsumer
{
    Q_OBJECT
    signals:
        void clientConnected();
        void clientDisconnected();

    public:
        static const uint16_t Default_ListenPort = 13801;

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
        void setListeningInfo(const QHostAddress &address, quint16 port = Default_ListenPort);

        bool isListening() const;

        size_t getNumberOfClients() const;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

#endif /* __MVME_ANALYSIS_DATA_SERVER_H__ */
