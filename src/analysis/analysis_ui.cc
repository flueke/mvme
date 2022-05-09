/* nvme - Mesytec VME Data Acquisition
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
#include "analysis_ui.h"

#include <map>
#include <memory>
#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QtConcurrent>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

#include "analysis/a2_adapter.h"
#include "analysis/analysis_info_widget.h"
#include "analysis/analysis_serialization.h"
#include "analysis/analysis_session.h"
#include "analysis/analysis_ui_p.h"
#include "analysis/analysis_util.h"
#include "analysis/condition_ui.h"
#include "analysis/data_extraction_widget.h"
#include "analysis/expression_operator_dialog.h"
#include "analysis/object_info_widget.h"
#include "analysis/sink_widget_factory.h"
#include "analysis/ui_eventwidget_p.h"
#include "analysis/ui_lib.h"
#include "gui_util.h"
#include "histo1d_widget.h"
#include "histo2d_widget.h"
#include "listfilter_extractor_dialog.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"
#include "mvme_qthelp.h"
#include "mvme_stream_worker.h"
#include "rate_monitor_widget.h"
#include "treewidget_utils.h"
#include "util/counters.h"
#include "util/strings.h"
#include "vme_analysis_common.h"
#include "vme_config_ui.h"

/* State of the UI and future plans
 *
 * Finding nodes for objects
 * Can use QTreeWidgetItem::treeWidget() to get the containing tree.  Right now
 * trees are recreated when updating (repopulate) and even with smarter updates
 * trees and nodes will get added and removed so the mapping has to be updated
 * constantly.
 * Can easily visit nodes given a container of analysis objects and create
 * chains of node handling objects. These objects could be configured by giving
 * them different sets of trees and modes. Also the responsibility chains could
 * be modified on the fly.
 *
 */

namespace analysis
{
namespace ui
{


static const QString AnalysisFileFilter = QSL("MVME Analysis Files (*.analysis);; All Files (*.*)");


static const u32 PeriodicUpdateTimerInterval_ms = 1000;

struct AnalysisWidgetPrivate
{
    AnalysisWidget *m_q;
    AnalysisServiceProvider *m_serviceProvider;
    EventWidget *m_eventWidget = nullptr;
    AnalysisSignalWrapper m_analysisSignalWrapper;

    QToolBar *m_toolbar;
    QFrame *m_eventWidgetFrame = nullptr;
    QScrollArea *m_eventWidgetScrollArea = nullptr;
    QStackedWidget *m_eventWidgetToolBarStack;
    QStackedWidget *m_eventWidgetEventSelectAreaToolBarStack;
    //ConditionWidget *m_conditionWidget = nullptr;
    ObjectInfoWidget *m_objectInfoWidget;
    QToolButton *m_removeUserLevelButton;
    QToolButton *m_addUserLevelButton;
    QStatusBar *m_statusBar;

    // Statusbar labels.
    QLabel *m_labelSinkStorageSize;
    QLabel *m_labelTimetickCount;
    QLabel *m_statusLabelA2;
    QLabel *m_labelEfficiency;
    QLabel *m_labelOriginalDataRate;

    QTimer *m_periodicUpdateTimer;
    WidgetGeometrySaver *m_geometrySaver;
    AnalysisInfoWidget *m_analysisInfoWidget = nullptr;
    QAction *m_actionPause;
    QAction *m_actionStepNextEvent;
    bool m_repopEnabled = true;
    QSettings m_settings;
    MVLCParserDebugHandler *mvlcParserDebugHandler = nullptr;
    MVLCSingleStepHandler *mvlcSingleStepHandler = nullptr;

    void onAnalysisChanged(Analysis *analysis);
    void repopulate();
    void repopulateEventRelatedWidgets(const QUuid &eventId);
    void doPeriodicUpdate();

    void closeAllUniqueWidgets();
    void closeAllSinkWidgets();

    void updateActions();

    void actionNew();
    void actionOpen();
    QPair<bool, QString> actionSave();
    QPair<bool, QString> actionSaveAs();
    void actionClearHistograms();

    void actionSaveSession();
    void actionLoadSession();

    void actionExploreWorkspace();
    void actionPause(bool isChecked);
    void actionStepNextEvent();

    void updateWindowTitle();
    void updateAddRemoveUserLevelButtons();

    // react to changes made to the analysis
    void onDataSourceAdded(const SourcePtr &src);
    void onDataSourceRemoved(const SourcePtr &src);
    void onOperatorAdded(const OperatorPtr &op);
    void onOperatorRemoved(const OperatorPtr &op);
    void onDirectoryAdded(const DirectoryPtr &dir);
    void onDirectoryRemoved(const DirectoryPtr &dir);
    void onConditionLinkAdded(const OperatorPtr &op, const ConditionPtr &cond);
    void onConditionLinkRemoved(const OperatorPtr &op, const ConditionPtr &cond);
    void editConditionLinkGraphically(const ConditionPtr &cond);

