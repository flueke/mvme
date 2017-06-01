#include "qt_util.h"

#include <QAction>
#include <QCloseEvent>
#include <QDebug>
#include <QPainter>
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

// VerticalLabel source: https://stackoverflow.com/a/18515898
VerticalLabel::VerticalLabel(QWidget *parent)
    : QLabel(parent)
{
}

VerticalLabel::VerticalLabel(const QString &text, QWidget *parent)
: QLabel(text, parent)
{
}

void VerticalLabel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setPen(Qt::black);
    painter.setBrush(Qt::Dense1Pattern);

#if 0
    painter.rotate(90);
#else
    // FIXME: Subtracting 10 is a hack that just works for the single use case
    // I have right now.
    painter.translate(sizeHint().width()-10, sizeHint().height());
    painter.rotate(270);
#endif

    painter.drawText(0,0, text());
}

QSize VerticalLabel::minimumSizeHint() const
{
    QSize s = QLabel::minimumSizeHint();
    return QSize(s.height(), s.width());
}

QSize VerticalLabel::sizeHint() const
{
    QSize s = QLabel::sizeHint();
    return QSize(s.height(), s.width());
}

void set_widget_font_pointsize(QWidget *widget, s32 pointSize)
{
    auto font = widget->font();
    font.setPointSize(pointSize);
    widget->setFont(font);
}
