#ifndef __QT_UTIL_H__
#define __QT_UTIL_H__

#include <QHash>
#include <QJsonObject>
#include <QKeySequence>
#include <QObject>

#define QSL(str) QStringLiteral(str)

class QAction;
class QEvent;
class QWidget;

class WidgetGeometrySaver: public QObject
{
    public:
        WidgetGeometrySaver(QObject *parent = 0);

        void addWidget(QWidget *widget, const QString &key);
        void removeWidget(QWidget *widget);
        void restoreGeometry(QWidget *widget, const QString &key);
        void addAndRestore(QWidget *widget, const QString &key);

    protected:
        bool eventFilter(QObject *obj, QEvent *event);

    private:
        QHash<QWidget *, QString> m_widgetKeys;
};

QAction *add_widget_close_action(QWidget *widget,
                                const QKeySequence &shortcut = QKeySequence(QSL("Ctrl+W")),
                                Qt::ShortcutContext shortcutContext = Qt::WidgetWithChildrenShortcut);


QJsonObject storeDynamicProperties(const QObject *object);
void loadDynamicProperties(const QJsonObject &json, QObject *dest);

#endif /* __QT_UTIL_H__ */
