#ifndef UUID_9196420f_dd04_4572_8e4b_952039634913
#define UUID_9196420f_dd04_4572_8e4b_952039634913

#include "globals.h"
#include "databuffer.h"
#include "mvme_config.h"
#include "vme_controller.h"
#include "threading.h"
#include <memory>
#include <QList>
#include <QWidget>
#include <QFuture>
#include <QFutureWatcher>
#include <QSettings>
#include <QSet>

class VMUSBReadoutWorker;
class VMUSBBufferProcessor;
class MVMEEventProcessor;
class mvme;
class ListFile;
class ListFileReader;
class QJsonObject;

class QTimer;
class QThread;

namespace analysis
{
    class Analysis;
    class OperatorInterface;
    class AnalysisWidget;
}

struct MVMEContextPrivate;

class MVMEContext: public QObject
{
    Q_OBJECT
    signals:
        void modeChanged(GlobalMode mode);
        void daqStateChanged(const DAQState &state);
        void eventProcessorStateChanged(EventProcessorState);
        void controllerStateChanged(ControllerState state);

        void vmeControllerSet(VMEController *controller);

        void daqConfigChanged(DAQConfig *config);
        void daqConfigFileNameChanged(const QString &fileName);

        void analysisConfigFileNameChanged(const QString &name);
        void analysisNGChanged();

        void objectAdded(QObject *object);
        void objectAboutToBeRemoved(QObject *object);

        void objectMappingAdded(QObject *key, QObject *value, const QString &category);
        void objectMappingRemoved(QObject *key, QObject *value, const QString &category);

        void sigLogMessage(const QString &);

        void daqAboutToStart(quint32 nCycles);

        void workspaceDirectoryChanged(const QString &);

        // Forwarding DAQConfig signals
        void eventAdded(EventConfig *event);
        void eventAboutToBeRemoved(EventConfig *event);
        void moduleAdded(ModuleConfig *module);
        void moduleAboutToBeRemoved(ModuleConfig *module);

    public:
        MVMEContext(mvme *mainwin, QObject *parent = 0);
        ~MVMEContext();

        void setController(VMEController *controller);

        QString getUniqueModuleName(const QString &prefix) const;

        VMEController *getController() const { return m_controller; }
        ControllerState getControllerState() const;
        VMUSBReadoutWorker *getReadoutWorker() const { return m_readoutWorker; }
        VMUSBBufferProcessor *getBufferProcessor() const { return m_bufferProcessor; }
        DAQConfig *getConfig() { return m_daqConfig; }
        DAQConfig *getDAQConfig() { return m_daqConfig; }
        void setDAQConfig(DAQConfig *config);
        QList<EventConfig *> getEventConfigs() const { return m_daqConfig->getEventConfigs(); }
        DAQState getDAQState() const;
        EventProcessorState getEventProcessorState() const;
        const DAQStats &getDAQStats() const { return m_daqStats; }
        DAQStats &getDAQStats() { return m_daqStats; }
        void setReplayFile(ListFile *listFile);
        void closeReplayFile();
        ListFile *getReplayFile() const { return m_listFile; }
        void setMode(GlobalMode mode);
        GlobalMode getMode() const;
        MVMEEventProcessor *getEventProcessor() const { return m_eventProcessor; }

        //
        // Object registry
        //
        void addObject(QObject *object);
        void removeObject(QObject *object, bool doDeleteLater = true);
        bool containsObject(QObject *object);

        template<typename T>
        QVector<T> getObjects() const
        {
            QVector<T> ret;
            for (auto obj: m_objects)
            {
                auto casted = qobject_cast<T>(obj);
                if (casted)
                    ret.push_back(casted);
            }
            return ret;
        }

        template<typename T, typename Predicate>
        QVector<T> filterObjects(Predicate p) const
        {
            QVector<T> ret;
            for (auto obj: m_objects)
            {
                auto casted = qobject_cast<T>(obj);
                if (casted && p(casted))
                    ret.push_back(casted);
            }
            return ret;
        }

        //
        // Object mappings
        //
        void addObjectMapping(QObject *key, QObject *value, const QString &category = QString());
        QObject *removeObjectMapping(QObject *key, const QString &category = QString());
        QObject *getMappedObject(QObject *key, const QString &category = QString()) const;

        //
        // Config mappings (specialized object mappings)
        //
        void registerObjectAndConfig(QObject *object, QObject *config)
        {
            addObjectMapping(object, config, QSL("ObjectToConfig"));
            addObjectMapping(config, object, QSL("ConfigToObject"));
            addObject(object);
        }

        void unregisterObjectAndConfig(QObject *object=nullptr, QObject *config=nullptr)
        {
            if (object)
                config = getConfigForObject(object);
            else if (config)
                object = getObjectForConfig(config);
            else
                return;

            removeObjectMapping(object, QSL("ObjectToConfig"));
            removeObjectMapping(config, QSL("ConfigToObject"));
        }

        QObject *getObjectForConfig(QObject *config)
        {
            return getMappedObject(config, QSL("ConfigToObject"));
        }

        QObject *getConfigForObject(QObject *object)
        {
            return getMappedObject(object, QSL("ObjectToConfig"));
        }

