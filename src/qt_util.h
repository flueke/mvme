#ifndef __QT_UTIL_H__
#define __QT_UTIL_H__

#include <QObject>
#include <QHash>

#define QSL(str) QStringLiteral(str)

class QEvent;
class QWidget;

class WidgetGeometrySaver: public QObject
{
    public:
        WidgetGeometrySaver(QObject *parent = 0);

        void addWidget(QWidget *widget, const QString &sizeKey, const QString &posKey);
        void removeWidget(QWidget *widget);
        void restoreGeometry(QWidget *widget, const QString &sizeKey, const QString &posKey);
        void addAndRestore(QWidget *widget, const QString &sizeKey, const QString &posKey);

    protected:
        bool eventFilter(QObject *obj, QEvent *event);

    private:
        struct Keys
        {
            QString sizeKey;
            QString posKey;
        };

        QHash<QWidget *, Keys> m_widgetKeys;
};

#endif /* __QT_UTIL_H__ */
