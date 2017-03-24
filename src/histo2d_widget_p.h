#ifndef __HISTO2D_WIDGET_P_H__
#define __HISTO2D_WIDGET_P_H__

#include <memory>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include "histo_util.h"

namespace analysis
{
    class Histo2DSink;
}

class MVMEContext;

class Histo2DSubRangeDialog: public QDialog
{
    Q_OBJECT
    public:
        using SinkPtr = std::shared_ptr<analysis::Histo2DSink>;
        using HistoSinkCallback = std::function<void (const SinkPtr &)>;
        using MakeUniqueOperatorNameFunction = std::function<QString (const QString &name)>;


        Histo2DSubRangeDialog(const SinkPtr &histoSink,
                              HistoSinkCallback addSinkCallback, HistoSinkCallback sinkModifiedCallback,
                              MakeUniqueOperatorNameFunction makeUniqueOperatorNameFunction,
                              double visibleMinX, double visibleMaxX, double visibleMinY, double visibleMaxY,
                              QWidget *parent = 0);

        virtual void accept() override;

        SinkPtr m_histoSink;
        HistoSinkCallback m_addSinkCallback;
        HistoSinkCallback m_sinkModifiedCallback;

        double m_visibleMinX;
        double m_visibleMaxX;
        double m_visibleMinY;
        double m_visibleMaxY;

        QComboBox *combo_xBins = nullptr;
        QComboBox *combo_yBins = nullptr;
        Histo2DAxisLimitsUI limits_x;
        Histo2DAxisLimitsUI limits_y;
        QDialogButtonBox *buttonBox;
        QLineEdit *le_name;
        QGroupBox *gb_createAsNew;
};

#endif /* __HISTO2D_WIDGET_P_H__ */
