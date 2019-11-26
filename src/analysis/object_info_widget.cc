#include "object_info_widget.h"

#include "a2_adapter.h"
#include "analysis.h"
#include "mvme_context.h"
#include "qt_util.h"

namespace analysis
{

struct ObjectInfoWidget::Private
{
    MVMEContext *m_context;
    AnalysisObjectPtr m_analysisObject;
    const ConfigObject *m_configObject;

    QLabel *m_infoLabel;
};

ObjectInfoWidget::ObjectInfoWidget(MVMEContext *ctx, QWidget *parent)
    : QFrame(parent)
    , m_d(std::make_unique<Private>())
{
    setFrameStyle(QFrame::NoFrame);

    m_d->m_context = ctx;
    m_d->m_infoLabel = new QLabel;
    m_d->m_infoLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_d->m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    set_widget_font_pointsize_relative(m_d->m_infoLabel, -2);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_d->m_infoLabel);

    connect(ctx, &MVMEContext::vmeConfigAboutToBeSet,
            this, &ObjectInfoWidget::clear);
}

ObjectInfoWidget::~ObjectInfoWidget()
{ }

void ObjectInfoWidget::setAnalysisObject(const AnalysisObjectPtr &obj)
{
    m_d->m_analysisObject = obj;
    m_d->m_configObject = nullptr;
    refresh();
}

void ObjectInfoWidget::setVMEConfigObject(const ConfigObject *obj)
{
    m_d->m_analysisObject = {};
    m_d->m_configObject = obj;
    refresh();
}

void ObjectInfoWidget::refresh()
{
    auto refresh_analysisObject = [this] (const AnalysisObjectPtr &obj)
    {
        assert(obj);
        auto &label(m_d->m_infoLabel);

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

        auto analysis = m_d->m_context->getAnalysis();

        if (auto op = qobject_cast<OperatorInterface *>(obj.get()))
        {
            text += QSL("\nrank=%1").arg(op->getRank());

            text += QSL("\n#inputs=%1, maxInRank=%2")
                .arg(op->getNumberOfSlots())
                .arg(op->getMaximumInputRank());

            text += QSL("\n#outputs=%1, maxOutRank=%2")
                .arg(op->getNumberOfOutputs())
                .arg(op->getMaximumOutputRank());


            if (auto condLink = analysis->getConditionLink(op))
            {
                text += QSL("\ncondLink=%1[%2], condRank=%3")
                    .arg(condLink.condition->objectName())
                    .arg(condLink.subIndex)
                    .arg(condLink.condition->getRank());
            }
        }

        auto a2State = analysis->getA2AdapterState();
        auto cond = qobject_cast<ConditionInterface *>(obj.get());

        if (a2State && a2State->a2 && cond && a2State->conditionBitIndexes.contains(cond))
        {
            // FIXME: async copy or direct reference here. does access to the
            // bitset need to be guarded? if copying try to find a faster way than
            // testing and setting bit-by-bit. maybe alloc, clear and use an
            // overloaded OR operator
            const auto &condBits = a2State->a2->conditionBits;
            s32 firstBit = a2State->conditionBitIndexes.value(cond);

            QString buffer;

            // rough estimate to avoid many reallocations
            buffer.reserve(cond->getNumberOfBits() * 3);

            for (s32 bi = firstBit; bi < firstBit + cond->getNumberOfBits(); bi++)
            {
                assert(0 <= bi && static_cast<size_t>(bi) < condBits.size());

                buffer += QSL("%1,").arg(condBits.test(bi));
            }

            text += QSL("\nbits=") + buffer;
        }

        label->setText(text);
    };

    auto refresh_vmeConfigObject = [this] (const ConfigObject *obj)
    {
        assert(obj);

        if (auto moduleConfig = qobject_cast<const ModuleConfig *>(obj))
        {
            QString text;
            text += QSL("VME module\nname=%1, type=%2\naddress=0x%3")
                .arg(moduleConfig->objectName())
                .arg(moduleConfig->getModuleMeta().typeName)
                .arg(moduleConfig->getBaseAddress(), 8, 16, QChar('0'));

            m_d->m_infoLabel->setText(text);
        }
        else
        {
            m_d->m_infoLabel->clear();
        }
    };

    if (m_d->m_analysisObject)
    {
        refresh_analysisObject(m_d->m_analysisObject);
    }
    else if (m_d->m_configObject)
    {
        refresh_vmeConfigObject(m_d->m_configObject);
    }
    else
    {
        m_d->m_infoLabel->clear();
    }
}

void ObjectInfoWidget::clear()
{
    m_d->m_analysisObject = {};
    m_d->m_configObject = nullptr;
    m_d->m_infoLabel->clear();
}


} // end namespace analysis
