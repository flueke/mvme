#include "histo_gui_util.h"
#include "typedefs.h"

static const u32 RRFMin = 2;

QSlider *make_res_reduction_slider(QWidget *parent)
{
    auto result = new QSlider(Qt::Horizontal, parent);

    result->setSingleStep(1);
    result->setPageStep(1);
    result->setMinimum(RRFMin);

    return result;
}
