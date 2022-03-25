/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "vme_script_editor.h"

#include <QFileDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStatusTipEvent>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <mesytec-mvlc/util/logging.h>

#include "gui_util.h"
#include "mvme.h"
#include "mvme_qthelp.h"
#include "util/qt_font.h"
#include "vme_config_scripts.h"
#include "vme_script.h"
#include "vme_script_util.h"

static const int TabStop = 4;

struct VMEScriptEditorPrivate
{
    VMEScriptEditor *m_q;

    VMEScriptConfig *m_script;

    QToolBar *m_toolBar;
    CodeEditor *m_editor;
    QStatusBar *m_statusBar;

    QLabel *m_labelPosition;

    void updateCursorPositionLabel()
    {
        auto cursor = m_editor->textCursor();
        int col = cursor.positionInBlock();
        int line = cursor.blockNumber() + 1;

        m_labelPosition->setText(QString(QSL("L%1 C%2 ")
                                         .arg(line, 3)
                                         .arg(col, 3)
                                        ));
    }

    QWidget *searchWindow;
    QLineEdit *searchInput;
    QPushButton *findNext;
    //QPushButton *findPrev;
};

VMEScriptEditor::VMEScriptEditor(VMEScriptConfig *script, QWidget *parent)
    : MVMEWidget(parent)
    , m_d(new VMEScriptEditorPrivate)
{
    m_d->m_q = this;
    m_d->m_script = script;
    m_d->m_toolBar = make_toolbar();
    m_d->m_editor = new CodeEditor;
    m_d->m_statusBar = make_statusbar();
    m_d->m_labelPosition = new QLabel;

    //
    // Search Widget
    //
    {
        m_d->searchWindow = new QWidget(this);
        //m_d->searchWindow->setWindowFlags(Qt::Tool
        //                                | Qt::CustomizeWindowHint
        //                                | Qt::WindowTitleHint
        //                                | Qt::WindowCloseButtonHint
        //                                );
        m_d->searchWindow->setWindowTitle(QSL("Search"));

        auto hideAction = new QAction(QSL("Close"), m_d->searchWindow);
        hideAction->setShortcut(QKeySequence::Cancel);
        hideAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_d->searchWindow->addAction(hideAction);
        connect(hideAction, &QAction::triggered, m_d->searchWindow, &QWidget::hide);

        m_d->searchInput = new QLineEdit;
        m_d->searchInput->setMinimumWidth(80);
        m_d->findNext = new QPushButton(QSL("Find"));
        //m_d->findPrev = new QPushButton(QSL("&Prev"));
        //m_d->findPrev->setVisible(false); // FIXME: findPrev() is not working

        connect(m_d->searchInput, &QLineEdit::textEdited, this, &VMEScriptEditor::onSearchTextEdited);
        connect(m_d->searchInput, &QLineEdit::returnPressed, this, [this] () { findNext(); });
        connect(m_d->findNext, &QPushButton::clicked, this, &VMEScriptEditor::findNext);
        //connect(m_d->findPrev, &QPushButton::clicked, this, &VMEScriptEditor::findPrev);

        auto layout = new QHBoxLayout(m_d->searchWindow);
        layout->addWidget(m_d->searchInput);
        layout->addWidget(m_d->findNext);
        //layout->addWidget(m_d->findPrev);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(2);
        layout->setStretch(0, 1);

        auto shortcut = new QShortcut(QSL("Ctrl+F"), this);
        connect(shortcut, &QShortcut::activated, this, &VMEScriptEditor::search);
    }

    // Editor area
    new vme_script::SyntaxHighlighter(m_d->m_editor->document());

    auto font = make_monospace_font();
    font.setPointSize(8);
    m_d->m_editor->setFont(font);
    set_tabstop_width(m_d->m_editor, TabStop);

    //qDebug() << __PRETTY_FUNCTION__ << "editor font key is:" << m_d->m_editor->font().key();

    connect(script, &VMEScriptConfig::modified, this, &VMEScriptEditor::onScriptModified);

    auto parentConfig = qobject_cast<ConfigObject *>(m_d->m_script->parent());

    if (parentConfig)
        connect(parentConfig, &ConfigObject::modified, this, &VMEScriptEditor::updateWindowTitle);

    m_d->m_editor->setPlainText(m_d->m_script->getScriptContents());

    auto pal = m_d->m_editor->palette();
    auto color = pal.color(QPalette::Active, QPalette::Highlight);
    pal.setColor(QPalette::Inactive, QPalette::Highlight, color);
    color = pal.color(QPalette::Active, QPalette::HighlightedText);
    pal.setColor(QPalette::Inactive, QPalette::HighlightedText, color);
    m_d->m_editor->setPalette(pal);

    updateWindowTitle();

    connect(m_d->m_editor->document(), &QTextDocument::contentsChanged, this, &VMEScriptEditor::onEditorTextChanged);
    connect(m_d->m_editor, &QPlainTextEdit::cursorPositionChanged, this, [this] { m_d->updateCursorPositionLabel(); });

    // Toolbar actions
    m_d->m_toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    QAction *action = {};

    action = m_d->m_toolBar->addAction(QIcon(":/script-run.png"), QSL("Run"), this,  &VMEScriptEditor::runScript_);
    action->setStatusTip(QSL("Run the VME script"));
    action->setShortcut(QSL("Ctrl+R"));

    action = m_d->m_toolBar->addAction(QIcon(":/dialog-ok-apply.png"), QSL("Apply"), this, &VMEScriptEditor::apply);
    action->setStatusTip(QSL("Apply any changes to the active VME configuration"));
    action->setShortcut(QSL("Ctrl+S"));

    action = m_d->m_toolBar->addAction(QIcon(":/dialog-close.png"), QSL("Close"), this, &VMEScriptEditor::close);
    action->setStatusTip(QSL("Close this window"));

    m_d->m_toolBar->addSeparator();

    auto loadMenu = new QMenu;
    loadMenu->addAction(QSL("from file"), this, &VMEScriptEditor::loadFromFile);
    loadMenu->addAction(QSL("from template"), this, &VMEScriptEditor::loadFromTemplate);
    auto loadAction = m_d->m_toolBar->addAction(QIcon(":/document-open.png"), QSL("Load"));
    loadAction->setMenu(loadMenu);

    auto loadButton = qobject_cast<QToolButton *>(m_d->m_toolBar->widgetForAction(loadAction));

    if (loadButton)
        loadButton->setPopupMode(QToolButton::InstantPopup);

    m_d->m_toolBar->addAction(QIcon(":/document-save-as.png"), "Save to file", this, &VMEScriptEditor::saveToFile);

    m_d->m_toolBar->addSeparator();
    action = m_d->m_toolBar->addAction(QIcon(":/document-revert.png"), "Revert Changes", this, &VMEScriptEditor::revert);
    action->setStatusTip(QSL("Reload the VME Script from the current VME configuration"));

    m_d->m_toolBar->addSeparator();

    m_d->m_toolBar->addAction(
        QIcon(QSL(":/help.png")), QSL("VME Script Help"),
        this, mesytec::mvme::make_help_keyword_handler("VMEScript"));

#if 0
    action = m_d->m_toolBar->addAction(
        QIcon(":/script-run.png"), QSL("(Dev) Run Writes Batched"),
        this,  &VMEScriptEditor::runScriptWritesBatched_);
#endif

    action->setStatusTip(QSL("Run the VME script"));
    action->setShortcut(QSL("Ctrl+R"));

    m_d->m_toolBar->addSeparator();

    // Search input field and button
    m_d->m_toolBar->addWidget(m_d->searchWindow);

    // Statusbar and info widgets
    m_d->m_statusBar->addPermanentWidget(m_d->m_labelPosition);

    set_widget_font_pointsize(m_d->m_labelPosition, 7);
    {
        auto font = m_d->m_labelPosition->font();
        font.setStyleHint(QFont::Monospace);
        m_d->m_labelPosition->setFont(font);
    }

    // Main layout
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(m_d->m_toolBar);
    layout->addWidget(m_d->m_editor);
    layout->addWidget(m_d->m_statusBar);

    m_d->m_editor->setFocus();
    m_d->updateCursorPositionLabel();
    setWindowIcon(QIcon(QPixmap(":/vme_script.png")));
    resize(800, 600);
}

