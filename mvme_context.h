#ifndef UUID_9196420f_dd04_4572_8e4b_952039634913
#define UUID_9196420f_dd04_4572_8e4b_952039634913

#include "globals.h"
#include "vmecommandlist.h"
#include "databuffer.h"
#include "mvme_config.h"
#include "histogram.h"
#include "channelspectro.h"
#include <QList>
#include <QWidget>
#include <QFuture>
#include <QDateTime>

class VMEController;
class VMUSBReadoutWorker;
class VMUSBBufferProcessor;
class MVMEEventProcessor;
class mvme;
class ListFile;
class ListFileWorker;
class QJsonObject;

class QTimer;
class QThread;

struct DAQStats
{
    QDateTime startTime;
    QDateTime endTime;
    u64 bytesRead = 0;
    u64 buffersRead = 0;
    u64 buffersWithErrors = 0;
    u64 droppedBuffers = 0;
    int freeBuffers = 0;
    int readSize = 0;
    u64 listFileBytesWritten = 0;
    QMap<QObject *, u64> eventCounts; // maps EventConfig/ModuleConfig to event count
};

enum class GlobalMode
{
    NotSet,
    DAQ,
    ListFile
};

Q_DECLARE_METATYPE(GlobalMode);

class MVMEContext: public QObject
{
    Q_OBJECT
    signals:
        void daqStateChanged(const DAQState &state);

        void vmeControllerSet(VMEController *controller);

        void eventConfigAdded(EventConfig *eventConfig);
        void eventConfigAboutToBeRemoved(EventConfig *eventConfig);

        void moduleAdded(EventConfig *eventConfig, ModuleConfig *module);
        void moduleAboutToBeRemoved(ModuleConfig *module);

        void configChanged(DAQConfig *config);
        void configFileNameChanged(const QString &fileName);

        void histogramAdded(const QString &name, Histogram *histo);
        void histogramAboutToBeRemoved(const QString &name, Histogram *histo);

        void hist2DAdded(ChannelSpectro *hist2d);

        void logMessage(const QString &);

        void modeChanged(GlobalMode mode);

    public:
        MVMEContext(mvme *mainwin, QObject *parent = 0);
        ~MVMEContext();

        void addEventConfig(EventConfig *eventConfig);
        void removeEvent(EventConfig *event);
        void addModule(EventConfig *eventConfig, ModuleConfig *module);
        void removeModule(ModuleConfig *module);
        void setController(VMEController *controller);

        // TODO: add something like getUniqueModuleName(prefix) to generate module names
        int getTotalModuleCount() const
        {
            int ret = 0;
            for (auto eventConfig: m_config->getEventConfigs())
                ret += eventConfig->modules.size();
            return ret;
        }

        VMEController *getController() const { return m_controller; }
        VMUSBReadoutWorker *getReadoutWorker() const { return m_readoutWorker; }
        VMUSBBufferProcessor *getBufferProcessor() const { return m_bufferProcessor; }
        DAQConfig *getConfig() { return m_config; }
        void setConfig(DAQConfig *config);
        QList<EventConfig *> getEventConfigs() const { return m_config->getEventConfigs(); }
        DataBufferQueue *getFreeBuffers() { return &m_freeBuffers; }
        DAQState getDAQState() const;
        const DAQStats &getDAQStats() const { return m_daqStats; }
        DAQStats &getDAQStats() { return m_daqStats; }
        void setListFile(ListFile *listFile);
        void setMode(GlobalMode mode);
        GlobalMode getMode() const;

        QMap<QString, Histogram *> getHistograms() { return m_histograms; }
        QList<Histogram *> getHistogramList() const { return m_histograms.values(); }

        Histogram *getHistogram(const QString &name)
        {
            return m_histograms.value(name);
        }

        bool addHistogram(const QString &name, Histogram *histo)
        {
            if (m_histograms.contains(name))
                return false;
            m_histograms[name] = histo;
            emit histogramAdded(name, histo);
            return true;
        }
        void addHist2D(ChannelSpectro *hist2d);

        QVector<ChannelSpectro *> get2DHistograms() const
        {
            return m_2dHistograms;
        }

        bool removeHistogram(const QString &name)
        {
            auto histo = m_histograms.take(name);

            if (histo)
            {
                emit histogramAboutToBeRemoved(name, histo);
                delete histo;
                return true;
            }

            return false;
        }

        void setConfigFileName(const QString &name)
        {
            m_configFileName = name;
            emit configFileNameChanged(name);
        }

        QString getConfigFileName() const
        {
            return m_configFileName;
        }

        void write(QJsonObject &json) const;
        void read(const QJsonObject &json);

        friend class mvme;

    public slots:
        void startReplay();

    private slots:
        void tryOpenController();

    private:
        DAQConfig *m_config;
        VMEController *m_controller = nullptr;
        QTimer *m_ctrlOpenTimer;
        QFuture<void> m_ctrlOpenFuture;
        QThread *m_readoutThread;

        VMUSBReadoutWorker *m_readoutWorker;
        VMUSBBufferProcessor *m_bufferProcessor;

        QThread *m_eventThread;
        MVMEEventProcessor *m_eventProcessor;

        DataBufferQueue m_freeBuffers;
        QString m_configFileName;
        QMap<QString, Histogram *> m_histograms;
        QVector<ChannelSpectro *> m_2dHistograms;
        mvme *m_mainwin;
        DAQStats m_daqStats;
        ListFile *m_listFile;
        GlobalMode m_mode;
        ListFileWorker *m_listFileWorker;
};

#endif