    AnalysisServiceProvider *getServiceProvider() const { return m_serviceProvider; }
    Analysis *getAnalysis() const { return getServiceProvider()->getAnalysis(); }
};

void AnalysisWidgetPrivate::onAnalysisChanged(Analysis *analysis)
{
    /* Assuming the old analysis has been (or will be deleted via
     * deleteLater()), thus signals connected to the old instance need not be
     * disconnected manually here. */

    m_analysisSignalWrapper.setAnalysis(analysis);
    repopulate();
}

void AnalysisWidgetPrivate::onDataSourceAdded(const SourcePtr &src)
{
    qDebug() << __PRETTY_FUNCTION__ << this << src.get();

    auto eventId = src->getEventId();
    repopulateEventRelatedWidgets(eventId);
}

void AnalysisWidgetPrivate::onDataSourceRemoved(const SourcePtr &src)
{
    qDebug() << __PRETTY_FUNCTION__ << this << src.get();

    auto eventId = src->getEventId();
    repopulateEventRelatedWidgets(eventId);
}

void AnalysisWidgetPrivate::onOperatorAdded(const OperatorPtr &op)
{
    qDebug() << __PRETTY_FUNCTION__ << this << op.get();

    auto eventId = op->getEventId();
    repopulateEventRelatedWidgets(eventId);
}

void AnalysisWidgetPrivate::onOperatorRemoved(const OperatorPtr &op)
{
    qDebug() << __PRETTY_FUNCTION__ << this << op.get();

    auto eventId = op->getEventId();
    repopulateEventRelatedWidgets(eventId);
}

void AnalysisWidgetPrivate::onDirectoryAdded(const DirectoryPtr &dir)
{
    qDebug() << __PRETTY_FUNCTION__ << this << dir.get();

    auto eventId = dir->getEventId();
    repopulateEventRelatedWidgets(eventId);
}

void AnalysisWidgetPrivate::onDirectoryRemoved(const DirectoryPtr &dir)
{
    qDebug() << __PRETTY_FUNCTION__ << this;
    auto eventId = dir->getEventId();
    repopulateEventRelatedWidgets(eventId);
}

void AnalysisWidgetPrivate::onConditionLinkAdded(const OperatorPtr &op, const ConditionPtr &cond)
{
#if 0
    qDebug() << __PRETTY_FUNCTION__ << this;
    assert(op->getEventId() == cond->getEventId());
    auto eventId = op->getEventId();
    repopulateEventRelatedWidgets(eventId);
#else
    (void) op;
    (void) cond;
#endif
}

void AnalysisWidgetPrivate::onConditionLinkRemoved(const OperatorPtr &op, const ConditionPtr &cond)
{
#if 0
    qDebug() << __PRETTY_FUNCTION__ << this;
    assert(op->getEventId() == cond->getEventId());
    auto eventId = op->getEventId();
    repopulateEventRelatedWidgets(eventId);
#else
    (void) op;
    (void) cond;
#endif
}

void AnalysisWidgetPrivate::editConditionLinkGraphically(const ConditionPtr &cond)
{
    (void) cond;
#if 1
    qDebug() << __PRETTY_FUNCTION__ << this;
    if (!cond) return;

    /* Get the input pipes of the conditon
     * Find a sink displaying all of the pipes and the cl subindex.
     * Find an active window for the sink or open a new one.
     * Tell the window that we want to edit the condition.
     * For now error out if no sink accumulating the pipes can be found. */

    auto sinks = get_sinks_for_condition(cond, getAnalysis()->getSinkOperators<SinkPtr>());
    auto widgetRegistry = getServiceProvider()->getWidgetRegistry();

    // Try to use an existing window to edit the condition
    for (const auto &sink: sinks)
    {
        auto widget = widgetRegistry->getObjectWidget(sink.get());

        if (auto condEditor = qobject_cast<ConditionEditorInterface *>(widget))
        {
            if (condEditor->setEditCondition(cond))
            {
                condEditor->beginEditCondition();
            }
            show_and_activate(widget);
            return;
        }
    }

    // Create a new window
    for (const auto &sink: sinks)
    {
        auto widget = std::unique_ptr<QWidget>(sink_widget_factory(sink, getServiceProvider()));

        if (auto condEditor = qobject_cast<ConditionEditorInterface *>(widget.get()))
        {
            auto raw = widget.get();
            widgetRegistry->addObjectWidget(widget.release(), sink.get(), sink->getId().toString());
            if (condEditor->setEditCondition(cond))
            {
                condEditor->beginEditCondition();
            }
            show_and_activate(raw);
            return;
        }
    }

    /* Two possible cases:
     * - no sinks matching the inputs of the condition link where found
     * - no cut editors could be found or created given the list of possible sinks
     * TODO: show message about having to create sink and display widget for
     * the condition inputs or even better: offer to create them.
     */

    if (sinks.isEmpty())
    {
        qDebug() << __PRETTY_FUNCTION__
            << "Error: no sinks found";
    }
    else
    {
        qDebug() << __PRETTY_FUNCTION__
            << "Error: no viable editor widget could be found or created";
    }

    //InvalidCodePath;
#endif
}

void AnalysisWidgetPrivate::repopulateEventRelatedWidgets(const QUuid &eventId)
{
    qDebug() << __PRETTY_FUNCTION__ << this << eventId;
    m_eventWidget->repopulate();
#if 0
    m_conditionWidget->repopulate(eventId);
#endif
}

void AnalysisWidgetPrivate::repopulate()
{
    if (!m_repopEnabled) return;

    clear_stacked_widget(m_eventWidgetEventSelectAreaToolBarStack);
    clear_stacked_widget(m_eventWidgetToolBarStack);

    m_eventWidget->deleteLater();
    m_eventWidget = nullptr;
    auto eventWidget = new EventWidget(getServiceProvider(), m_q);

#if 0
    auto condWidget = m_conditionWidget;

    QObject::connect(condWidget, &ConditionWidget::conditionLinkSelected,
                     eventWidget, &EventWidget::onConditionLinkSelected);

    QObject::connect(condWidget, &ConditionWidget::applyConditionAccept,
                     eventWidget, &EventWidget::applyConditionAccept);

    QObject::connect(condWidget, &ConditionWidget::applyConditionReject,
                     eventWidget, &EventWidget::applyConditionReject);

    QObject::connect(eventWidget, &EventWidget::objectSelected,
                     m_q, [this] (const analysis::AnalysisObjectPtr &obj) {

         ConditionLink cl;

         if (auto op = std::dynamic_pointer_cast<OperatorInterface>(obj))
         {
             cl = getAnalysis()->getConditionLink(op);
         }

         if (cl)
         {
             m_conditionWidget->highlightConditionLink(cl);
         }
         else
         {
             m_conditionWidget->clearTreeHighlights();
         }
    });

    QObject::connect(eventWidget, &EventWidget::nonObjectNodeSelected,
                     m_q, [this] (const QTreeWidgetItem *) {
        m_conditionWidget->clearTreeHighlights();
    });

    QObject::connect(eventWidget, &EventWidget::conditionLinksModified,
                     m_conditionWidget, &ConditionWidget::setModificationButtonsVisible);
#endif

    m_eventWidgetToolBarStack->addWidget(eventWidget->getToolBar());
    m_eventWidgetEventSelectAreaToolBarStack->addWidget(eventWidget->getEventSelectAreaToolBar());
    m_eventWidget = eventWidget;

    if (!m_eventWidgetScrollArea)
    {
        m_eventWidgetScrollArea = new QScrollArea;
        m_eventWidgetScrollArea->setWidgetResizable(true);
        m_eventWidgetFrame->layout()->addWidget(m_eventWidgetScrollArea);
    }

    m_eventWidgetScrollArea->setWidget(m_eventWidget);

#if 0
    m_conditionWidget->repopulate();
#endif
    updateWindowTitle();
    updateAddRemoveUserLevelButtons();
}

void AnalysisWidgetPrivate::doPeriodicUpdate()
{
    m_eventWidget->m_d->doPeriodicUpdate();

#if 0
    m_conditionWidget->doPeriodicUpdate();
#endif
    m_objectInfoWidget->refresh();
}

void AnalysisWidgetPrivate::closeAllUniqueWidgets()
{
    if (m_eventWidget->m_d->m_uniqueWidget)
    {
        m_eventWidget->m_d->m_uniqueWidget->close();
        m_eventWidget->uniqueWidgetCloses();
    }
}

/* Close any open histograms and other sink widgets belonging to the current
 * analysis. */
void AnalysisWidgetPrivate::closeAllSinkWidgets()
{
    auto close_if_not_null = [](QWidget *widget)
    {
        if (widget)
            widget->close();
    };

    auto widgetRegistry = m_serviceProvider->getWidgetRegistry();

    for (const auto &op: m_serviceProvider->getAnalysis()->getOperators())
    {
        if (auto sink = qobject_cast<Histo1DSink *>(op.get()))
        {
            close_if_not_null(widgetRegistry->getObjectWidget(sink));

            for (const auto &histoPtr: sink->m_histos)
            {
                close_if_not_null(widgetRegistry->getObjectWidget(histoPtr.get()));
            }
        }
        else if (auto sink = qobject_cast<SinkInterface *>(op.get()))
        {
            close_if_not_null(widgetRegistry->getObjectWidget(sink));
        }
    }
}

void AnalysisWidgetPrivate::actionNew()
{
    if (!gui_analysis_maybe_save_if_modified(m_serviceProvider).first)
        return;

    /* Close any active unique widgets _before_ replacing the analysis as the
     * unique widgets might perform actions on the analysis in their reject()
     * code. */
    closeAllUniqueWidgets();
    closeAllSinkWidgets();

    AnalysisPauser pauser(m_serviceProvider);
    m_serviceProvider->getAnalysis()->clear();
    m_serviceProvider->getAnalysis()->setModified(false);
    m_serviceProvider->setAnalysisConfigFilename(QString());
    m_serviceProvider->analysisWasCleared();
    repopulate();
}

void AnalysisWidgetPrivate::actionOpen()
{
    auto path = m_serviceProvider->getWorkspaceDirectory();

    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getOpenFileName(
        m_q, QSL("Load analysis config"), path, AnalysisFileFilter);

    if (fileName.isEmpty())
        return;

    if (!gui_analysis_maybe_save_if_modified(m_serviceProvider).first)
        return;

    closeAllUniqueWidgets();
    closeAllSinkWidgets();

    m_serviceProvider->loadAnalysisConfig(fileName);
}

QPair<bool, QString> AnalysisWidgetPrivate::actionSave()
{
    QString fileName = m_serviceProvider->getAnalysisConfigFilename();

    if (fileName.isEmpty())
    {
        return actionSaveAs();
    }
    else
    {
        auto result = gui_save_analysis_config(m_serviceProvider->getAnalysis(), fileName,
                                         m_serviceProvider->getWorkspaceDirectory(),
                                         AnalysisFileFilter,
                                         m_serviceProvider);
        if (result.first)
        {
            m_serviceProvider->setAnalysisConfigFilename(result.second);
            m_serviceProvider->getAnalysis()->setModified(false);
            m_serviceProvider->analysisWasSaved();
        }

        return result;
    }
}

QPair<bool, QString> AnalysisWidgetPrivate::actionSaveAs()
{
    auto path = m_serviceProvider->getWorkspaceDirectory();
    auto filename = m_serviceProvider->getAnalysisConfigFilename();

    if (filename.isEmpty())
    {
        if (m_serviceProvider->getGlobalMode() == GlobalMode::ListFile)
        {
            // Use the listfile basename to suggest a filename.
            const auto &replayHandle = m_serviceProvider->getReplayFileHandle();
            path += "/" +  QFileInfo(replayHandle.listfileFilename).baseName() + ".analysis";
        }
        else
        {
            // Use the last part of the workspace path to suggest a filename.
            auto filename = m_serviceProvider->getAnalysisConfigFilename();
            if (filename.isEmpty())
                filename = QFileInfo(m_serviceProvider->getWorkspaceDirectory()).fileName() + ".analysis";
            path += "/" + filename;
        }
    }
    else
    {
        path += "/" + filename;
    }

    auto result = gui_save_analysis_config_as(m_serviceProvider->getAnalysis(),
                                       path,
                                       AnalysisFileFilter,
                                       m_serviceProvider);

    if (result.first)
    {
        m_serviceProvider->setAnalysisConfigFilename(result.second);
        m_serviceProvider->getAnalysis()->setModified(false);
    }

    return result;
}

void AnalysisWidgetPrivate::actionClearHistograms()
{
    AnalysisPauser pauser(m_serviceProvider);

    for (auto &op: m_serviceProvider->getAnalysis()->getOperators())
    {
        if (auto histoSink = qobject_cast<Histo1DSink *>(op.get()))
        {
            for (auto &histo: histoSink->m_histos)
            {
                histo->clear();
            }
        }
        else if (auto histoSink = qobject_cast<Histo2DSink *>(op.get()))
        {
            if (histoSink->m_histo)
            {
                histoSink->m_histo->clear();
            }
        }
    }
}

void handle_session_error(const QString &title, const QString &message)
{
    SessionErrorDialog dialog(title, message);
    dialog.exec();

    //m_serviceProvider->logMessage(QString("Error saving session:"));
    //m_serviceProvider->logMessageRaw(result.second);
}

void AnalysisWidgetPrivate::actionSaveSession()
{
    qDebug() << __PRETTY_FUNCTION__;

    using ResultType = QPair<bool, QString>;

    ResultType result;

    auto sessionPath = m_serviceProvider->getWorkspacePath(QSL("SessionDirectory"));

    if (sessionPath.isEmpty())
    {
        sessionPath = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString filename = QFileDialog::getSaveFileName(
        m_q, QSL("Save session"), sessionPath, SessionFileFilter);

    if (filename.isEmpty())
        return;

    QFileInfo fileInfo(filename);

    if (fileInfo.completeSuffix().isEmpty())
    {
        filename += SessionFileExtension;
    }

    AnalysisPauser pauser(m_serviceProvider);

#if 1 // The QtConcurrent path
    QProgressDialog progressDialog;
    progressDialog.setLabelText(QSL("Saving session..."));
    progressDialog.setMinimum(0);
    progressDialog.setMaximum(0);

    QFutureWatcher<ResultType> watcher;
    QObject::connect(&watcher, &QFutureWatcher<ResultType>::finished,
                     &progressDialog, &QDialog::close);

    QFuture<ResultType> future = QtConcurrent::run(save_analysis_session, filename,
                                                   m_serviceProvider->getAnalysis());
    watcher.setFuture(future);

    progressDialog.exec();

    result = future.result();
#else // The blocking path
    result = save_analysis_session(filename, m_serviceProvider->getAnalysis());
#endif

    if (!result.first)
    {
        handle_session_error(result.second, "Error saving session");
    }
}

void AnalysisWidgetPrivate::actionLoadSession()
{
    auto sessionPath = m_serviceProvider->getWorkspacePath(QSL("SessionDirectory"));

    if (sessionPath.isEmpty())
    {
        sessionPath = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString filename = QFileDialog::getOpenFileName(
        m_q, QSL("Load session"), sessionPath, SessionFileFilter);

    if (filename.isEmpty())
        return;

    AnalysisPauser pauser(m_serviceProvider);

    QProgressDialog progressDialog;
    progressDialog.setLabelText(QSL("Loading session config..."));
    progressDialog.setMinimum(0);
    progressDialog.setMaximum(0);
    progressDialog.show();

    QEventLoop loop;

    QJsonDocument analysisJson;

    // load the config first
    {
#if 1 // The QtConcurrent path
        using ResultType = QPair<QJsonDocument, QString>;

        QFutureWatcher<ResultType> watcher;
        QObject::connect(&watcher, &QFutureWatcher<ResultType>::finished, &loop, &QEventLoop::quit);

        QFuture<ResultType> future = QtConcurrent::run(load_analysis_config_from_session_file, filename);
        watcher.setFuture(future);

        loop.exec();

        auto result = future.result();
#else // The blocking path
        auto result = load_analysis_config_from_session_file(filename);
#endif

        if (result.first.isNull())
        {
            progressDialog.hide();

            handle_session_error(result.second, "Error loading session config");

            //m_serviceProvider->logMessage(QString("Error loading session:"));
            //m_serviceProvider->logMessageRaw(result.second);
            return;
        }

        analysisJson = QJsonDocument(result.first);
    }

    if (!gui_analysis_maybe_save_if_modified(m_serviceProvider).first)
        return;

    // This is the standard procedure when loading an analysis config
    closeAllUniqueWidgets();
    closeAllSinkWidgets();

    if (m_serviceProvider->loadAnalysisConfig(analysisJson, filename, { .NoAutoResume = true }))
    {
        m_serviceProvider->setAnalysisConfigFilename(QString());
        progressDialog.setLabelText(QSL("Loading session data..."));


#if 1 // The QtConcurrent path
        using ResultType = QPair<bool, QString>;

        QFutureWatcher<ResultType> watcher;
        QObject::connect(&watcher, &QFutureWatcher<ResultType>::finished, &loop, &QEventLoop::quit);

        QFuture<ResultType> future = QtConcurrent::run(load_analysis_session, filename,
                                                       m_serviceProvider->getAnalysis());
        watcher.setFuture(future);

        loop.exec();

        auto result = future.result();
#else // The blocking path
        auto result = load_analysis_session(filename, m_serviceProvider->getAnalysis());
#endif

        if (!result.first)
        {
            handle_session_error(result.second, "Error loading session data");
            return;
        }
    }
}

void AnalysisWidgetPrivate::updateActions()
{
    auto streamWorker = m_serviceProvider->getMVMEStreamWorker();
    auto workerState = streamWorker->getState();

    qDebug() << __PRETTY_FUNCTION__ << to_string(workerState);

    switch (workerState)
    {
        case AnalysisWorkerState::Idle:
            m_actionPause->setIcon(QIcon(":/control_pause.png"));
            m_actionPause->setText(QSL("Pause"));
            m_actionPause->setEnabled(true);
            m_actionPause->setChecked(streamWorker->getStartPaused());
            m_actionStepNextEvent->setEnabled(false);
            break;

        case AnalysisWorkerState::Running:
            m_actionPause->setIcon(QIcon(":/control_pause.png"));
            m_actionPause->setText(QSL("Pause"));
            m_actionPause->setEnabled(true);
            m_actionPause->setChecked(false);
            m_actionStepNextEvent->setEnabled(false);
            break;

        case AnalysisWorkerState::Paused:
            m_actionPause->setIcon(QIcon(":/control_play.png"));
            m_actionPause->setText(QSL("Resume"));
            m_actionPause->setEnabled(true);
            m_actionPause->setChecked(true);
            m_actionStepNextEvent->setEnabled(true);
            break;

        case AnalysisWorkerState::SingleStepping:
            m_actionPause->setEnabled(false);
            m_actionStepNextEvent->setEnabled(false);
            break;
    }
}

void AnalysisWidgetPrivate::actionExploreWorkspace()
{
    QString path = m_serviceProvider->getWorkspaceDirectory();

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void AnalysisWidgetPrivate::actionPause(bool actionIsChecked)
{
    auto streamWorker = m_serviceProvider->getMVMEStreamWorker();
    auto workerState = streamWorker->getState();

    switch (workerState)
    {
        case AnalysisWorkerState::Idle:
            streamWorker->setStartPaused(actionIsChecked);
            break;

        case AnalysisWorkerState::Running:
            streamWorker->pause();
            break;

        case AnalysisWorkerState::Paused:
            streamWorker->resume();
            break;

        case AnalysisWorkerState::SingleStepping:
            // cannot pause/resume during the time a singlestep is active
            InvalidCodePath;
            break;
    }
}

void AnalysisWidgetPrivate::actionStepNextEvent()
{
    auto streamWorker = m_serviceProvider->getMVMEStreamWorker();
    auto workerState = streamWorker->getState();

    switch (workerState)
    {
        case AnalysisWorkerState::Idle:
        case AnalysisWorkerState::Running:
        case AnalysisWorkerState::SingleStepping:
            InvalidCodePath;
            break;

        case AnalysisWorkerState::Paused:
            streamWorker->singleStep();
            break;
    }
}

void AnalysisWidgetPrivate::updateWindowTitle()
{
    QString fileName = m_serviceProvider->getAnalysisConfigFilename();

    if (fileName.isEmpty())
        fileName = QSL("<not saved>");

    auto wsDir = m_serviceProvider->getWorkspaceDirectory() + '/';

    if (fileName.startsWith(wsDir))
        fileName.remove(wsDir);

    auto title = QString(QSL("%1 - [Analysis UI]")).arg(fileName);

    if (m_serviceProvider->getAnalysis()->isModified())
    {
        title += " *";
    }

    m_q->setWindowTitle(title);
}

void AnalysisWidgetPrivate::updateAddRemoveUserLevelButtons()
{
    qDebug() << __PRETTY_FUNCTION__;

    auto analysis = m_serviceProvider->getAnalysis();
    s32 maxUserLevel = 0;

    for (const auto &op: analysis->getOperators())
        maxUserLevel = std::max(maxUserLevel, op->getUserLevel());

    s32 numUserLevels = maxUserLevel + 1;

    s32 visibleUserLevels = m_eventWidget->m_d->m_levelTrees.size();

    m_removeUserLevelButton->setEnabled(visibleUserLevels > 1 && visibleUserLevels > numUserLevels);
}

AnalysisWidget::AnalysisWidget(AnalysisServiceProvider *asp, QWidget *parent)
    : QWidget(parent)
    , m_d(new AnalysisWidgetPrivate)
{
    m_d->m_q = this;
    m_d->m_serviceProvider = asp;

    m_d->m_periodicUpdateTimer = new QTimer(this);
    m_d->m_periodicUpdateTimer->start(PeriodicUpdateTimerInterval_ms);
    m_d->m_geometrySaver = new WidgetGeometrySaver(this);

    /* Note: This code is not efficient at all. This AnalysisWidget and the
     * EventWidgets are recreated and repopulated more often than is really
     * necessary. Rebuilding everything when the underlying objects change was
     * just the easiest way to implement it.
     *
     * Update (beta 0.9.6): added new MVMEContext::vmeConfigAboutToBeSet()
     * signal. When this signal arrives we suspend repopulating until the final
     * emission of the vmeConfigChanged() signal. */

    connect(m_d->m_serviceProvider, &AnalysisServiceProvider::vmeConfigAboutToBeSet,
            this, [this] (VMEConfig *, VMEConfig *) {
                qDebug() << __PRETTY_FUNCTION__ << "disabling repops";
                m_d->m_repopEnabled = false;
            });

    connect(m_d->m_serviceProvider, &AnalysisServiceProvider::vmeConfigChanged,
            this, [this] (VMEConfig *) {
                qDebug() << __PRETTY_FUNCTION__ << "reenabling repops";
                m_d->m_repopEnabled = true;
                m_d->repopulate();
            });

    auto do_repopulate_lambda = [this]() { m_d->repopulate(); };

    // Individual VME config changes
    connect(m_d->m_serviceProvider, &AnalysisServiceProvider::eventAdded, this, do_repopulate_lambda);
    connect(m_d->m_serviceProvider, &AnalysisServiceProvider::eventAboutToBeRemoved, this, do_repopulate_lambda);
    connect(m_d->m_serviceProvider, &AnalysisServiceProvider::moduleAdded, this, do_repopulate_lambda);
    connect(m_d->m_serviceProvider, &AnalysisServiceProvider::moduleAboutToBeRemoved, this, do_repopulate_lambda);

    // Analysis changes
    connect(m_d->m_serviceProvider, &AnalysisServiceProvider::analysisChanged,
            this, [this] (Analysis *analysis) {
                m_d->onAnalysisChanged(analysis);
    });

    connect(m_d->m_serviceProvider, &AnalysisServiceProvider::analysisConfigFileNameChanged,
            this, [this](const QString &) {
                m_d->updateWindowTitle();
    });

    // QStackedWidgets for EventWidgets and their toolbars
    m_d->m_eventWidgetFrame = new QFrame;
    {
        auto l = new QHBoxLayout;
        l->setContentsMargins(0, 0, 0, 0);
        m_d->m_eventWidgetFrame->setLayout(l);
    }

    m_d->m_eventWidgetToolBarStack = new QStackedWidget;
    m_d->m_eventWidgetEventSelectAreaToolBarStack = new QStackedWidget;

    // object info widget
    {
        m_d->m_objectInfoWidget = new ObjectInfoWidget(m_d->m_serviceProvider);
    }

#if 0
    // condition/cut displays
    {
        m_d->m_conditionWidget = new ConditionWidget(m_d->m_serviceProvider);
        auto condWidget = m_d->m_conditionWidget;

        QObject::connect(condWidget, &ConditionWidget::objectSelected,
                         this, [this] (const AnalysisObjectPtr &) {

            m_d->m_eventWidget->m_d->clearAllTreeSelections();
        });

        QObject::connect(condWidget, &ConditionWidget::editCondition,
                         this, [this] (const ConditionLink &cl) {
            m_d->editConditionLinkGraphically(cl);
        });

        QObject::connect(condWidget, &ConditionWidget::objectSelected,
                         m_d->m_objectInfoWidget, &ObjectInfoWidget::setAnalysisObject);
    }
#endif

    // toolbar
    {
        m_d->m_toolbar = make_toolbar();

        // new, open, save, save as
        m_d->m_toolbar->addAction(QIcon(":/document-new.png"), QSL("New"),
                                  this, [this]() { m_d->actionNew(); });

        m_d->m_toolbar->addAction(QIcon(":/document-open.png"), QSL("Open"),
                                  this, [this]() { m_d->actionOpen(); });

        m_d->m_toolbar->addAction(QIcon(":/document-save.png"), QSL("Save"),
                                  this, [this]() { m_d->actionSave(); });

        m_d->m_toolbar->addAction(QIcon(":/document-save-as.png"), QSL("Save As"),
                                  this, [this]() { m_d->actionSaveAs(); });

        // clear histograms
        m_d->m_toolbar->addSeparator();
        m_d->m_toolbar->addAction(QIcon(":/clear_histos.png"), QSL("Clear Histos"),
                                  this, [this]() { m_d->actionClearHistograms(); });

        // info window
        m_d->m_toolbar->addSeparator();
        m_d->m_toolbar->addAction(QIcon(":/info.png"), QSL("Debug && Stats"), this, [this]() {

            AnalysisInfoWidget *widget = nullptr;

            if (m_d->m_analysisInfoWidget)
            {
                widget = m_d->m_analysisInfoWidget;
            }
            else
            {
                widget = new AnalysisInfoWidget(m_d->m_serviceProvider);
                widget->setAttribute(Qt::WA_DeleteOnClose);
                add_widget_close_action(widget);
                m_d->m_geometrySaver->addAndRestore(widget, QSL("WindowGeometries/AnalysisInfo"));

                connect(widget, &QObject::destroyed, this, [this]() {
                    m_d->m_analysisInfoWidget = nullptr;
                });

                m_d->m_analysisInfoWidget = widget;
            }

            show_and_activate(widget);
        });

        // pause, resume, step actions and MVLC parser debugging
        m_d->mvlcParserDebugHandler = new MVLCParserDebugHandler(this);

        auto logger = [this] (const QString &msg) { m_d->m_serviceProvider->logMessage(msg); };
        m_d->mvlcSingleStepHandler = new MVLCSingleStepHandler(logger, this);

        auto setup_parser_debug = [this] ()
        {
            connect(m_d->m_serviceProvider->getMVMEStreamWorker(), &StreamWorkerBase::stateChanged,
                    this, [this](AnalysisWorkerState) {
                        m_d->updateActions();
                    });

            // MVLC specific
            if (auto worker = qobject_cast<MVLC_StreamWorker *>(
                    m_d->m_serviceProvider->getMVMEStreamWorker()))
            {
                connect(worker, &MVLC_StreamWorker::debugInfoReady,
                        m_d->mvlcParserDebugHandler, &MVLCParserDebugHandler::handleDebugInfo);

                connect(worker, &MVLC_StreamWorker::singleStepResultReady,
                        m_d->mvlcSingleStepHandler, &MVLCSingleStepHandler::handleSingleStepResult);
            }
        };

        // Have to react to vmeControllerSet as that will change the
        // StreamWorkerBase instance used in MVMEContext. Thus the connection
        // to stateChanged() has to be remade and the test for an
        // MVLC_StreamWorker instance has to be done again.
        connect(m_d->m_serviceProvider, &AnalysisServiceProvider::vmeControllerSet,
                this, setup_parser_debug);

        setup_parser_debug();

        m_d->m_toolbar->addSeparator();
        m_d->m_actionPause = m_d->m_toolbar->addAction(
            QIcon(":/control_pause.png"), QSL("Pause"),
            this, [this](bool checked) { m_d->actionPause(checked); });

        m_d->m_actionPause->setCheckable(true);

        m_d->m_actionStepNextEvent = m_d->m_toolbar->addAction(
            QIcon(":/control_play_stop.png"), QSL("Next Event"),
            this, [this] { m_d->actionStepNextEvent(); });

        m_d->m_toolbar->addSeparator();
        m_d->m_toolbar->addAction(QIcon(":/document-open.png"), QSL("Load Session"),
                                  this, [this]() { m_d->actionLoadSession(); });
        m_d->m_toolbar->addAction(QIcon(":/document-save.png"), QSL("Save Session"),
                                  this, [this]() { m_d->actionSaveSession(); });

        m_d->m_toolbar->addSeparator();

        m_d->m_toolbar->addAction(QIcon(QSL(":/folder_orange.png")), QSL("Explore Workspace"),
                                  this, [this]() { m_d->actionExploreWorkspace(); });

        m_d->m_toolbar->addSeparator();

        m_d->m_toolbar->addAction(
            QIcon(QSL(":/help.png")), QSL("Help"),
            this, mesytec::mvme::make_help_keyword_handler("Analysis"));
    }

    // After the toolbar entries the EventWidget specific action will be added.
    // See EventWidget::makeToolBar()

    auto toolbarFrame = new QFrame;
    toolbarFrame->setFrameStyle(QFrame::StyledPanel);
    auto toolbarFrameLayout = new QHBoxLayout(toolbarFrame);
    toolbarFrameLayout->setContentsMargins(0, 0, 0, 0);
    toolbarFrameLayout->setSpacing(0);
    toolbarFrameLayout->addWidget(m_d->m_toolbar);
    toolbarFrameLayout->addWidget(m_d->m_eventWidgetToolBarStack);
    toolbarFrameLayout->addStretch();

    // remove user level
    m_d->m_removeUserLevelButton = new QToolButton();
    m_d->m_removeUserLevelButton->setIcon(QIcon(QSL(":/list_remove.png")));
    connect(m_d->m_removeUserLevelButton, &QPushButton::clicked, this, [this]() {
        m_d->m_eventWidget->removeUserLevel();
        updateAddRemoveUserLevelButtons();
    });

    // add user level
    m_d->m_addUserLevelButton = new QToolButton();
    m_d->m_addUserLevelButton->setIcon(QIcon(QSL(":/list_add.png")));

    connect(m_d->m_addUserLevelButton, &QPushButton::clicked, this, [this]() {
        m_d->m_eventWidget->addUserLevel();
        updateAddRemoveUserLevelButtons();
    });

    // Layout containing a 2nd EventWidget specific toolbar and the add and
    // remove userlevel buttons.
    auto innerToolbarLayout = new QHBoxLayout;
    innerToolbarLayout->addWidget(m_d->m_eventWidgetEventSelectAreaToolBarStack);
    innerToolbarLayout->addStretch();
    innerToolbarLayout->addWidget(m_d->m_removeUserLevelButton);
    innerToolbarLayout->addWidget(m_d->m_addUserLevelButton);

    // statusbar
    m_d->m_statusBar = make_statusbar();

    // efficiency
    m_d->m_labelEfficiency = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_labelEfficiency);

    // original data rate
    m_d->m_labelOriginalDataRate = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_labelOriginalDataRate);

    // timeticks label
    m_d->m_labelTimetickCount = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_labelTimetickCount);

    // histo storage label
    m_d->m_labelSinkStorageSize = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_labelSinkStorageSize);

