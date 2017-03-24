#include "histo1d_widget_p.h"
#include "analysis/analysis.h"

#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QVBoxLayout>

using namespace analysis;

Histo1DSubRangeDialog::Histo1DSubRangeDialog(const SinkPtr &histoSink,
                                             HistoSinkCallback sinkModifiedCallback,
                                             double visibleMinX, double visibleMaxX,
                                             QWidget *parent)
    : QDialog(parent)
    , m_sink(histoSink)
    , m_sinkModifiedCallback(sinkModifiedCallback)
    , m_visibleMinX(visibleMinX)
    , m_visibleMaxX(visibleMaxX)
{
    setWindowTitle(QSL("Set histogram range"));

    limits_x = make_histo2d_axis_limits_ui(QSL("X Limits"),
                                           std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
                                           visibleMinX, visibleMaxX);

    //
    // buttons bottom
    //
    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    //
    // main layout
    //
    auto layout = new QVBoxLayout(this);

    layout->addWidget(limits_x.groupBox);
    layout->addStretch();
    layout->addWidget(buttonBox);
}

void Histo1DSubRangeDialog::accept()
{
    if (limits_x.groupBox->isChecked())
    {
        m_sink->m_xLimitMin = limits_x.spin_min->value();
        m_sink->m_xLimitMax = limits_x.spin_max->value();
    }
    else
    {
        m_sink->m_xLimitMin = make_quiet_nan();
        m_sink->m_xLimitMax = make_quiet_nan();
    }

    m_sinkModifiedCallback(m_sink);

    QDialog::accept();
}
