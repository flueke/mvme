/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __QT_UTIL_H__
#define __QT_UTIL_H__

#include "typedefs.h"

#include <QEventLoop>
#include <QFrame>
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

void show_and_activate(QWidget *widget);

QString get_bitness_string();

QFont make_monospace_font(QFont baseFont = QFont());

void processQtEvents(QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents);
void processQtEvents(int maxtime_ms, QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents);

inline QFrame *make_separator_frame(Qt::Orientation orientation = Qt::Horizontal)
{
    auto frame = new QFrame;
    frame->setFrameStyle((orientation == Qt::Horizontal ? QFrame::HLine : QFrame::VLine) | QFrame::Plain);
    return frame;
}

inline QLabel *make_aligned_label(const QString &text, Qt::Alignment alignment = (Qt::AlignLeft | Qt::AlignVCenter))
{
    auto label = new QLabel(text);
    label->setAlignment(alignment);
    return label;
}

#endif /* __QT_UTIL_H__ */
