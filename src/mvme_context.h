/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include "libmvme_export.h"

#include <mesytec-mvlc/mesytec-mvlc.h>
#include "analysis/analysis.h"
#include "analysis_service_provider.h"
#include "globals.h"
#include "databuffer.h"
#include "listfile_replay.h"
#include "mvme_options.h"
#include "mvme_stream_worker.h"
#include "vme_config.h"
#include "vme_controller.h"
#include "vme_readout_worker.h"
#include "vme_script.h"
#include "vme_script_exec.h"

#include <memory>

#include <QElapsedTimer>
#include <QFuture>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QList>
#include <QSet>
#include <QSettings>
#include <QWidget>

class MVMEMainWindow;
class ListFile;
class QJsonObject;

class QThread;

namespace analysis
{
namespace ui
{
    class AnalysisWidget;
} // ns ui
} // ns analysis

struct MVMEContextPrivate;

// listfile opening
struct OpenListfileOptions
{
    bool loadAnalysis = false;
};

// Filenames used for the temporary vme and analysis configs created when
// opening a listfile.
static const QString ListfileTempVMEConfigFilename = QSL(".vmeconfig_from_listfile.vme");
static const QString ListfileTempAnalysisConfigFilename = QSL(".analysis_from_listfile.analysis");

class LIBMVME_EXPORT MVMEContext: public QObject
{
    Q_OBJECT
    signals:
        void mvmeStateChanged(const MVMEState &newState);
        void modeChanged(GlobalMode mode);
        void daqStateChanged(const DAQState &state);
        void mvmeStreamWorkerStateChanged(AnalysisWorkerState);
        void controllerStateChanged(ControllerState state);

        void vmeControllerAboutToBeChanged();
        void vmeControllerSet(VMEController *controller);

        void vmeConfigAboutToBeSet(VMEConfig *oldConfig, VMEConfig *newConfig);
        void vmeConfigChanged(VMEConfig *config);
        void vmeConfigFilenameChanged(const QString &fileName);

        void ListFileOutputInfoChanged(const ListFileOutputInfo &info);

        /* Emitted when a new analysis is loaded.
         * Note that a nullptr may be passed in case loading did not succeed. */
        void analysisChanged(analysis::Analysis *analysis);

        /* Emitted when the current analysis file name changed. */
        void analysisConfigFileNameChanged(const QString &name);

        void objectAdded(QObject *object);
        void objectAboutToBeRemoved(QObject *object);

        void objectMappingAdded(QObject *key, QObject *value, const QString &category);
        void objectMappingRemoved(QObject *key, QObject *value, const QString &category);

        void sigLogMessage(const QString &);
        void sigLogError(const QString &);

        void daqAboutToStart();

        void workspaceDirectoryChanged(const QString &);

        // Forwarding DAQConfig signals
        void eventAdded(EventConfig *event);
        void eventAboutToBeRemoved(EventConfig *event);
        void moduleAdded(ModuleConfig *module);
        void moduleAboutToBeRemoved(ModuleConfig *module);

        // MVLC readout buffer sniffing
        void sniffedReadoutBufferReady(const mesytec::mvlc::ReadoutBuffer &readoutBuffer);

    public:
        explicit MVMEContext(QObject *parent = nullptr, const MVMEOptions &options = {}):
            MVMEContext(nullptr, parent, options)
        { }

        explicit MVMEContext(MVMEMainWindow *mainwin, QObject *parent = nullptr,
                             const MVMEOptions &options = {});

        ~MVMEContext();

        bool setVMEController(VMEController *controller, const QVariantMap &settings = QVariantMap());
        bool setVMEController(VMEControllerType type, const QVariantMap &settings = QVariantMap());
        VMEController *getVMEController() const { return m_controller; }

        ControllerState getControllerState() const;
        VMEReadoutWorker *getReadoutWorker() { return m_readoutWorker; }
        VMEConfig *getVMEConfig() const { return m_vmeConfig; }
        void setVMEConfig(VMEConfig *config);
        QList<EventConfig *> getEventConfigs() const { return m_vmeConfig->getEventConfigs(); }
        QString getUniqueModuleName(const QString &prefix) const;
        DAQState getDAQState() const;
        AnalysisWorkerState getMVMEStreamWorkerState() const;
        MVMEState getMVMEState() const;
        DAQStats getDAQStats() const;

        bool setReplayFileHandle(ListfileReplayHandle listfile, OpenListfileOptions options = {});
        const ListfileReplayHandle &getReplayFileHandle() const;
        ListfileReplayHandle &getReplayFileHandle();
        void closeReplayFileHandle();

        void setMode(GlobalMode mode);
        GlobalMode getMode() const;
        StreamWorkerBase *getMVMEStreamWorker() const { return m_streamWorker.get(); }

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

        void setVMEConfigFilename(QString name, bool updateWorkspace = true);
        QString getVMEConfigFilename() const { return m_configFileName; }

        void setAnalysisConfigFilename(QString name, bool updateWorkspace = true);
        QString getAnalysisConfigFilename() const { return m_analysisConfigFileName; }

        QStringList getLogBuffer() const;

