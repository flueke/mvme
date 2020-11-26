#ifndef __MVME_QT_LAYOUTS_H__
#define __MVME_QT_LAYOUTS_H__

#include <QWidget>
#include <QHBoxLayout>

inline QWidget *make_centered(QWidget *widget)
{
    auto w = new QWidget;
    auto l = new QHBoxLayout(w);
    l->setSpacing(0);
    l->setContentsMargins(0, 0, 0, 0);
    l->addStretch(1);
    l->addWidget(widget);
    l->addStretch(1);
    return w;
}

#endif /* __MVME_QT_LAYOUTS_H__ */
