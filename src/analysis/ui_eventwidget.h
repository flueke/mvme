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
#ifndef __MVME_ANALYSIS_UI_EVENTWIDGET_H__
#define __MVME_ANALYSIS_UI_EVENTWIDGET_H__

#include "analysis/analysis_fwd.h"
#include "run_info.h"
#include "typedefs.h"
#include "vme_config_fwd.h"

#include <functional>
#include <QWidget>

class MVMEContext;

class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace analysis
{
namespace ui
{

using Slot = analysis::Slot;

class AnalysisWidget;
struct EventWidgetPrivate;

class EventWidget: public QWidget
{
    Q_OBJECT
    signals:
        void objectSelected(const analysis::AnalysisObjectPtr &obj);
        void nonObjectNodeSelected(QTreeWidgetItem *node);
        void conditionLinksModified(const ConditionLink &cl, bool modified);

    public:

        using SelectInputCallback = std::function<void (Slot *destSlot,
                                                        Pipe *sourcePipe,
                                                        s32 sourceParamIndex)>;

        EventWidget(MVMEContext *ctx, AnalysisWidget *analysisWidget, QWidget *parent = 0);
        virtual ~EventWidget();

        void selectInputFor(Slot *slot, s32 userLevel, SelectInputCallback callback,
                            QSet<PipeSourceInterface *> additionalInvalidSources = {});
        void endSelectInput();
        void highlightInputOf(Slot *slot, bool doHighlight);

        void addSource(SourcePtr src, ModuleConfig *module, bool addHistogramsAndCalibration,
                       const QString &unit = QString(), double unitMin = 0.0, double unitMax = 0.0);
        void sourceEdited(SourceInterface *src);
        void removeSource(SourceInterface *src);

        void removeOperator(OperatorInterface *op);

        void uniqueWidgetCloses();
        void addUserLevel();
        void removeUserLevel();
        void toggleSinkEnabled(SinkInterface *sink);
        void repopulate();
        QToolBar *getToolBar();
        QToolBar *getEventSelectAreaToolBar();

        MVMEContext *getContext() const;
        AnalysisWidget *getAnalysisWidget() const;
        Analysis *getAnalysis() const;
        RunInfo getRunInfo() const;
        VMEConfig *getVMEConfig() const;
        QTreeWidgetItem *findNode(const AnalysisObjectPtr &obj);

        friend class AnalysisWidget;
        friend struct AnalysisWidgetPrivate;

        void selectObjects(const AnalysisObjectVector &objects);
        AnalysisObjectVector getAllSelectedObjects() const;
        AnalysisObjectVector getTopLevelSelectedObjects() const;
        void copyToClipboard(const AnalysisObjectVector &objects);
        void pasteFromClipboard(QTreeWidget *destTree);

    public slots:
        void objectEditorDialogApplied();
        void objectEditorDialogAccepted();
        void objectEditorDialogRejected();

        void onConditionLinkSelected(const ConditionLink &cl);
        void applyConditionAccept();
        void applyConditionReject();

    private:
        std::unique_ptr<EventWidgetPrivate> m_d;
};

} // end namespace ui
} // end namespace analysis

#endif /* __MVME_ANALYSIS_UI_EVENTWIDGET_H__ */
