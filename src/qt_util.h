#ifndef __QT_UTIL_H__
#define __QT_UTIL_H__

#include "typedefs.h"

#include <QHash>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QObject>
#include <QStatusBar>
#include <QToolBar>

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


// VerticalLabel source: https://stackoverflow.com/a/18515898
class VerticalLabel : public QLabel
{
    Q_OBJECT

public:
    explicit VerticalLabel(QWidget *parent=0);
    explicit VerticalLabel(const QString &text, QWidget *parent=0);

protected:
    void paintEvent(QPaintEvent*);
    QSize sizeHint() const ;
    QSize minimumSizeHint() const;
};

void set_widget_font_pointsize(QWidget *widget, s32 pointSize);

QToolBar *make_toolbar(QWidget *parent = nullptr);
QStatusBar *make_statusbar(QWidget *parent = nullptr);

#endif /* __QT_UTIL_H__ */
