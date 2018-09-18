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

    public:

        using SelectInputCallback = std::function<void (Slot *destSlot,
                                                        Pipe *sourcePipe,
                                                        s32 sourceParamIndex)>;

        EventWidget(MVMEContext *ctx, const QUuid &eventId, int eventIndex,
                    AnalysisWidget *analysisWidget, QWidget *parent = 0);
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
        friend class AnalysisWidgetPrivate;

        QUuid getEventId() const;

        void selectObjects(const AnalysisObjectVector &objects);
        AnalysisObjectVector getAllSelectedObjects() const;
        AnalysisObjectVector getTopLevelSelectedObjects() const;
        void copyToClipboard(const AnalysisObjectVector &objects);
        void pasteFromClipboard(QTreeWidget *destTree);

    public slots:
        void objectEditorDialogApplied();
        void objectEditorDialogAccepted();
        void objectEditorDialogRejected();

        void conditionLinkSelected(const ConditionLink &cl);
        void applyConditionBegin(const ConditionLink &cl);
        void applyConditionAccept();
        void applyConditionReject();

    private:
        std::unique_ptr<EventWidgetPrivate> m_d;
};

} // end namespace ui
} // end namespace analysis

#endif /* __MVME_ANALYSIS_UI_EVENTWIDGET_H__ */
