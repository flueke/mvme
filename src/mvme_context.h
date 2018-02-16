/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef UUID_9196420f_dd04_4572_8e4b_952039634913
#define UUID_9196420f_dd04_4572_8e4b_952039634913

#include "globals.h"
#include "databuffer.h"
#include "libmvme_export.h"
#include "mvme_stream_worker.h"
#include "rate_monitoring.h"
#include "vme_config.h"
#include "vme_controller.h"
#include "vme_readout_worker.h"
#include <memory>

#include <QFuture>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QList>
#include <QSet>
#include <QSettings>
#include <QWidget>

class MVMEMainWindow;
class ListFile;
class ListFileReader;
class QJsonObject;
class RateMonitorRegistry;

class QTimer;
class QThread;

namespace analysis
{
    class Analysis;
    class OperatorInterface;
    class AnalysisWidget;
}

struct MVMEContextPrivate;

class LIBMVME_EXPORT MVMEContext: public QObject
{
    Q_OBJECT
    signals:
        void modeChanged(GlobalMode mode);
        void daqStateChanged(const DAQState &state);
        void mvmeStreamWorkerStateChanged(MVMEStreamWorkerState);
        void controllerStateChanged(ControllerState state);

        void vmeControllerSet(VMEController *controller);

        void daqConfigChanged(VMEConfig *config);
        void daqConfigFileNameChanged(const QString &fileName);

        void analysisConfigFileNameChanged(const QString &name);
        void analysisChanged();

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
        MVMEContext(MVMEMainWindow *mainwin, QObject *parent = 0);
        ~MVMEContext();

        void setVMEController(VMEController *controller, const QVariantMap &settings = QVariantMap());
        void setVMEController(VMEControllerType type, const QVariantMap &settings = QVariantMap());
        VMEController *getVMEController() const { return m_controller; }
        void reconnectVMEController();

        ControllerState getControllerState() const;
        VMEReadoutWorker *getReadoutWorker() { return m_readoutWorker; }
        VMEConfig *getConfig() { return m_vmeConfig; }
        VMEConfig *getVMEConfig() { return m_vmeConfig; }
        void setVMEConfig(VMEConfig *config);
        QList<EventConfig *> getEventConfigs() const { return m_vmeConfig->getEventConfigs(); }
        QString getUniqueModuleName(const QString &prefix) const;
        DAQState getDAQState() const;
        MVMEStreamWorkerState getMVMEStreamWorkerState() const;
        const DAQStats &getDAQStats() const { return m_daqStats; }
        DAQStats &getDAQStats() { return m_daqStats; }

        struct ReplayFileAnalysisInfo
        {
            QString filename;
            QString analysisFilename;
            QByteArray analysisConfigData;
        };

        bool setReplayFile(ListFile *listFile);
        void closeReplayFile();
        ListFile *getReplayFile() const { return m_listFile; }
        void setReplayFileAnalysisInfo(ReplayFileAnalysisInfo info);
        ReplayFileAnalysisInfo getReplayFileAnalysisInfo() const;

        void setMode(GlobalMode mode);
        GlobalMode getMode() const;
        MVMEStreamWorker *getMVMEStreamWorker() const { return m_streamWorker.get(); }

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

        void setAnalysisConfigFileName(QString name, bool updateWorkspace = true);
        QString getAnalysisConfigFileName() const { return m_analysisConfigFileName; }

        // Logs the given msg as-is.
        void logMessageRaw(const QString &msg);
        // Prepends the current time to the given msg.
        void logMessage(const QString &msg);
        QStringList getLogBuffer() const;

        friend class MVMEMainWindow;

        //QFuture<vme_script::ResultList>
        vme_script::ResultList
            runScript(const vme_script::VMEScript &script,
                      vme_script::LoggerFun logger = vme_script::LoggerFun(),
                      bool logEachResult = false);

        MVMEMainWindow *getMainWindow() const { return m_mainwin; }

        // Workspace handling
        void newWorkspace(const QString &dirName);
        void openWorkspace(const QString &dirName);
        bool isWorkspaceOpen() const { return !m_workspaceDir.isEmpty(); }

        QString getWorkspaceDirectory() const { return m_workspaceDir; }
        // Returns an empty shared_Ptr if getWorkspaceDirectory() returns an empty string
        std::shared_ptr<QSettings> makeWorkspaceSettings() const;
        // Returns an empty string if not workspace is open
        QString getWorkspacePath(const QString &settingsKey, const QString &defaultValue = QString(), bool setIfDefaulted = true) const;

        void loadVMEConfig(const QString &fileName);

        struct AnalysisLoadFlags
        {
            bool NoAutoResume: 1;
        };

