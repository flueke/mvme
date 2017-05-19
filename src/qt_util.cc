#include "qt_util.h"

#include <QAction>
#include <QCloseEvent>
#include <QDebug>
#include <QSettings>
#include <QWidget>

WidgetGeometrySaver::WidgetGeometrySaver(QObject *parent)
    : QObject(parent)
{}

void WidgetGeometrySaver::addWidget(QWidget *widget, const QString &key)
{
    if (!m_widgetKeys.contains(widget))
    {
        widget->installEventFilter(this);

        connect(widget, &QWidget::destroyed, this, [this, widget] (QObject *) {
            m_widgetKeys.remove(widget);
        });
    }
    m_widgetKeys.insert(widget, key);
}

void WidgetGeometrySaver::removeWidget(QWidget *widget)
{
    m_widgetKeys.remove(widget);
}

void WidgetGeometrySaver::restoreGeometry(QWidget *widget, const QString &key)
{
    QSettings settings;

    if (settings.contains(key))
    {
        widget->restoreGeometry(settings.value(key).toByteArray());
    }
}

void WidgetGeometrySaver::addAndRestore(QWidget *widget, const QString &key)
{
    addWidget(widget, key);
    restoreGeometry(widget, key);
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
            settings.setValue(m_widgetKeys[widget], widget->saveGeometry());
            qDebug() << "saved geometry for" << widget << " key =" << m_widgetKeys[widget];
        }
    }

    return QObject::eventFilter(obj, event);
}

QAction *add_widget_close_action(QWidget *widget,
                                const QKeySequence &shortcut,
                                Qt::ShortcutContext shortcutContext)
{
    auto closeAction = new QAction(QSL("Close"), widget);

    QObject::connect(closeAction, &QAction::triggered, widget, [widget] (bool) {
        widget->close();
    });

    closeAction->setShortcutContext(shortcutContext);
    closeAction->setShortcut(shortcut);
    widget->addAction(closeAction);

    return closeAction;
}

QJsonObject storeDynamicProperties(const QObject *object)
{
    QJsonObject json;

    for (auto name: object->dynamicPropertyNames())
       json[QString::fromLocal8Bit(name)] = QJsonValue::fromVariant(object->property(name.constData()));

    return json;
}

void loadDynamicProperties(const QJsonObject &json, QObject *dest)
{
    auto properties = json.toVariantMap();

    for (auto propName: properties.keys())
    {
        const auto &value = properties[propName];
        dest->setProperty(propName.toLocal8Bit().constData(), value);
    }
}
