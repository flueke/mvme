#include "object_info_widget.h"

#include "analysis.h"
#include "mvme_context.h"
#include "qt_util.h"

namespace analysis
{

struct ObjectInfoWidget::Private
{
    MVMEContext *m_context;
    AnalysisObjectPtr m_obj;

    QLabel *m_infoLabel;
};

ObjectInfoWidget::ObjectInfoWidget(MVMEContext *ctx, QWidget *parent)
    : QFrame(parent)
    , m_d(std::make_unique<Private>())
{
    setFrameStyle(QFrame::StyledPanel);

    m_d->m_context = ctx;
    m_d->m_infoLabel = new QLabel;
    m_d->m_infoLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    set_widget_font_pointsize_relative(m_d->m_infoLabel, -2);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addWidget(m_d->m_infoLabel);
}

ObjectInfoWidget::~ObjectInfoWidget()
{ }

void ObjectInfoWidget::setObject(const AnalysisObjectPtr &obj)
{
    m_d->m_obj = obj;
    refresh();
}

void ObjectInfoWidget::refresh()
{
    const auto &obj(m_d->m_obj);
    auto &label(m_d->m_infoLabel);

    if (!obj)
    {
        label->clear();
        return;
    }

    /*
     * AnalysisObject: className(), objectName(), getId(),
     *                 getUserLevel(), eventId(), objectFlags()
     *
     * PipeSourceInterface: getNumberOfOutputs(), getOutput(idx)
     *
     * OperatorInterface: getMaximumInputRank(), getMaximumOutputRank(),
     *                    getNumberOfSlots(), getSlot(idx)
     *
     * SinkInterface: getStorageSize(), isEnabled()
     *
     * ConditionInterface: getNumberOfBits()
     */

    QString text;

    text += QSL("cls=%1, n=%2")
        .arg(obj->metaObject()->className())
        .arg(obj->objectName())
        ;

    text += QSL("\nusrLvl=%1, flags=%2")
        .arg(obj->getUserLevel())
        .arg(to_string(obj->getObjectFlags()))
        ;

    if (auto op = qobject_cast<OperatorInterface *>(obj.get()))
    {
        text += QSL("\n#inputs=%1, maxInRank=%2")
            .arg(op->getNumberOfSlots())
            .arg(op->getMaximumInputRank());

        text += QSL("\n#outputs=%1, maxOutRank=%2")
            .arg(op->getNumberOfOutputs())
            .arg(op->getMaximumOutputRank());

        auto analysis = m_d->m_context->getAnalysis();

        if (auto condLink = analysis->getConditionLink(op))
        {
            text += QSL("\ncondLink=%1[%2], condMaxInRank=%3")
                .arg(condLink.condition->objectName())
                .arg(condLink.subIndex)
                .arg(condLink.condition->getMaximumInputRank());
        }
    }

    label->setText(text);
}


} // end namespace analysis
