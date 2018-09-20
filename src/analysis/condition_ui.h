#ifndef __MVME_CONDITION_UI_H__
#define __MVME_CONDITION_UI_H__

class MVMEContext;

#include <memory>
#include <QTreeWidget>

#include "analysis_fwd.h"

namespace analysis
{
namespace ui
{

class ConditionTreeWidget: public QTreeWidget
{
    Q_OBJECT
    signals:
        void applyConditionAccept();
        void applyConditionReject();

    public:
        ConditionTreeWidget(MVMEContext *ctx, const QUuid &eventId, int eventIndex,
                            QWidget *parent = nullptr);
        virtual ~ConditionTreeWidget() override;

        void repopulate();
        void doPeriodicUpdate();

        void highlightConditionLink(const ConditionLink &cl);
        void clearHighlights();
        void setModificationButtonsVisible(const ConditionLink &cl, bool visible);

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

class ConditionWidget: public QWidget
{
    Q_OBJECT
    signals:
        void conditionLinkSelected(const ConditionLink &cl);
        void applyConditionAccept();
        void applyConditionReject();
        void objectSelected(const AnalysisObjectPtr &obj);

    public:
        ConditionWidget(MVMEContext *ctx, QWidget *parent = nullptr);
        virtual ~ConditionWidget() override;

    public slots:
        void repopulate();
        void repopulate(int eventIndex);
        void repopulate(const QUuid &eventId);
        void doPeriodicUpdate();

        void selectEvent(int eventIndex);
        void selectEventById(const QUuid &eventId);
        void clearTreeSelections();
        void clearTreeHighlights();

        void highlightConditionLink(const ConditionLink &cl);
        void setModificationButtonsVisible(const ConditionLink &cl, bool visible);


    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // ns ui
} // ns analysis

#endif /* __MVME_CONDITION_UI_H__ */