        template<typename T>
        T *getObjectForConfig(QObject *config)
        {
            return qobject_cast<T *>(getObjectForConfig(config));
        }

        template<typename T>
        T *getConfigForObject(QObject *object)
        {
            return qobject_cast<T *>(getConfigForObject(object));
        }

        void setConfigFileName(QString name, bool updateWorkspace = true);
        QString getConfigFileName() const { return m_configFileName; }

        void setAnalysisConfigFileName(QString name);
        QString getAnalysisConfigFileName() const { return m_analysisConfigFileName; }

        void logMessage(const QString &msg);
        void logMessages(const QStringList &mgs, const QString &prefix = QString());

        friend class mvme;

        //QFuture<vme_script::ResultList>
        vme_script::ResultList
            runScript(const vme_script::VMEScript &script,
                      vme_script::LoggerFun logger = vme_script::LoggerFun(),
                      bool logEachResult = false);

        mvme *getMainWindow() const { return m_mainwin; }

        // Workspace handling
        void newWorkspace(const QString &dirName);
        void openWorkspace(const QString &dirName);

        void setWorkspaceDirectory(const QString &dirName);
        QString getWorkspaceDirectory() const { return m_workspaceDir; }
        std::shared_ptr<QSettings> makeWorkspaceSettings() const;

        void loadVMEConfig(const QString &fileName);
        void loadAnalysisConfig(const QString &fileName);

        void setListFileDirectory(const QString &dirName);
        QString getListFileDirectory() const { return m_listFileDir; }
        void setListFileOutputEnabled(bool b);
        bool isListFileOutputEnabled() const { return m_listFileEnabled; }

        bool isWorkspaceModified() const;

        analysis::Analysis *getAnalysisNG() const { return m_analysis_ng; }

        bool isAnalysisRunning();
        void stopAnalysis();
        void resumeAnalysis();

        void setAnalysisUi(analysis::AnalysisWidget *analysisUi)
        {
            m_analysisUi = analysisUi;
        }

        analysis::AnalysisWidget *getAnalysisUi() const
        {
            return m_analysisUi;
        }

        void addObjectWidget(QWidget *widget, QObject *object, const QString &stateKey);
        bool hasObjectWidget(QObject *object) const;
        QWidget *getObjectWidget(QObject *object) const;
        QList<QWidget *> getObjectWidgets(QObject *object) const;
        void activateObjectWidget(QObject *object);

        void addWidget(QWidget *widget, const QString &stateKey);

    public slots:
        void startDAQ(quint32 nCycles=0);
        // Stops DAQ or replay depending on the current GlobalMode
        void stopDAQ();
        void pauseDAQ();
        void resumeDAQ();

        void startReplay(u32 nEvents = 0);
        void pauseReplay();
        void resumeReplay(u32 nEvents = 0);

        void addAnalysisOperator(QUuid eventId, const std::shared_ptr<analysis::OperatorInterface> &op, s32 userLevel);
        void analysisOperatorEdited(const std::shared_ptr<analysis::OperatorInterface> &op);

    private slots:
        void tryOpenController();
        void logModuleCounters();
        void onDAQStateChanged(DAQState state);
        void onEventProcessorStateChanged(EventProcessorState state);
        void onDAQDone();
        void onReplayDone();

        // config related
        void onEventAdded(EventConfig *event);
        void onEventAboutToBeRemoved(EventConfig *config);
        void onModuleAdded(ModuleConfig *module);
        void onModuleAboutToBeRemoved(ModuleConfig *config);
        void onGlobalScriptAboutToBeRemoved(VMEScriptConfig *config);

        friend class MVMEContextPrivate;

    private:
        void prepareStart();

        MVMEContextPrivate *m_d;

        DAQConfig *m_daqConfig = nullptr;
        QString m_configFileName;
        QString m_analysisConfigFileName;
        QString m_workspaceDir;
        QString m_listFileDir;
        bool    m_listFileEnabled;

        VMEController *m_controller = nullptr;
        QTimer *m_ctrlOpenTimer;
        QTimer *m_logTimer;
        QFuture<VMEError> m_ctrlOpenFuture;
        QFutureWatcher<VMEError> m_ctrlOpenWatcher;
        QThread *m_readoutThread;

        VMUSBReadoutWorker *m_readoutWorker;
        VMUSBBufferProcessor *m_bufferProcessor;

        QThread *m_eventThread;
        MVMEEventProcessor *m_eventProcessor;

        QSet<QObject *> m_objects;
        QMap<QString, QMap<QObject *, QObject *>> m_objectMappings;
        mvme *m_mainwin;
        DAQStats m_daqStats;
        ListFile *m_listFile = nullptr;
        GlobalMode m_mode;
        DAQState m_daqState;
        ListFileReader *m_listFileWorker;
        QTime m_replayTime;

        analysis::Analysis *m_analysis_ng;

        ThreadSafeDataBufferQueue m_freeBufferQueue;
        ThreadSafeDataBufferQueue m_filledBufferQueue;

        analysis::AnalysisWidget *m_analysisUi = nullptr;
};

struct AnalysisPauser
{
    AnalysisPauser(MVMEContext *context);
    ~AnalysisPauser();

    MVMEContext *context;
    bool was_running;
};

#endif