VMEScriptEditor::~VMEScriptEditor()
{
    delete m_d;
}

bool VMEScriptEditor::event(QEvent *e)
{
    if (e->type() == QEvent::StatusTip)
    {
        m_d->m_statusBar->showMessage(reinterpret_cast<QStatusTipEvent *>(e)->tip());
        return true;
    }

    return QWidget::event(e);
}

bool VMEScriptEditor::isModified() const
{
    return m_d->m_editor->document()->isModified();
}

void VMEScriptEditor::updateWindowTitle()
{
    auto title = m_d->m_script->getVerboseTitle();

    if (m_d->m_editor->document()->isModified())
        title += QSL(" *");

    setWindowTitle(title);
}

void VMEScriptEditor::onScriptModified(bool isModified)
{
    mesytec::mvlc::get_logger("vme_script_editor")->debug(
        "onScriptModified(isModified={})", isModified);

    if (!isModified)
        return;

    if (m_d->m_editor->toPlainText() != m_d->m_script->getScriptContents())
    {
        mesytec::mvlc::get_logger("vme_script_editor")->debug(
            "editor text != script text -> calling editor->setPlaintText()");
        // Store the current vertical scrollbar position, update the textedit with
        // the new text and restore the scrollbar position.
        auto pos = m_d->m_editor->verticalScrollBar()->sliderPosition();
        m_d->m_editor->setPlainText(m_d->m_script->getScriptContents());
        m_d->m_editor->verticalScrollBar()->setSliderPosition(pos);
    }

    updateWindowTitle();
}

