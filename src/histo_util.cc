#include "histo_util.h"
#include "qt_util.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>

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

Histo2DAxisLimitsUI make_histo2d_axis_limits_ui(const QString &groupBoxTitle, double inputMin, double inputMax,
                                                double limitMin, double limitMax)
{
    Histo2DAxisLimitsUI result = {};
    result.groupBox = new QGroupBox(groupBoxTitle);
    result.groupBox->setCheckable(true);
    result.spin_min = new QDoubleSpinBox;
    result.spin_max = new QDoubleSpinBox;

    result.limitFrame = new QFrame;
    auto limitFrameLayout = new QFormLayout(result.limitFrame);
    limitFrameLayout->setContentsMargins(2, 2, 2, 2);
    limitFrameLayout->addRow(QSL("Min"), result.spin_min);
    limitFrameLayout->addRow(QSL("Max"), result.spin_max);

    auto groupBoxLayout = new QVBoxLayout(result.groupBox);
    groupBoxLayout->setContentsMargins(0, 0, 0, 0);
    groupBoxLayout->addWidget(result.limitFrame);

    QObject::connect(result.groupBox, &QGroupBox::toggled, result.limitFrame, [result](bool usesFullRange) {
        result.limitFrame->setEnabled(usesFullRange);
    });

    bool usesFullRange = (std::isnan(limitMin) || std::isnan(limitMax));
    result.groupBox->setChecked(!usesFullRange);

    result.spin_min->setMinimum(inputMin);
    result.spin_min->setMaximum(inputMax);

    result.spin_max->setMinimum(inputMin);
    result.spin_max->setMaximum(inputMax);

    if (!usesFullRange)
    {
        result.spin_min->setValue(limitMin);
        result.spin_max->setValue(limitMax);
    }

    return result;
}
