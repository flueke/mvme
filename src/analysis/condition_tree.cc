#include "condition_tree.h"

namespace analysis
{

struct ConditionTreeWidget::Private
{
    Private(EventWidget *eventWidget)
        : m_eventWidget(eventWidget)
    {}

    Analysis *getAnalysis() const
    {
        return m_eventWidget->getAnalysis();
    }

    QUuid getEventId() const
    {
        return m_eventWidget->getEventId();
    }

    EventWidget *m_eventWidget;
};

ConditionTreeWidget::ConditionTreeWidget(EventWidget *eventWidget, QWidget *parent)
    : QTreeWidget(parent)
    , m_d(std::make_unique<Private>(eventWidget))
{
    qDebug() << __PRETTY_FUNCTION__ << this;

    // columns: name, type, condval
    setColumnCount(3);
    headerItem()->setText(0, QSL("Name"));
    headerItem()->setText(1, QSL("Type"));
    headerItem()->setText(2, QSL("Flag"));

    repopulate();
}

ConditionTreeWidget::~ConditionTreeWidget()
{
    qDebug() << __PRETTY_FUNCTION__ << this;
}

void ConditionTreeWidget::repopulate()
{
    auto analysis = m_d->getAnalysis();
    auto conditions = analysis->getConditions(m_d->getEventId());

    qSort(conditions.begin(), conditions.end(), [](auto c1, auto c2) {
        return c1->objectName() < c2->objectName();
    });

    for (auto cond: conditions)
    {
        auto node = new QTreeWidgetItem;

        node->setText(0, cond->objectName());
        node->setText(1, cond->metaObject()->className());
        node->setText(2, "???");

        this->addTopLevelItem(node);
    }
}

void ConditionTreeWidget::doPeriodicUpdate()
{
}

}