void VMEScriptEditor::onEditorTextChanged()
{
    updateWindowTitle();
}

void VMEScriptEditor::runScript_()
{
    try
    {
        // We want to execute the text that's currently in the editor window
        // using the variables visible to the underlying (unmodified)
        // VMEScriptConfig object. So first collect the symbol tables, then get
        // the script text and finally parse the text, passing in the list of
        // symbol tables.

        auto symtabs = mesytec::mvme::collect_symbol_tables(m_d->m_script);

        auto moduleConfig = qobject_cast<ModuleConfig *>(m_d->m_script->parent());
        u32 baseAddress = moduleConfig ? moduleConfig->getBaseAddress() : 0u;
        auto scriptText = m_d->m_editor->toPlainText();

        auto script = vme_script::parse(scriptText, symtabs, baseAddress);

        emit logMessage(QString("Running script \"%1\":").arg(m_d->m_script->getVerboseTitle()));
        emit runScript(script);
    }
    catch (const vme_script::ParseError &e)
    {
        emit logMessage(QSL("Parse error: ") + e.toString());
    }
}

void VMEScriptEditor::runScriptWritesBatched_()
{
    try
    {
        // We want to execute the text that's currently in the editor window
        // using the variables visible to the underlying (unmodified)
        // VMEScriptConfig object. So first collect the symbol tables, then get
        // the script text and finally parse the text, passing in the list of
        // symbol tables.

        auto symtabs = mesytec::mvme::collect_symbol_tables(m_d->m_script);

        auto moduleConfig = qobject_cast<ModuleConfig *>(m_d->m_script->parent());
        u32 baseAddress = moduleConfig ? moduleConfig->getBaseAddress() : 0u;
        auto scriptText = m_d->m_editor->toPlainText();

        auto script = vme_script::parse(scriptText, symtabs, baseAddress);

        emit logMessage(QString("Running script writes batched \"%1\":").arg(m_d->m_script->getVerboseTitle()));
        emit runScriptWritesBatched(script);
    }
    catch (const vme_script::ParseError &e)
    {
        emit logMessage(QSL("Parse error: ") + e.toString());
    }
}

void VMEScriptEditor::loadFromFile()
{
    QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    QSettings settings;
    if (settings.contains("Files/LastVMEScriptDirectory"))
    {
        path = settings.value("Files/LastVMEScriptDirectory").toString();
    }

    QString fileName = QFileDialog::getOpenFileName(this, QSL("Load vme script file"), path,
                                                    QSL("VME scripts (*.vmescript *.vme);; All Files (*)"));
    if (!fileName.isEmpty())
    {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly))
        {
            QTextStream stream(&file);
            m_d->m_editor->setPlainText(stream.readAll());
            m_d->m_editor->document()->setModified(true);
            QFileInfo fi(fileName);
            settings.setValue("Files/LastVMEScriptDirectory", fi.absolutePath());
        }
    }
}

void VMEScriptEditor::loadFromTemplate()
{
    QString path = vats::get_template_path();

    if (auto module = qobject_cast<ModuleConfig *>(m_d->m_script->parent()))
    {
        path = vats::get_module_path(module->getModuleMeta().typeName) + QSL("/vme");
    }

    if (!path.isEmpty())
    {
        QString fileName = QFileDialog::getOpenFileName(this, QSL("Load vme script file"), path,
                                                        QSL("VME scripts (*.vmescript *.vme);; All Files (*)"));
        if (!fileName.isEmpty())
        {
            QFile file(fileName);
            if (file.open(QIODevice::ReadOnly))
            {
                QTextStream stream(&file);
                m_d->m_editor->setPlainText(stream.readAll());
                m_d->m_editor->document()->setModified(true);
            }
        }
    }
}

