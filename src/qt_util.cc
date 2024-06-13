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
#include "qt_util.h"

#include <cassert>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <quazipfile.h>

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
    if (m_settings.contains(key))
    {
        widget->restoreGeometry(m_settings.value(key).toByteArray());
    }
}

void WidgetGeometrySaver::addAndRestore(QWidget *widget, const QString &key)
{
    addWidget(widget, key);
    restoreGeometry(widget, key);
}

bool WidgetGeometrySaver::eventFilter(QObject *obj, QEvent *event)
{
    auto widget = qobject_cast<QWidget *>(obj);

    if (event->type() == QEvent::Close)
    {
        if (widget && widget->isVisible() && m_widgetKeys.contains(widget))
        {
            m_settings.setValue(m_widgetKeys[widget], widget->saveGeometry());
            qDebug() << "saved geometry for" << widget << " key =" << m_widgetKeys[widget];
        }
    }
    else if (event->type() == QEvent::Hide)
    {
        if (widget && m_widgetKeys.contains(widget))
        {
            m_settings.setValue(m_widgetKeys[widget], widget->saveGeometry());
            qDebug() << "saved geometry for" << widget << " key =" << m_widgetKeys[widget];
        }
    }

    return QObject::eventFilter(obj, event);
}

namespace
{

QAction *get_close_action(const QWidget *widget)
{
    for (auto action: widget->actions())
    {
        if (action->data().toString() == QSL("WidgetCloseAction"))
            return action;
    }

    return nullptr;
}

}

QAction *add_widget_close_action(QWidget *widget,
                                const QKeySequence &shortcut,
                                Qt::ShortcutContext shortcutContext)
{
    if (auto action = get_close_action(widget))
        return action;

    auto closeAction = new QAction(QSL("Close"), widget);

    closeAction->setData(QSL("WidgetCloseAction"));

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
    loadDynamicProperties(json.toVariantMap(), dest);

}

void loadDynamicProperties(const QVariantMap &properties, QObject *dest)
{
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

void set_widget_font_pointsize(QWidget *widget, float pointSize)
{
    auto font = widget->font();
    font.setPointSizeF(pointSize);
    widget->setFont(font);
}

void set_widget_font_pointsize_relative(QWidget *widget, float relPointSize)
{
    auto font = widget->font();
    font.setPointSizeF(font.pointSizeF() + relPointSize);
    widget->setFont(font);
}

QToolBar *make_toolbar(QWidget *parent)
{
    auto tb = new QToolBar(parent);
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->setIconSize(QSize(16, 16));
    set_widget_font_pointsize_relative(tb, -1.0);
    return tb;
}

QStatusBar *make_statusbar(QWidget *parent)
{
    auto result = new QStatusBar(parent);
    result->setSizeGripEnabled(false);
    set_widget_font_pointsize_relative(result, -2.0);
    return result;
}

void show_and_activate(QWidget *widget)
{
    widget->show();
    widget->showNormal();
    widget->raise();
    widget->activateWindow();
    widget->setWindowState(Qt::WindowActive);
}

QString get_bitness_string()
{
#ifdef Q_PROCESSOR_X86_64
    return QSL("64-bit");
#elif defined Q_PROCESSOR_X86_32
    return QSL("32-bit");
#else
#warning "Unknown processor bitness."
    return QString();
#endif
}

void processQtEvents(QEventLoop::ProcessEventsFlags flags)
{
    QCoreApplication::processEvents(flags);
}

void processQtEvents(int maxtime_ms, QEventLoop::ProcessEventsFlags flags)
{
    QCoreApplication::processEvents(flags, maxtime_ms);
}

VBoxContainerWithLabel make_vbox_container(const QString &labelText, QWidget *widget,
                                           int spacing, int labelRelativeFontPointSize)
{
    auto label = new QLabel(labelText);
    label->setAlignment(Qt::AlignCenter);
    set_widget_font_pointsize_relative(label, labelRelativeFontPointSize);

    auto container = std::make_unique<QWidget>();
    auto layout = new QVBoxLayout(container.get());
    layout->setContentsMargins(0, 0, 0, 0);

    layout->setSpacing(spacing);
    layout->addWidget(label, 0, Qt::AlignCenter);
    layout->addWidget(widget, 0, Qt::AlignCenter);

    VBoxContainerWithLabel result { std::move(container), layout, label, widget };

    return result;
}

QWidget *make_spacer_widget(QWidget *parent)
{
    auto result = new QWidget(parent);
    result->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return result;
}

QToolButton *make_toolbutton(const QString &icon, const QString &text)
{
    auto result = new QToolButton;
    result->setIcon(QIcon(icon));
    result->setText(text);
    result->setStatusTip(text);
    result->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    auto font = result->font();
    font.setPointSize(7);
    result->setFont(font);
    return result;
}

QToolButton *make_action_toolbutton(QAction *action)
{
    auto result = new QToolButton;
    if (action)
        result->setDefaultAction(action);
    result->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    auto font = result->font();
    font.setPointSize(7);
    result->setFont(font);
    return result;
}

int get_widget_row(QFormLayout *layout, QWidget *widget)
{
    assert(layout);
    assert(widget);

    int row;
    QFormLayout::ItemRole role;

    layout->getWidgetPosition(widget, &row, &role);

    return row;
}

// TextEditSearchWidget

TextEditSearchWidget::TextEditSearchWidget(QTextEdit *te, QWidget *parent)
    : QWidget(parent)
    , m_searchInput(new QLineEdit)
    , m_searchButton(new QPushButton("Find"))
    , m_textEdit(te)
{
    assert(te);

    connect(m_searchInput, &QLineEdit::textEdited,
            this, &TextEditSearchWidget::onSearchTextEdited);

    connect(m_searchInput, &QLineEdit::returnPressed,
            this, [this]() { findNext(); });

    connect(m_searchButton, &QPushButton::clicked,
            this, [this]() { findNext(); });

    m_searchInput->setMinimumWidth(80);

    auto layout = make_layout<QHBoxLayout>(this);
    layout->addWidget(m_searchInput);
    layout->addWidget(m_searchButton);
    layout->setStretch(0, 1);
}

void TextEditSearchWidget::findNext()
{
    findNext(false);
}

void TextEditSearchWidget::findNext(bool hasWrapped)
{
    auto searchText = m_searchInput->text();
    bool found      = m_textEdit->find(searchText);

    if (!found && !hasWrapped)
    {
        auto currentCursor = m_textEdit->textCursor();
        currentCursor.setPosition(0);
        m_textEdit->setTextCursor(currentCursor);
        findNext(true);
    }
}

void TextEditSearchWidget::onSearchTextEdited(const QString &)
{
    /* Move the cursor to the beginning of the current word, then search
     * forward from that position. */
    auto currentCursor = m_textEdit->textCursor();
    currentCursor.movePosition(QTextCursor::StartOfWord);
    m_textEdit->setTextCursor(currentCursor);
    findNext();
}

void TextEditSearchWidget::focusSearchInput()
{
    if (m_searchInput->hasFocus())
        m_searchInput->selectAll();
    else
        m_searchInput->setFocus();
}

QWidget *find_top_level_widget(const QString &objectName)
{
    for (auto &w: QApplication::topLevelWidgets())
        if (w->objectName() == objectName)
            return w;
    return {};
}
