#ifndef EB200261_6034_4809_B469_CC350D1424B8
#define EB200261_6034_4809_B469_CC350D1424B8

#include <qwt_plot_picker.h>
#include <qwt_transform.h>
#include <qwt_series_data.h>

inline double get_log_min_inl()
{
    return QwtLogTransform::LogMin;
}

inline double get_log_max_inl()
{
    return QwtLogTransform::LogMax;
}

double get_log_min();
double get_log_max();

#endif /* EB200261_6034_4809_B469_CC350D1424B8 */