    // a2 label
    m_d->m_statusLabelA2 = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_statusLabelA2);

    m_d->m_statusLabelA2->setText(QSL("a2::"));


    auto centralWidget = new QWidget;
    auto centralLayout = new QVBoxLayout(centralWidget);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(2);
    centralLayout->addLayout(innerToolbarLayout);
    centralLayout->addWidget(m_d->m_eventWidgetFrame);
    centralLayout->setStretch(1, 1);

#if 0
    auto conditionsTabWidget = new QTabWidget;
    conditionsTabWidget->addTab(m_d->m_conditionWidget,
                                QIcon(QSL(":/scissors.png")),
                                QSL("Cuts/Conditions"));
#endif

    // Object info inside a scrollarea in the bottom right corner
    auto objectInfoTabWidget = new QTabWidget;

    {
        auto scrollArea = new QScrollArea;
        scrollArea->setWidgetResizable(true);
        scrollArea->setWidget(m_d->m_objectInfoWidget);

        objectInfoTabWidget->addTab(scrollArea,
                                    QIcon(QSL(":/info.png")),
                                    QSL("Object Info"));
    }

    // right splitter with condition tree on top and object info window at the
    // bottom
    auto rightSplitter = new QSplitter(Qt::Vertical);
    //rightSplitter->addWidget(conditionsTabWidget);
    rightSplitter->addWidget(objectInfoTabWidget);
    rightSplitter->setStretchFactor(0, 2);
    rightSplitter->setStretchFactor(1, 1);