        bool loadAnalysisConfig(const QString &fileName);
        bool loadAnalysisConfig(QIODevice *input, const QString &inputInfo = QString());
        bool loadAnalysisConfig(const QJsonDocument &doc, const QString &inputInfo = QString(), AnalysisLoadFlags flags = {});
        bool loadAnalysisConfig(const QByteArray &blob, const QString &inputInfo = QString());

        // listfile output
        void setListFileOutputInfo(const ListFileOutputInfo &info);
        ListFileOutputInfo getListFileOutputInfo() const;


        bool isWorkspaceModified() const;

        analysis::Analysis *getAnalysis() const { return m_analysis_ng; }

        bool isAnalysisRunning();
        void stopAnalysis();
        void resumeAnalysis();
        QJsonDocument getAnalysisJsonDocument() const;

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

        RunInfo getRunInfo() const;

        RateMonitorRegistry *getRateMonitorRegistry() const;

    public slots:
        void startDAQReadout(u32 nCycles = 0, bool keepHistoContents = false);
        void startDAQReplay(u32 nEvents = 0, bool keepHistoContents = false);

        /* These methods act on DAQ readout or replay depending on the current
         * GlobalMode. */
        void stopDAQ();
        void pauseDAQ();
        void resumeDAQ(u32 nCycles = 0);

        void addAnalysisOperator(QUuid eventId, const std::shared_ptr<analysis::OperatorInterface> &op, s32 userLevel);
        void analysisOperatorEdited(const std::shared_ptr<analysis::OperatorInterface> &op);

    private slots:
        void tryOpenController();
        void logModuleCounters();
        void onDAQStateChanged(DAQState state);
        void onMVMEStreamWorkerStateChanged(MVMEStreamWorkerState state);
        void onDAQDone();
        void onReplayDone();

        // config related
        void onEventAdded(EventConfig *event);
        void onEventAboutToBeRemoved(EventConfig *config);
        void onModuleAdded(ModuleConfig *module);
        void onModuleAboutToBeRemoved(ModuleConfig *config);
        void onGlobalScriptAboutToBeRemoved(VMEScriptConfig *config);

        void onControllerStateChanged(ControllerState state);

        friend struct MVMEContextPrivate;

    private:
        std::shared_ptr<QSettings> makeWorkspaceSettings(const QString &workspaceDirectory) const;
        void setWorkspaceDirectory(const QString &dirName);
        void prepareStart();
        QString getListFileOutputDirectoryFullPath(const QString &directory) const;

        MVMEContextPrivate *m_d;

        VMEConfig *m_vmeConfig = nullptr;
        QString m_configFileName;
        QString m_analysisConfigFileName;
        QString m_workspaceDir;
        QString m_listFileDir;
        bool    m_listFileEnabled;
        ListFileFormat m_listFileFormat = ListFileFormat::Plain;

        VMEController *m_controller = nullptr;
        QTimer *m_ctrlOpenTimer;
        QTimer *m_logTimer;
        QFuture<VMEError> m_ctrlOpenFuture;
        QFutureWatcher<VMEError> m_ctrlOpenWatcher;
        QThread *m_readoutThread;

        VMEReadoutWorker *m_readoutWorker = nullptr;

        QThread *m_eventThread;
        std::unique_ptr<MVMEStreamWorker> m_streamWorker;

        QSet<QObject *> m_objects;
        QMap<QString, QMap<QObject *, QObject *>> m_objectMappings;
        MVMEMainWindow *m_mainwin;
        DAQStats m_daqStats;
        ListFile *m_listFile = nullptr;
        GlobalMode m_mode;
        DAQState m_daqState;
        ListFileReader *m_listFileWorker;
        QTime m_replayTime;

        analysis::Analysis *m_analysis_ng;

        ThreadSafeDataBufferQueue m_freeBuffers;
        ThreadSafeDataBufferQueue m_fullBuffers;

        analysis::AnalysisWidget *m_analysisUi = nullptr;
};

struct AnalysisPauser
{
    AnalysisPauser(MVMEContext *context);
    ~AnalysisPauser();

    MVMEContext *context;
    bool was_running;
};

struct DAQPauser
{
    DAQPauser(MVMEContext *context);
    ~DAQPauser();

    MVMEContext *context;
    bool was_running;
};

QPair<bool, QString> saveAnalysisConfig(analysis::Analysis *analysis,
                                        const QString &fileName,
                                        QString startPath,
                                        QString fileFilter,
                                        MVMEContext *context);

QPair<bool, QString> saveAnalysisConfigAs(analysis::Analysis *analysis,
                                           QString startPath,
                                           QString fileFilter,
                                           MVMEContext *context);

#endif
