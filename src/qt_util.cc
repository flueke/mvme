#include "qt_util.h"

#include <QCloseEvent>
#include <QDebug>
#include <QSettings>
#include <QWidget>

WidgetGeometrySaver::WidgetGeometrySaver(QObject *parent)
    : QObject(parent)
{}

void WidgetGeometrySaver::addWidget(QWidget *widget, const QString &sizeKey, const QString &posKey)
{
    if (!m_widgetKeys.contains(widget))
    {
        widget->installEventFilter(this);
    }
    m_widgetKeys.insert(widget, {sizeKey, posKey});
}

void WidgetGeometrySaver::removeWidget(QWidget *widget)
{
    m_widgetKeys.remove(widget);
}

void WidgetGeometrySaver::restoreGeometry(QWidget *widget, const QString &sizeKey, const QString &posKey)
{
    QSettings settings;

    if (settings.contains(posKey))
    {
        auto pos = settings.value(posKey).toPoint();
        widget->move(pos);
    }

    if (settings.contains(sizeKey))
    {
        auto size = settings.value(sizeKey).toSize();
        widget->resize(size);
    }

}

void WidgetGeometrySaver::addAndRestore(QWidget *widget, const QString &sizeKey, const QString &posKey)
{
    addWidget(widget, sizeKey, posKey);
    restoreGeometry(widget, sizeKey, posKey);
}

bool WidgetGeometrySaver::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Close)
    {
        auto widget = qobject_cast<QWidget *>(obj);

        if (widget && m_widgetKeys.contains(widget))
        {
            auto closeEvent = static_cast<QCloseEvent *>(event);
            QSettings settings;
            settings.setValue(m_widgetKeys[widget].sizeKey, widget->size());
            settings.setValue(m_widgetKeys[widget].posKey, widget->pos());
        }
    }

    return QObject::eventFilter(obj, event);
}