    static const char *rightSplitterStateKey = "AnalysisWidget/RightSplitterState";

    connect(rightSplitter, &QSplitter::splitterMoved,
            this, [this, rightSplitter] (int pos, int index)
            {
                (void) pos;
                (void) index;
                m_d->m_settings.setValue(rightSplitterStateKey, rightSplitter->saveState());
            });


    if (m_d->m_settings.contains(rightSplitterStateKey))
    {
        rightSplitter->restoreState(m_d->m_settings.value(rightSplitterStateKey).toByteArray());
    }

    // main splitter dividing the ui into userlevel trees (left) and conditions
    // and object info (right)
    auto mainSplitter = new QSplitter;
    mainSplitter->addWidget(centralWidget);
    mainSplitter->addWidget(rightSplitter);
    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 1);

    static const char *mainSplitterStateKey = "AnalysisWidget/MainSplitterState";

    connect(mainSplitter, &QSplitter::splitterMoved,
            this, [this, mainSplitter] (int pos, int index)
            {
                (void) pos;
                (void) index;
                m_d->m_settings.setValue(mainSplitterStateKey, mainSplitter->saveState());
            });


    if (m_d->m_settings.contains(mainSplitterStateKey))
    {
        mainSplitter->restoreState(m_d->m_settings.value(mainSplitterStateKey).toByteArray());
    }

    // main layout
    auto layout = new QGridLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    layout->addWidget(toolbarFrame,             0, 0);
    layout->addWidget(mainSplitter,             1, 0);
    layout->addWidget(m_d->m_statusBar,         2, 0);
    layout->setRowStretch(1, 1);

    // Update the histo storage size in the statusbar
    connect(m_d->m_periodicUpdateTimer, &QTimer::timeout, this, [this]() {
        double storageSize = m_d->m_serviceProvider->getAnalysis()->getTotalSinkStorageSize();
        QString unit("B");

        if (storageSize > Gigabytes(1))
        {
            storageSize /= Gigabytes(1);
            unit = QSL("GiB");
        }
        else if (storageSize > Megabytes(1))
        {
            storageSize /= Megabytes(1);
            unit = QSL("MiB");
        }
        else if (storageSize == 0.0)
        {
            unit = QSL("MiB");
        }

        m_d->m_labelSinkStorageSize->setText(QString("Histo Storage: %1 %2")
                                             .arg(storageSize, 0, 'f', 2)
                                             .arg(unit));
    });

    // Update statusbar timeticks label
    connect(m_d->m_periodicUpdateTimer, &QTimer::timeout, this, [this]() {

        double tickCount = m_d->m_serviceProvider->getAnalysis()->getTimetickCount();

        m_d->m_labelTimetickCount->setText(QString("Timeticks: %1 s")
                                           .arg(tickCount));


        if (!m_d->m_serviceProvider->getAnalysis()->getRunInfo().isReplay)
        {

            auto daqStats = m_d->m_serviceProvider->getDAQStats();
            double efficiency = daqStats.getAnalysisEfficiency();
            efficiency = std::isnan(efficiency) ? 0.0 : efficiency;

            m_d->m_labelEfficiency->setText(QString("Efficiency: %1")
                                            .arg(efficiency, 0, 'f', 2));

            auto tt = (QString("Analyzed Buffers:\t%1\n"
                               "Skipped Buffers:\t%2\n"
                               "Total Buffers:\t%3")
                       .arg(daqStats.getAnalyzedBuffers())
                       .arg(daqStats.droppedBuffers)
                       .arg(daqStats.totalBuffersRead)
                      );

            m_d->m_labelEfficiency->setToolTip(tt);
        }
        else
        {
            m_d->m_labelEfficiency->setText(QSL("Replay  |"));
            m_d->m_labelEfficiency->setToolTip(QSL(""));
        }
    });

    // Update the "DAQ Data Rate" label
    connect(m_d->m_periodicUpdateTimer, &QTimer::timeout, this, [this]() {

        if (m_d->m_serviceProvider->getAnalysis()->getRunInfo().isReplay)
        {
            if (auto streamWorker = m_d->m_serviceProvider->getMVMEStreamWorker())
            {
                auto streamCounters = streamWorker->getCounters();
                double timetickCount = m_d->m_serviceProvider->getAnalysis()->getTimetickCount();
                timetickCount = std::max(timetickCount, 1.0);
                auto bytesProcessed = streamCounters.bytesProcessed;
                double rate = bytesProcessed / timetickCount;
                auto str = (QSL("DAQ Data Rate: %1")
                            .arg(format_number(rate, "B/s", UnitScaling::Binary, 0, 'g', 4))
                            );
                m_d->m_labelOriginalDataRate->setText(str);
            }
        }
        else
        {
            m_d->m_labelOriginalDataRate->clear();
        }
    });

    // Run the periodic update
    connect(m_d->m_periodicUpdateTimer, &QTimer::timeout,
            this, [this]() { m_d->doPeriodicUpdate(); });

    // Build the analysis to make sure everything is setup properly
    auto analysis = m_d->m_serviceProvider->getAnalysis();

    analysis->beginRun(m_d->m_serviceProvider->getRunInfo(), m_d->m_serviceProvider->getVMEConfig());

    // React to changes to the analysis but using the local signal wrapper
    // instead of the analysis directly.
    auto &wrapper = m_d->m_analysisSignalWrapper;
    using Wrapper = AnalysisSignalWrapper;

    QObject::connect(&wrapper, &Wrapper::modifiedChanged,
                     this, [this]() {
        m_d->updateWindowTitle();
    });

    QObject::connect(&wrapper, &Wrapper::dataSourceAdded,
                     this, [this](const SourcePtr &src) {
        m_d->onDataSourceAdded(src);
    });

    QObject::connect(&wrapper, &Wrapper::dataSourceRemoved,
                     this, [this](const SourcePtr &src) {
        m_d->onDataSourceRemoved(src);
    });

    QObject::connect(&wrapper, &Wrapper::operatorAdded,
                     this, [this](const OperatorPtr &op) {
        m_d->onOperatorAdded(op);
    });

    QObject::connect(&wrapper, &Wrapper::operatorRemoved,
                     this, [this](const OperatorPtr &op) {
        m_d->onOperatorRemoved(op);
    });

    QObject::connect(&wrapper, &Wrapper::directoryAdded,
                     this, [this](const DirectoryPtr &dir) {
        m_d->onDirectoryAdded(dir);
    });

    QObject::connect(&wrapper, &Wrapper::directoryRemoved,
                     this, [this](const DirectoryPtr &dir) {
        m_d->onDirectoryRemoved(dir);
    });

    QObject::connect(&wrapper, &Wrapper::conditionLinkAdded,
                     this, [this](const OperatorPtr &op, const ConditionPtr &cond) {
         m_d->onConditionLinkAdded(op, cond);
    });

    QObject::connect(&wrapper, &Wrapper::conditionLinkRemoved,
                     this, [this](const OperatorPtr &op, const ConditionPtr &cond) {
         m_d->onConditionLinkRemoved(op, cond);
    });

    // Initial update
    m_d->onAnalysisChanged(analysis);
    m_d->updateActions();

    resize(800, 600);
}

