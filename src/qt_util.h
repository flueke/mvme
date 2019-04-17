/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
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

#include <memory>
#include <QEventLoop>
#include <QFormLayout>
#include <QFrame>
#include <QHash>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QObject>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include "libmvme_core_export.h"
#include "typedefs.h"

#define QSL(str) QStringLiteral(str)

class QAction;
class QEvent;
class QWidget;

class WidgetGeometrySaver: public QObject
{
    public:
        explicit WidgetGeometrySaver(QObject *parent = 0);

        void addWidget(QWidget *widget, const QString &key);
        void removeWidget(QWidget *widget);
        void restoreGeometry(QWidget *widget, const QString &key);
        void addAndRestore(QWidget *widget, const QString &key);

    protected:
        bool eventFilter(QObject *obj, QEvent *event);

    private:
        QSettings m_settings;
        QHash<QWidget *, QString> m_widgetKeys;
};

LIBMVME_CORE_EXPORT QAction *
add_widget_close_action(QWidget *widget,
                        const QKeySequence &shortcut = QKeySequence(QSL("Ctrl+W")),
                        Qt::ShortcutContext shortcutContext = Qt::WidgetWithChildrenShortcut);


QJsonObject storeDynamicProperties(const QObject *object);
void loadDynamicProperties(const QJsonObject &json, QObject *dest);
void loadDynamicProperties(const QVariantMap &properties, QObject *dest);


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

void set_widget_font_pointsize(QWidget *widget, float pointSize);
void set_widget_font_pointsize_relative(QWidget *widget, float relPointSize);

QToolBar *make_toolbar(QWidget *parent = nullptr);
QStatusBar *make_statusbar(QWidget *parent = nullptr);

void show_and_activate(QWidget *widget);

QString get_bitness_string();

LIBMVME_CORE_EXPORT QFont make_monospace_font(QFont baseFont = QFont());

void processQtEvents(QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents);
void processQtEvents(int maxtime_ms, QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents);

inline QFrame *make_separator_frame(Qt::Orientation orientation = Qt::Horizontal)
{
    auto frame = new QFrame;
    frame->setFrameStyle((orientation == Qt::Horizontal ? QFrame::HLine : QFrame::VLine) | QFrame::Plain);
    return frame;
}

inline QLabel *make_aligned_label(const QString &text,
                                  Qt::Alignment alignment = (Qt::AlignLeft | Qt::AlignVCenter))
{
    auto label = new QLabel(text);
    label->setAlignment(alignment);
    return label;
}

struct VBoxContainerWithLabel
{
    std::unique_ptr<QWidget> container;
    QVBoxLayout *layout;
    QLabel *label;
    QWidget *widget;
};

VBoxContainerWithLabel make_vbox_container(const QString &labelText, QWidget *widget,
                                           int spacing = 2, int labelRelativeFontPointSize = 0);

QWidget *make_spacer_widget(QWidget *parent = nullptr);
QToolButton *make_toolbutton(const QString &icon, const QString &text);
QToolButton *make_action_toolbutton(QAction *action = nullptr);

int get_widget_row(QFormLayout *layout, QWidget *widget);

/* Helper class for QLabel which makes the label only grow but never shrink
 * when a new text is set. */
class NonShrinkingLabelHelper
{
    public:
        NonShrinkingLabelHelper(QLabel *label = nullptr)
            : m_label(label)
        { }

        QLabel *getLabel() const { return m_label; }

        void setText(const QString &text)
        {
            if (m_label)
            {
                m_label->setText(text);

                if (m_label->isVisible())
                {
                    m_maxWidth  = std::max(m_maxWidth, m_label->width());
                    m_maxHeight = std::max(m_maxHeight, m_label->height());

                    m_label->setMinimumWidth(m_maxWidth);
                    m_label->setMinimumHeight(m_maxHeight);
                }
            }
        }

    private:
        QLabel *m_label = nullptr;
        s32 m_maxWidth  = 0;
        s32 m_maxHeight = 0;
};

template<typename T>
uint qHash(const std::shared_ptr<T> &ptr, uint seed = 0)
{
    return qHash(ptr.get());
}

template<typename LayoutType, int Margin = 2, int Spacing = 2>
LayoutType *make_layout(QWidget *widget = nullptr)
{
    auto ret = new LayoutType(widget);
    ret->setContentsMargins(Margin, Margin, Margin, Margin);
    ret->setSpacing(Spacing);
    return ret;
};

LIBMVME_CORE_EXPORT int calculate_tab_width(const QFont &font, int tabStop = 4);

#endif /* __QT_UTIL_H__ */
