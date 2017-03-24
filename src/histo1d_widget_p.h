#ifndef __HISTO1D_WIDGET_P_H__
#define __HISTO1D_WIDGET_P_H__

#include <QDialog>
#include <QDialogButtonBox>
#include "histo1d_widget.h"

class Histo1DSubRangeDialog: public QDialog
{
    Q_OBJECT
    public:
        using SinkPtr = Histo1DWidget::SinkPtr;
        using HistoSinkCallback = Histo1DWidget::HistoSinkCallback;

        Histo1DSubRangeDialog(const SinkPtr &histoSink,
                              HistoSinkCallback sinkModifiedCallback,
                              double visibleMinX, double visibleMaxX,
                              QWidget *parent = 0);

        virtual void accept() override;

        SinkPtr m_sink;
        HistoSinkCallback m_sinkModifiedCallback;

        double m_visibleMinX;
        double m_visibleMaxX;

        HistoAxisLimitsUI limits_x;
        QDialogButtonBox *buttonBox;
};

#endif /* __HISTO1D_WIDGET_P_H__ */
