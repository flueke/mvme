#ifndef __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_
#define __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_

#include <qwt_plot.h>

class TilePlot: public QwtPlot
{
    Q_OBJECT
    public:
       explicit TilePlot(QWidget *parent = nullptr);
       ~TilePlot() override;

        QSize sizeHint() const override;
};

#endif // __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_