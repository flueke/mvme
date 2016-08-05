#ifndef UUID_9196420f_dd04_4572_8e4b_952039634913
#define UUID_9196420f_dd04_4572_8e4b_952039634913

#include <QList>
#include <QWidget>
#include <QFuture>
#include "globals.h"
#include "vmecommandlist.h"
#include "vme_module.h"
#include "databuffer.h"
#include "mvme_config.h"
#include "histogram.h"

class VMEController;
class VMUSBReadoutWorker;
class VMUSBBufferProcessor;
class MVMEEventProcessor;
class mvme;

class QTimer;
class QThread;

class MVMEContext: public QObject
{
    static const size_t dataBufferCount = 20;
    static const size_t dataBufferSize  = 30 * 1024;

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

    public:
        MVMEContext(mvme *mainwin, QObject *parent = 0);
        ~MVMEContext();

        void addEventConfig(EventConfig *eventConfig);
        void removeEvent(EventConfig *event);
        void addModule(EventConfig *eventConfig, ModuleConfig *module);
        void removeModule(ModuleConfig *module);
        void setController(VMEController *controller);

        // TODO: add something like getUniqueModuleName(prefix);
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

        QMap<QString, Histogram *> getHistograms() { return m_histograms; }
        Histogram *getHistogram(const QString &name) { return m_histograms.value(name);; }

        bool addHistogram(const QString &name, Histogram *histo)
        {
            if (m_histograms.contains(name))
                return false;
            m_histograms[name] = histo;
            emit histogramAdded(name, histo);
            return true;
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

        friend class mvme;

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

        QThread *m_eventProcessorThread;
        MVMEEventProcessor *m_eventProcessor;

        DataBufferQueue m_freeBuffers;
        QString m_configFileName;
        QMap<QString, Histogram *> m_histograms;
        mvme *m_mainwin;
};

#endif