        friend class MVMEMainWindow;

#if 1
        //QFuture<vme_script::ResultList>
        vme_script::ResultList
            runScript(const vme_script::VMEScript &script,
                      vme_script::LoggerFun logger = vme_script::LoggerFun(),
                      bool logEachResult = false);
#endif

        MVMEMainWindow *getMainWindow() const { return m_mainwin; }

        // Workspace handling
        void newWorkspace(const QString &dirName);
        void openWorkspace(const QString &dirName);
        bool isWorkspaceOpen() const { return !m_workspaceDir.isEmpty(); }

        QString getWorkspaceDirectory() const { return m_workspaceDir; }
        // Returns an empty shared_Ptr if getWorkspaceDirectory() returns an empty string
        std::shared_ptr<QSettings> makeWorkspaceSettings() const;
        // Returns an empty string if no workspace is open
        QString getWorkspacePath(const QString &settingsKey,
                                 const QString &defaultValue = QString(),
                                 bool setIfDefaulted = true) const;

        /* Reapplies some of the settings found in the mvmeworkspace.ini file.  Right now
         * (re)starts or stops the JSON-RPC server. */
        void reapplyWorkspaceSettings();

        void loadVMEConfig(const QString &fileName);
        void vmeConfigWasSaved();


        bool loadAnalysisConfig(const QString &fileName);
        bool loadAnalysisConfig(QIODevice *input, const QString &inputInfo = QString());
        bool loadAnalysisConfig(const QJsonDocument &doc, const QString &inputInfo = QString(),
                                AnalysisLoadFlags flags = {});
        bool loadAnalysisConfig(const QByteArray &blob, const QString &inputInfo = QString());
        void analysisWasCleared();
        void analysisWasSaved();

        // listfile output
        void setListFileOutputInfo(const ListFileOutputInfo &info);
        ListFileOutputInfo getListFileOutputInfo() const;


        bool isWorkspaceModified() const;

        analysis::Analysis *getAnalysis() const { return m_analysis.get(); }

        bool isAnalysisRunning();
        void stopAnalysis();
        void resumeAnalysis(analysis::Analysis::BeginRunOption option);
        QJsonDocument getAnalysisJsonDocument() const;

        void setAnalysisUi(analysis::ui::AnalysisWidget *analysisUi)
        {
            m_analysisUi = analysisUi;
        }

        analysis::ui::AnalysisWidget *getAnalysisUi() const
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
        QString getRunNotes() const;

        AnalysisServiceProvider *getAnalysisServiceProvider() const;

    public slots:
        // Logs the given msg as-is.
        void logMessageRaw(const QString &msg);
        // Prepends the current time to the given msg.
        void logMessage(const QString &msg);
        void logError(const QString &errMsg);

        void startDAQReadout(u32 nCycles = 0, bool keepHistoContents = false);
        void startDAQReplay(u32 nEvents = 0, bool keepHistoContents = false);

        /* These methods act on DAQ readout or replay depending on the current
         * GlobalMode. */
        void stopDAQ();
        void pauseDAQ();
        void resumeDAQ(u32 nCycles = 0);

        void addAnalysisOperator(QUuid eventId, const std::shared_ptr<analysis::OperatorInterface> &op,
                                 s32 userLevel);
        void analysisOperatorEdited(const std::shared_ptr<analysis::OperatorInterface> &op);

        void reconnectVMEController();
        void forceResetVMEController();
        void dumpVMEControllerRegisters();

        void setRunNotes(const QString &notes);

    private slots:
        void tryOpenController();
        void logModuleCounters();
        void onDAQStateChanged(DAQState state);
        void onAnalysisWorkerStateChanged(AnalysisWorkerState state);
        void onDAQDone();
        void onReplayDone();

        // config related
        void onEventAdded(EventConfig *event);
        void onEventAboutToBeRemoved(EventConfig *config);
        void onModuleAdded(ModuleConfig *module);
        void onModuleAboutToBeRemoved(ModuleConfig *config);
        void onGlobalChildAboutToBeRemoved(ConfigObject *config);

        void onControllerStateChanged(ControllerState state);
        void onControllerOpenFinished();

        friend struct MVMEContextPrivate;

    private:
        std::shared_ptr<QSettings> makeWorkspaceSettings(const QString &workspaceDirectory) const;
        void setWorkspaceDirectory(const QString &dirName);
        void cleanupWorkspaceAutoSaveFiles();

        QString getListFileOutputDirectoryFullPath(const QString &directory) const;
        bool prepareStart();

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

        QThread *m_analysisThread;
        std::unique_ptr<StreamWorkerBase> m_streamWorker;

        QSet<QObject *> m_objects;
        QMap<QString, QMap<QObject *, QObject *>> m_objectMappings;
        MVMEMainWindow *m_mainwin;
        GlobalMode m_mode;
        DAQState m_daqState;
        QElapsedTimer m_replayTime;

        std::shared_ptr<analysis::Analysis> m_analysis;

        ThreadSafeDataBufferQueue m_freeBuffers;
        ThreadSafeDataBufferQueue m_fullBuffers;

        analysis::ui::AnalysisWidget *m_analysisUi = nullptr;
};

struct DAQPauser
{
    explicit DAQPauser(MVMEContext *context);
    ~DAQPauser();

    MVMEContext *context;
    bool was_running;
};

#endif
