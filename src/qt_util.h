/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include <QClipboard>
#include <QEventLoop>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHash>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QObject>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <sstream>

#include "libmvme_export.h"
#include "typedefs.h"
#include "util/qt_str.h"

class QAction;
class QEvent;
class QWidget;

class LIBMVME_EXPORT WidgetGeometrySaver: public QObject
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

LIBMVME_EXPORT QAction *
add_widget_close_action(QWidget *widget,
                        const QKeySequence &shortcut = QKeySequence(QSL("Ctrl+W")),
                        Qt::ShortcutContext shortcutContext = Qt::WidgetWithChildrenShortcut);


QJsonObject LIBMVME_EXPORT storeDynamicProperties(const QObject *object);
void LIBMVME_EXPORT loadDynamicProperties(const QJsonObject &json, QObject *dest);
void LIBMVME_EXPORT loadDynamicProperties(const QVariantMap &properties, QObject *dest);


// VerticalLabel source: https://stackoverflow.com/a/18515898
class LIBMVME_EXPORT VerticalLabel : public QLabel
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

void LIBMVME_EXPORT set_widget_font_pointsize(QWidget *widget, float pointSize);
void LIBMVME_EXPORT set_widget_font_pointsize_relative(QWidget *widget, float relPointSize);

LIBMVME_EXPORT QToolBar *make_toolbar(QWidget *parent = nullptr);
LIBMVME_EXPORT QStatusBar *make_statusbar(QWidget *parent = nullptr);

LIBMVME_EXPORT void show_and_activate(QWidget *widget);

LIBMVME_EXPORT QString get_bitness_string();

LIBMVME_EXPORT
void processQtEvents(QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents);

LIBMVME_EXPORT
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

struct LIBMVME_EXPORT BoxContainerWithLabel
{
    std::unique_ptr<QWidget> container;
    QBoxLayout *layout;
    QLabel *label;
    QWidget *widget;
};

LIBMVME_EXPORT BoxContainerWithLabel make_vbox_container(
    const QString &labelText, QWidget *widget,
    int spacing = 2, int labelRelativeFontPointSize = 0);

LIBMVME_EXPORT BoxContainerWithLabel make_hbox_container(
    const QString &labelText, QWidget *widget,
    int spacing = 2, int labelRelativeFontPointSize = 0);

LIBMVME_EXPORT QWidget *make_spacer_widget(QWidget *parent = nullptr);
LIBMVME_EXPORT QToolButton *make_toolbutton(const QString &icon, const QString &text);
LIBMVME_EXPORT QToolButton *make_action_toolbutton(QAction *action = nullptr);

LIBMVME_EXPORT int get_widget_row(QFormLayout *layout, QWidget *widget);

/* Helper class for QLabel which makes the label only grow but never shrink
 * when a new text is set. */
class LIBMVME_EXPORT NonShrinkingLabelHelper
{
    public:
        explicit NonShrinkingLabelHelper(QLabel *label = nullptr)
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
    return qHash(ptr.get(), seed);
}

template<typename LayoutType, int Margin = 2, int Spacing = 2>
LayoutType *make_layout(QWidget *widget = nullptr)
{
    auto ret = new LayoutType(widget);
    ret->setContentsMargins(Margin, Margin, Margin, Margin);
    ret->setSpacing(Spacing);
    return ret;
};

template<int Margin = 2, int Spacing = 2>
QHBoxLayout *make_hbox(QWidget *widget = nullptr)
{
    return make_layout<QHBoxLayout, Margin, Spacing>(widget);
}

template<int Margin = 2, int Spacing = 2>
QVBoxLayout *make_vbox(QWidget *widget = nullptr)
{
    return make_layout<QVBoxLayout, Margin, Spacing>(widget);
}

template<int Margin = 2, int Spacing = 2>
QGridLayout *make_grid(QWidget *widget = nullptr)
{
    return make_layout<QGridLayout, Margin, Spacing>(widget);
}

template<typename WidgetType, typename LayoutType, int Margin = 2, int Spacing = 2>
std::pair<WidgetType *, LayoutType *>make_widget_with_layout(QWidget *parent = nullptr)
{
    auto widget = new WidgetType(parent);
    auto layout = make_layout<LayoutType, Margin, Spacing>(widget);
    widget->setLayout(layout);
    return { widget, layout };
}

class QTextEdit;
class QPushButton;
class QLineEdit;

class LIBMVME_EXPORT TextEditSearchWidget: public QWidget
{
    Q_OBJECT
    public:
        TextEditSearchWidget(QTextEdit *te, QWidget *parent = nullptr);

        QPushButton *getSearchButton();
        QLineEdit *getSearchTextEdit();
        QHBoxLayout *getLayout();

    public slots:
        void focusSearchInput();
        void findNext();
        void searchFor(const QString &text);

    private slots:
        void onSearchTextEdited(const QString &text);

    private:
        void findNext(bool hasWrapped);

        QLineEdit *m_searchInput;
        QPushButton *m_searchButton;
        QTextEdit *m_textEdit;
        QHBoxLayout *m_layout;
};

QWidget *find_top_level_widget(const QString &objectName);

template<typename Result, typename Handler>
QFutureWatcher<Result> *make_watcher(Handler &&handler)
{
    auto watcher = new QFutureWatcher<Result>();
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, [watcher, handler]
    {
        watcher->deleteLater();
        handler(watcher->future());
    });
    return watcher;
}

bool equals(const QRectF &a, const QRectF &b, qreal epsilon = std::numeric_limits<qreal>::epsilon());

template<typename TextEdit>
void append_lines(std::stringstream &oss, TextEdit *textEdit, unsigned indent = 0)
{
    std::string line;
    while (std::getline(oss, line))
    {
        QString indentStr(indent, ' ');
        textEdit->appendPlainText(QSL("%1%2").arg(indentStr).arg(line.c_str()));
    }
}

template<typename ItemWidget>
auto make_copy_to_clipboard_handler(ItemWidget *itemWidget)
{
    auto copy_to_clipboard = [itemWidget]
    {
        auto sm = itemWidget->selectionModel();
        auto idx = sm->currentIndex();

        if (!idx.isValid())
            return;

        auto item = itemWidget->item(idx.row(), idx.column());

        if (!item)
            return;

        auto text = item->data(Qt::EditRole).toString();
        QGuiApplication::clipboard()->setText(text);
    };

    return copy_to_clipboard;
}

template<typename ItemWidget>
QAction *make_copy_to_clipboard_action(ItemWidget *widget, QWidget *actionParent = nullptr)
{
    auto result = new QAction(QIcon::fromTheme("edit-copy"), QSL("Copy"), actionParent);
    result->setShortcuts(QKeySequence::Copy);
    QObject::connect(result, &QAction::triggered, widget, make_copy_to_clipboard_handler(widget));
    return result;
}

#endif /* __QT_UTIL_H__ */