void VMEScriptEditor::saveToFile()
{
    QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    QSettings settings;
    if (settings.contains("Files/LastVMEScriptDirectory"))
    {
        path = settings.value("Files/LastVMEScriptDirectory").toString();
    }

    QString fileName = QFileDialog::getSaveFileName(this, QSL("Save vme script file"), path,
                                                    QSL("VME scripts (*.vmescript *.vme);; All Files (*)"));

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
    {
        fileName += ".vmescript";
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, "File error", QString("Error opening \"%1\" for writing").arg(fileName));
        return;
    }

    QTextStream stream(&file);
    stream << m_d->m_editor->toPlainText();

    if (stream.status() != QTextStream::Ok)
    {
        QMessageBox::critical(this, "File error", QString("Error writing to \"%1\"").arg(fileName));
        return;
    }

    settings.setValue("Files/LastVMEScriptDirectory", fi.absolutePath());
}

void VMEScriptEditor::apply()
{
    auto contents = m_d->m_editor->toPlainText();
    m_d->m_script->setScriptContents(contents);
    m_d->m_editor->document()->setModified(false);
    updateWindowTitle();
}

void VMEScriptEditor::revert()
{
    auto pos = m_d->m_editor->verticalScrollBar()->sliderPosition();
    m_d->m_editor->setPlainText(m_d->m_script->getScriptContents());
    m_d->m_editor->verticalScrollBar()->setSliderPosition(pos);

    m_d->m_editor->document()->setModified(false);
    updateWindowTitle();
}

void VMEScriptEditor::search()
{
    if (!m_d->searchWindow->isVisible())
    {
        // move the search window close to the top-right corner
        QPoint pos = this->mapToGlobal(
            QPoint(this->width() - m_d->searchWindow->sizeHint().width(),
                   75));
        m_d->searchWindow->move(pos);
    }
    m_d->searchWindow->show();
    m_d->searchWindow->raise();

    if (m_d->searchInput->hasFocus())
    {
        m_d->searchInput->selectAll();
    }
    else
    {
        m_d->searchWindow->activateWindow();
        m_d->searchInput->setFocus();
    }
}

void VMEScriptEditor::onSearchTextEdited(const QString &/*text*/)
{
    /* Move the cursor to the beginning of the current word, then search
     * forward from that position. */
    auto currentCursor = m_d->m_editor->textCursor();
    currentCursor.movePosition(QTextCursor::StartOfWord);
    m_d->m_editor->setTextCursor(currentCursor);
    findNext();
}

void VMEScriptEditor::findNext(bool hasWrapped)
{
    auto searchText = m_d->searchInput->text();
    bool found      = m_d->m_editor->find(searchText);

    if (!found && !hasWrapped)
    {
        auto currentCursor = m_d->m_editor->textCursor();
        currentCursor.setPosition(0);
        m_d->m_editor->setTextCursor(currentCursor);
        findNext(true);
    }
}

void VMEScriptEditor::findPrev()
{
    // FIXME: findPrev() is not working. Not sure why. Qt has has some bugs
    // related to backward searching though.
    auto searchText     = m_d->searchInput->text();
#if 0
    auto currentCursor  = m_d->m_editor->textCursor();
    currentCursor.clearSelection();
    currentCursor.movePosition(QTextCursor::StartOfWord);
    m_d->m_editor->setTextCursor(currentCursor);

    auto doc            = m_d->m_editor->document();
    auto cursor         = doc->find(searchText, currentCursor, QTextDocument::FindBackward);

    if (!cursor.isNull())
    {
        bool wasModified = doc->isModified();
        m_d->m_editor->setTextCursor(cursor);
        doc->setModified(wasModified);
        updateWindowTitle();
    }
#else
    m_d->m_editor->find(searchText, QTextDocument::FindBackward);
#endif
}

void VMEScriptEditor::closeEvent(QCloseEvent *event)
{
    bool doClose = !m_d->m_editor->document()->isModified();

    if (m_d->m_editor->document()->isModified())
    {
        auto response = QMessageBox::question(this, QSL("Apply changes?"),
                                              QSL("The script was modified. Do you want to apply the changes?"),
                                              QMessageBox::Apply | QMessageBox::Discard | QMessageBox::Cancel);

        if (response == QMessageBox::Apply)
        {
            apply();
            doClose = true;
        }
        else if (response == QMessageBox::Discard)
        {
            doClose = true;
        }
    }

    if (doClose)
        MVMEWidget::closeEvent(event);
    else
        event->ignore();
}

CodeEditor *VMEScriptEditor::textEdit()
{
    return m_d->m_editor;
}

QString VMEScriptEditor::toPlainText() const
{
    return m_d->m_editor->toPlainText();
}
