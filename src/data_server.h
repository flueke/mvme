#ifndef __MVME_ANALYSIS_DATA_SERVER_H__
#define __MVME_ANALYSIS_DATA_SERVER_H__

#include "mvme_stream_processor.h"
#include <QHostAddress>

class AnalysisDataServer: public QObject, public IMVMEStreamModuleConsumer
{
    Q_OBJECT
    signals:
        void clientConnected();
        void clientDisconnected();

    public:
        static const uint16_t Default_ListenPort = 13801;
        static const qint64 Default_WriteThresholdBytes = Megabytes(25);


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

        // Set/get the maximum number of pending bytes allowed in the output
        // buffer for each client.
        // If this threshold is exceeded a blocking wait is performed until
        // data has been transferred from the output buffer to the operating
        // system.
        void setWriteThresholdBytes(qint64 threshold);
        qint64 getWriteThresholdBytes() const;

        bool isListening() const;

        size_t getNumberOfClients() const;

        // This information will be converted to a JSON object and sent out to
        // each newly connected client.
        void setServerInfo(const QVariantMap &serverInfo);

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

#endif /* __MVME_ANALYSIS_DATA_SERVER_H__ */
