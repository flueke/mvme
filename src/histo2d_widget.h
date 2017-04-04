#ifndef __HISTO2D_WIDGET_H__
#define __HISTO2D_WIDGET_H__

#include "histo2d.h"
#include <QWidget>

class QTimer;
class QwtPlotSpectrogram;
class QwtLinearColorMap;
class QwtPlotHistogram;
class ScrollZoomer;
class MVMEContext;
class Histo1DWidget;
class WidgetGeometrySaver;

namespace Ui
{
    class Histo2DWidget;
}

namespace analysis
{
    class Histo2DSink;
};


class Histo2DWidget: public QWidget
{
    Q_OBJECT
    public:
        using SinkPtr = std::shared_ptr<analysis::Histo2DSink>;
        using HistoSinkCallback = std::function<void (const SinkPtr &)>;
        using MakeUniqueOperatorNameFunction = std::function<QString (const QString &name)>;

        Histo2DWidget(const Histo2DPtr histoPtr, QWidget *parent = 0);
        Histo2DWidget(Histo2D *histo, QWidget *parent = 0);
        ~Histo2DWidget();

        void setSink(const SinkPtr &sink, HistoSinkCallback addSinkCallback, HistoSinkCallback sinkModifiedCallback,
                     MakeUniqueOperatorNameFunction makeUniqueOperatorNameFunction);

    private slots:
        void replot();
        void exportPlot();
        void mouseCursorMovedToPlotCoord(QPointF);
        void mouseCursorLeftPlot();
        void displayChanged();
        void zoomerZoomed(const QRectF &);
        void on_tb_info_clicked();
        void on_tb_subRange_clicked();
        void on_tb_projX_clicked();
        void on_tb_projY_clicked();

    private:
        bool zAxisIsLog() const;
        bool zAxisIsLin() const;
        QwtLinearColorMap *getColorMap() const;
        void updateCursorInfoLabel();
        void doXProjection();
        void doYProjection();

        Ui::Histo2DWidget *ui;
        Histo2D *m_histo;
        Histo2DPtr m_histoPtr;
        QwtPlotSpectrogram *m_plotItem;
        ScrollZoomer *m_zoomer;
        QTimer *m_replotTimer;
        QPointF m_cursorPosition;
        int m_labelCursorInfoWidth;

        std::shared_ptr<analysis::Histo2DSink> m_sink;
        HistoSinkCallback m_addSinkCallback;
        HistoSinkCallback m_sinkModifiedCallback;
        MakeUniqueOperatorNameFunction m_makeUniqueOperatorNameFunction;

        Histo1DWidget *m_xProjWidget = nullptr;
        Histo1DWidget *m_yProjWidget = nullptr;

        WidgetGeometrySaver *m_geometrySaver;
};

#endif /* __HISTO2D_WIDGET_H__ */
