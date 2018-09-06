#ifndef __MVME_CONDITION_TREE_H__
#define __MVME_CONDITION_TREE_H__

#include "analysis_ui_p.h"

namespace analysis
{


class ConditionTreeWidget: public QTreeWidget
{
    Q_OBJECT
    public:
        ConditionTreeWidget(EventWidget *eventWidget, QWidget *parent = nullptr);
        virtual ~ConditionTreeWidget() override;

        void repopulate();
        void doPeriodicUpdate();

    private:
        struct Private;

        std::unique_ptr<Private> m_d;
};

}

#endif /* __MVME_CONDITION_TREE_H__ */
