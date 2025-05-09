#ifndef DDCD557F_CBF3_492F_A9F4_289AC4C4F7C8
#define DDCD557F_CBF3_492F_A9F4_289AC4C4F7C8

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>

#include "qt_util.h"
#include "mdpp-sampling/mdpp_decode.h"
#include "mdpp-sampling/waveform_interpolation.h"

namespace analysis
{

inline QSpinBox *add_channel_select(QToolBar *toolbar)
{
    auto result = new QSpinBox;
    result->setMinimum(0);
    result->setMaximum(0);
    auto boxStruct = make_vbox_container(QSL("Channel"), result, 0, -2);
    toolbar->addWidget(boxStruct.container.release());
    return result;
}

inline QSpinBox *add_trace_select(QToolBar *toolbar)
{
    auto result = new QSpinBox;
    result->setMinimum(0);
    result->setMaximum(1);
    result->setValue(0);
    result->setSpecialValueText("latest");
    auto boxStruct = make_vbox_container(QSL("Trace#"), result, 0, -2);
    toolbar->addWidget(boxStruct.container.release());
    return result;
}

struct DtSampleSetterUi
{
    QDoubleSpinBox *spin_dtSample;
    QPushButton *pb_useDefaultSampleInterval;
    QCheckBox *cb_showSamples;
};

inline DtSampleSetterUi add_dt_sample_setter(QToolBar *toolbar)
{
    namespace mdpp_sampling = mesytec::mvme::mdpp_sampling;
    auto spin = new QDoubleSpinBox;
    spin->setMinimum(0.1);
    spin->setMaximum(1e2);
    spin->setSingleStep(0.1);
    spin->setSuffix(" ns");
    spin->setValue(mdpp_sampling::MdppDefaultSamplePeriod);

    auto pb_useDefaultSampleInterval = new QPushButton(QIcon(":/reset_to_default.png"), {});
    pb_useDefaultSampleInterval->setToolTip(QSL("Reset to MDPP default sample interval (%1 ns).")
        .arg(mdpp_sampling::MdppDefaultSamplePeriod));

    DtSampleSetterUi res{};
    res.spin_dtSample = spin;
    res.pb_useDefaultSampleInterval = pb_useDefaultSampleInterval;
    res.cb_showSamples = new QCheckBox("Sample Nr.");

    auto [w0, l0] = make_widget_with_layout<QWidget, QGridLayout>();
    l0->addWidget(spin, 0, 0);
    l0->addWidget(pb_useDefaultSampleInterval, 0, 1);
    l0->addWidget(res.cb_showSamples, 0, 2, 1, 1);

    auto boxStruct = make_vbox_container(QSL("Sample Interval"), w0, 0, -2);
    boxStruct.layout->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(boxStruct.container.release());

    QObject::connect(pb_useDefaultSampleInterval, &QPushButton::clicked, spin, [spin] {
        spin->setValue(mdpp_sampling::MdppDefaultSamplePeriod);
    });

    QObject::connect(res.cb_showSamples, &QCheckBox::stateChanged, spin, [res] (int state) {
        res.spin_dtSample->setEnabled(state == Qt::Unchecked);
        res.pb_useDefaultSampleInterval->setEnabled(state == Qt::Unchecked);
    });

    return res;
}

inline QSpinBox *add_interpolation_factor_setter(QToolBar *toolbar)
{
    auto result = new QSpinBox;
    result->setSpecialValueText("off");
    result->setMinimum(0);
    result->setMaximum(100);
    result->setValue(5);
    auto boxStruct = make_vbox_container(QSL("Interpolation"), result, 0, -2);
    toolbar->addWidget(boxStruct.container.release());
    return result;
}

struct SplineSettingsUi: public QFrame
{
    QFormLayout *layout;
    QComboBox *combo_splineType;
    QCheckBox *cb_makeMonotonic;

    SplineSettingsUi(QWidget *parent = nullptr)
        : QFrame(parent)
        , combo_splineType(new QComboBox(this))
        , cb_makeMonotonic(new QCheckBox("Make Monotonic", this))
    {
        layout = new QFormLayout(this);
        setLayout(layout);
        setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        setLineWidth(1);
        setContentsMargins(0, 0, 0, 0);

        combo_splineType->addItem("cubic", "cspline");
        combo_splineType->addItem("cubic hermite", "cspline_hermite");
        combo_splineType->addItem("linear", "linear");

        layout->addRow("Spline Type", combo_splineType);
        layout->addRow(cb_makeMonotonic);
    }
};

class InterpolationSettingsUi: public QDialog
{
    Q_OBJECT
    signals:
    void interpolationTypeSelected(const QString &ipolType);
    void interpolationFactorChanged(int factor);

    public:

    QFormLayout *layout;
    QComboBox *combo_interpolationType;
    QStackedWidget *stack_settings;
    SplineSettingsUi *splineSettings;
    QSpinBox *spin_factor;

    InterpolationSettingsUi(QWidget *parent = nullptr)
        : QDialog(parent)
        , combo_interpolationType(new QComboBox)
        , stack_settings(new QStackedWidget)
        , splineSettings(new SplineSettingsUi)
        , spin_factor(new QSpinBox)
    {
        layout = new QFormLayout(this);
        setLayout(layout);
        setContentsMargins(0, 0, 0, 0);
        setWindowTitle("Interpolation Settings");

        spin_factor->setMinimum(0);
        spin_factor->setMaximum(100);
        spin_factor->setValue(5);

        combo_interpolationType->addItem("spline", "spline");
        combo_interpolationType->addItem("sinc", "sinc");
        stack_settings->addWidget(splineSettings);
        stack_settings->addWidget(new QWidget);

        auto gb_settings = new QGroupBox("Settings");
        auto l_settings = make_hbox(gb_settings);
        l_settings->addWidget(stack_settings);

        layout->addRow("Interpolation Type", combo_interpolationType);
        layout->addRow("Interpolation Factor", spin_factor);
        layout->addRow(gb_settings);

        connect(combo_interpolationType, qOverload<int>(&QComboBox::currentIndexChanged),
                stack_settings, &QStackedWidget::setCurrentIndex);

        connect(combo_interpolationType, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] (int)
        {
            emit interpolationTypeSelected(combo_interpolationType->currentData().toString());
        });

        connect(spin_factor, qOverload<int>(&QSpinBox::valueChanged),
            this, [this] (int value) { emit interpolationFactorChanged(value); });
    }

    int getInterpolationFactor() const
    {
        return spin_factor->value();
    }

    QString getInterpolationType() const
    {
        return combo_interpolationType->currentData().toString();
    }

    mesytec::mvme::waveforms::SplineParams getSplineParams()
    {
        mesytec::mvme::waveforms::SplineParams result;
        result.splineType = splineSettings->combo_splineType->currentData().toString().toStdString();
        result.makeMonotonic = splineSettings->cb_makeMonotonic->isChecked();
        return result;
    }
};

}

#endif /* DCC8E3B2_5822_46AA_A29C_3B92C583BD2A */
