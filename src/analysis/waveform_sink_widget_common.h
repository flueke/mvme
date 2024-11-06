#ifndef DDCD557F_CBF3_492F_A9F4_289AC4C4F7C8
#define DDCD557F_CBF3_492F_A9F4_289AC4C4F7C8

#include <QPushButton>
#include <QSpinBox>
#include "qt_util.h"
#include "mdpp-sampling/mdpp_decode.h"

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

inline QDoubleSpinBox *add_dt_sample_setter(QToolBar *toolbar)
{
    namespace mdpp_sampling = mesytec::mvme::mdpp_sampling;
    auto result = new QDoubleSpinBox;
    result->setMinimum(1.0);
    result->setMaximum(1e9);
    result->setSingleStep(0.1);
    result->setSuffix(" ns");
    result->setValue(mdpp_sampling::MdppDefaultSamplePeriod);

    auto pb_useDefaultSampleInterval = new QPushButton(QIcon(":/reset_to_default.png"), {});

    QObject::connect(pb_useDefaultSampleInterval, &QPushButton::clicked, result, [result] {
        result->setValue(mdpp_sampling::MdppDefaultSamplePeriod);
    });

    auto [w0, l0] = make_widget_with_layout<QWidget, QHBoxLayout>();
    l0->addWidget(result);
    l0->addWidget(pb_useDefaultSampleInterval);

    auto boxStruct = make_vbox_container(QSL("Sample Interval"), w0, 0, -2);
    toolbar->addWidget(boxStruct.container.release());

    return result;
}

inline QSpinBox *add_interpolation_factor_setter(QToolBar *toolbar)
{
    auto result = new QSpinBox;
    result->setSpecialValueText("off");
    result->setMinimum(0);
    result->setMaximum(100);
    result->setValue(5);
    auto boxStruct = make_vbox_container(QSL("Interpolation Factor"), result, 0, -2);
    toolbar->addWidget(boxStruct.container.release());
    return result;
}

}

#endif /* DCC8E3B2_5822_46AA_A29C_3B92C583BD2A */
