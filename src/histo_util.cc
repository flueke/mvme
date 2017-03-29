#include "histo_util.h"
#include "qt_util.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QRadioButton>

QString makeAxisTitle(const QString &title, const QString &unit)
{
    QString result;
    if (!title.isEmpty())
    {
        result = title;
        if (!unit.isEmpty())
        {
            result += QString(" [%1]").arg(unit);
        }
    }

    return result;
}

void select_by_resolution(QComboBox *combo, s32 selectedRes)
{
    s32 minBits = 0;
    s32 selectedBits = std::log2(selectedRes);

    if (selectedBits > 0)
    {
        s32 index = selectedBits - minBits - 1;
        index = std::min(index, combo->count() - 1);
        combo->setCurrentIndex(index);
    }
}

QComboBox *make_resolution_combo(s32 minBits, s32 maxBits, s32 selectedBits)
{
    QComboBox *result = new QComboBox;

    for (s32 bits = minBits;
         bits <= maxBits;
         ++bits)
    {
        s32 value = 1 << bits;

        QString text = QString("%1, %2 bit").arg(value, 4).arg(bits, 2);

        result->addItem(text, value);
    }

    select_by_resolution(result, 1 << selectedBits);

    return result;
}

/* inputMin and inputMax are used as the min and max possible values for the spin boxes.
 *
 * limitMin and limitMax are the values used to populate both spinboxes.
 *
 * isLimtied indicates whether limiting is currently in effect and affects
 * which radio button is initially active.
 */
HistoAxisLimitsUI make_axis_limits_ui(const QString &limitButtonTitle, double inputMin, double inputMax,
                                      double limitMin, double limitMax, bool isLimited)
{
    HistoAxisLimitsUI result = {};
    result.rb_limited = new QRadioButton(limitButtonTitle);
    result.rb_fullRange = new QRadioButton(QSL("Full Range"));

    result.spin_min = new QDoubleSpinBox;
    result.spin_max = new QDoubleSpinBox;

    result.limitFrame = new QFrame;
    result.limitFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);

    auto limitFrameLayout = new QFormLayout(result.limitFrame);
    limitFrameLayout->setContentsMargins(2, 2, 2, 2);
    limitFrameLayout->addRow(QSL("Min"), result.spin_min);
    limitFrameLayout->addRow(QSL("Max"), result.spin_max);

    result.outerFrame = new QFrame;
    auto outerFrameLayout = new QVBoxLayout(result.outerFrame);
    outerFrameLayout->setContentsMargins(4, 4, 4, 4);
    outerFrameLayout->addWidget(result.rb_limited);
    outerFrameLayout->addWidget(result.limitFrame);
    outerFrameLayout->addWidget(result.rb_fullRange);

    QObject::connect(result.rb_limited, &QAbstractButton::toggled, result.outerFrame, [result] (bool checked) {
        result.limitFrame->setEnabled(checked);
    });

    result.rb_limited->setChecked(true);

    if (!isLimited)
    {
        result.rb_fullRange->setChecked(true);
    }

    result.spin_min->setMinimum(inputMin);
    result.spin_min->setMaximum(inputMax);

    result.spin_max->setMinimum(inputMin);
    result.spin_max->setMaximum(inputMax);

    if (!std::isnan(limitMin))
        result.spin_min->setValue(limitMin);

    if (!std::isnan(limitMax))
        result.spin_max->setValue(limitMax);

    return result;
}
