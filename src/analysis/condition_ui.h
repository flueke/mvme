#ifndef __MVME_CONDITION_UI_H__
#define __MVME_CONDITION_UI_H__

class MVMEContext;

#include <memory>
#include <QTreeWidget>

#include "analysis_fwd.h"

namespace analysis
{

class ConditionInterface;

class ConditionTreeWidget: public QTreeWidget
{
    Q_OBJECT
    public:
        ConditionTreeWidget(MVMEContext *ctx, const QUuid &eventId, int eventIndex,
                            QWidget *parent = nullptr);

        virtual ~ConditionTreeWidget() override;

        void repopulate();
        void doPeriodicUpdate();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

class ConditionWidget: public QWidget
{
    Q_OBJECT
    signals:
        void applyConditionBegin(ConditionInterface *cond, int bitIndex);
        void applyConditionAccept();
        void applyConditionReject();
        void objectSelected(const AnalysisObjectPtr &obj);

    public:
        ConditionWidget(MVMEContext *ctx, QWidget *parent = nullptr);

        virtual ~ConditionWidget() override;

        void repopulate();
        void repopulate(int eventIndex);
        void repopulate(const QUuid &eventId);
        void doPeriodicUpdate();

    public slots:
        void selectEvent(int eventIndex);
        void selectEventById(const QUuid &eventId);
        void clearTreeSelections();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

}

#endif /* __MVME_CONDITION_UI_H__ */
