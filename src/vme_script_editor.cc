#include "vme_script_editor.h"
#include "mvme_context.h"
#include "vme_script.h"
#include "gui_util.h"
#include "mvme.h"

#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStatusTipEvent>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

static const int tabStop = 4;

struct VMEScriptEditorPrivate
{
    VMEScriptEditor *m_q;

    MVMEContext *m_context;
    VMEScriptConfig *m_script;

    QToolBar *m_toolBar;
    QPlainTextEdit *m_editor;
    QStatusBar *m_statusBar;

    QLabel *m_labelPosition;

    void updateCursorPosition()
    {
        auto cursor = m_editor->textCursor();
        int col = cursor.positionInBlock();
        int line = cursor.blockNumber() + 1;

        m_labelPosition->setText(QString(QSL("L%1 C%2 ")
                                         .arg(line, 3)
                                         .arg(col, 3)
                                        ));
    }
};

VMEScriptEditor::VMEScriptEditor(MVMEContext *context, VMEScriptConfig *script, QWidget *parent)
    : MVMEWidget(parent)
    , m_d(new VMEScriptEditorPrivate)
{
    m_d->m_q = this;
    m_d->m_context = context;
    m_d->m_script = script;
    m_d->m_toolBar = make_toolbar();
    m_d->m_editor = new QPlainTextEdit;
    m_d->m_statusBar = make_statusbar();
    m_d->m_labelPosition = new QLabel;

    // Editor area
    new vme_script::SyntaxHighlighter(m_d->m_editor->document());

    auto font = QFont("Monospace", 8);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    m_d->m_editor->setFont(font);

    {
        // Tab width calculation
        QString spaces;
        for (int i = 0; i < tabStop; ++i)
            spaces += " ";
        QFontMetrics metrics(font);
        m_d->m_editor->setTabStopWidth(metrics.width(spaces));
    }

    connect(script, &VMEScriptConfig::modified, this, &VMEScriptEditor::onScriptModified);

    auto parentConfig = qobject_cast<ConfigObject *>(m_d->m_script->parent());

    if (parentConfig)
        connect(parentConfig, &ConfigObject::modified, this, &VMEScriptEditor::updateWindowTitle);

    m_d->m_editor->setPlainText(m_d->m_script->getScriptContents());
    updateWindowTitle();

    connect(m_d->m_editor->document(), &QTextDocument::contentsChanged, this, &VMEScriptEditor::onEditorTextChanged);
    connect(m_d->m_editor, &QPlainTextEdit::cursorPositionChanged, this, [this] { m_d->updateCursorPosition(); });

    // Toolbar actions
    m_d->m_toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    QAction *action;

    action = m_d->m_toolBar->addAction(QIcon(":/script-run.png"), QSL("Run"), this,  &VMEScriptEditor::runScript);
    action->setStatusTip(QSL("Run the VME script"));

    action = m_d->m_toolBar->addAction(QIcon(":/dialog-ok-apply.png"), QSL("Apply"), this, &VMEScriptEditor::apply);
    action->setStatusTip(QSL("Apply any changes to the active VME configuration"));

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
    m_d->m_toolBar->addAction(QIcon(":/document-revert.png"), "Revert Changes", this, &VMEScriptEditor::revert);
    m_d->m_toolBar->addSeparator();

    // Add the script Help action from the main window
    m_d->m_toolBar->addAction(m_d->m_context->getMainWindow()->findChild<QAction *>("actionVMEScriptRef"));

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

    m_d->updateCursorPosition();
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
    if (!isModified)
        return;

    // TODO: ask about reloading from the config or keeping the current text editor content
    // This should not happen unless the config has been modified by something other than this editor.
    //m_d->m_editor->setText(m_d->m_script->getScriptContents());

    updateWindowTitle();
}

void VMEScriptEditor::onEditorTextChanged()
{
    updateWindowTitle();
}

void VMEScriptEditor::runScript()
{
    try
    {
        auto moduleConfig = qobject_cast<ModuleConfig *>(m_d->m_script->parent());
        auto script = vme_script::parse(m_d->m_editor->toPlainText(),
                                        moduleConfig ? moduleConfig->getBaseAddress() : 0);

        m_d->m_context->logMessage(QString("Running script %1:").arg(m_d->m_script->objectName()));

        auto logger = [this](const QString &str) { m_d->m_context->logMessage(QSL("  ") + str); };
        auto results = m_d->m_context->runScript(script, logger);

        for (auto result: results)
            logger(format_result(result));
    }
    catch (const vme_script::ParseError &e)
    {
        m_d->m_context->logMessage(QSL("Parse error: ") + e.what());
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
                                                    QSL("VME scripts (*.vme);; All Files (*)"));
    if (!fileName.isEmpty())
    {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly))
        {
            QTextStream stream(&file);
            m_d->m_editor->setPlainText(stream.readAll());
            QFileInfo fi(fileName);
            settings.setValue("Files/LastVMEScriptDirectory", fi.absolutePath());
        }
    }
}

void VMEScriptEditor::loadFromTemplate()
{
    TemplateLoader loader;
    connect(&loader, &TemplateLoader::logMessage, m_d->m_context, &MVMEContext::logMessage);
    QString path = loader.getTemplatePath();

    if (!path.isEmpty())
    {
        QString fileName = QFileDialog::getOpenFileName(this, QSL("Load vme script file"), path,
                                                        QSL("VME scripts (*.vme);; All Files (*)"));
        if (!fileName.isEmpty())
        {
            QFile file(fileName);
            if (file.open(QIODevice::ReadOnly))
            {
                QTextStream stream(&file);
                m_d->m_editor->setPlainText(stream.readAll());
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
                                                    QSL("VME scripts (*.vme);; All Files (*)"));

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
    {
        fileName += ".vme";
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
    m_d->m_editor->setPlainText(m_d->m_script->getScriptContents());
    m_d->m_editor->document()->setModified(false);
    updateWindowTitle();
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
