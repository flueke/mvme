#ifndef __MVME_PLOT_UTIL_H__
#define __MVME_PLOT_UTIL_H__

#include <memory>
#include <qwt_plot_item.h>
#include <qwt_text.h>
#include "libmvme_export.h"

namespace mvme_qwt
{

class TextLabelRowLayout;

class LIBMVME_EXPORT TextLabelItem: public QwtPlotItem
{
    public:
        TextLabelItem(const QwtText &title = QwtText());
        virtual ~TextLabelItem();

        void setText(const QwtText &text);
        QwtText text() const;

        void setParentLayout(TextLabelRowLayout *layout);
        TextLabelRowLayout *getParentLayout() const;

        virtual int rtti() const override { return QwtPlotItem::Rtti_PlotTextLabel; }

        virtual void draw( QPainter *painter,
                          const QwtScaleMap &xMap, const QwtScaleMap &yMap,
                          const QRectF &canvasRect) const override;

    protected:
        void invalidateCache();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

class LIBMVME_EXPORT TextLabelRowLayout
{
    public:
        TextLabelRowLayout();
        ~TextLabelRowLayout();

        void addTextLabel(TextLabelItem *label);
        QVector<TextLabelItem *> getTextLabels() const;
        int size() const;
        void removeTextLabel(TextLabelItem *label);
        void removeTextLabel(int index);

        QRectF getPaintArea(const TextLabelItem *label, QPainter *painter,
                            const QRectF &canvasRect) const;

        void attachAll(QwtPlot *plot);

        void setMarginTop(int margin);
        int getMarginTop() const;
        void setMarginRight(int margin);
        int getMarginRight() const;
        void setSpacing(int spacing);
        int getSpacing() const;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};


} // end namespace mvme_qwt

#endif /* __MVME_PLOT_UTIL_H__ */