AnalysisWidget::~AnalysisWidget()
{
    if (m_d->m_analysisInfoWidget)
    {
        m_d->m_analysisInfoWidget->close();
    }

    delete m_d;
    qDebug() << __PRETTY_FUNCTION__;
}

void AnalysisWidget::operatorAddedExternally(const OperatorPtr &/*op*/)
{
    m_d->m_eventWidget->m_d->repopulate();
#if 0
    m_d->m_conditionWidget->repopulate();
#endif
}

void AnalysisWidget::operatorEditedExternally(const OperatorPtr &/*op*/)
{
    m_d->m_eventWidget->m_d->repopulate();
#if 0
    m_d->m_conditionWidget->repopulate();
#endif
}

void AnalysisWidget::updateAddRemoveUserLevelButtons()
{
    m_d->updateAddRemoveUserLevelButtons();
}

#if 0
ConditionWidget *AnalysisWidget::getConditionWidget() const
{
    return m_d->m_conditionWidget;
}
#endif

ObjectInfoWidget *AnalysisWidget::getObjectInfoWidget() const
{
    return m_d->m_objectInfoWidget;
}

void AnalysisWidget::eventConfigModified()
{
}

bool AnalysisWidget::event(QEvent *e)
{
    if (e->type() == QEvent::StatusTip)
    {
        m_d->m_statusBar->showMessage(reinterpret_cast<QStatusTipEvent *>(e)->tip());
        return true;
    }

    return QWidget::event(e);
}

int AnalysisWidget::removeObjects(const AnalysisObjectVector &objects)
{
    qDebug() << __PRETTY_FUNCTION__ << objects.size();

    if (objects.isEmpty()) return 0;

    AnalysisPauser pauser(m_d->m_serviceProvider);
    QSignalBlocker blocker(m_d->m_analysisSignalWrapper);
    auto analysis = m_d->getAnalysis();
    int result = analysis->removeObjectsRecursively(objects);

    m_d->m_eventWidget->repopulate();
    return result;
}

void AnalysisWidget::repopulate()
{
    m_d->repopulate();
}

} // end namespace ui
} // end namespace analysis
