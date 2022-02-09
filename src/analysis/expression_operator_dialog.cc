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
#include "expression_operator_dialog.h"
#include "expression_operator_dialog_p.h"

#include "analysis/a2/a2_impl.h"
#include "analysis/a2_adapter.h"
#include "analysis/analysis.h"
#include "analysis/analysis_ui_p.h"
#include "analysis/analysis_util.h"
#include "analysis/ui_eventwidget.h"
#include "mvme_context_lib.h"
#include "qt_util.h"
#include "util/cpp17_algo.h"
#include "util/qt_font.h"

#include <QApplication>
#include <QHeaderView>
#include <QTabWidget>
#include <QTextBrowser>

/* NOTES and TODO:
 - workflow:
   select inputs, write init script, run init script, check output definition is as desired,
   write step script, test with sample data, accept changes

 - implement copy/paste for editable PipeView

 - remove the part that updates the gui from model_compileXYZ(). Instead make the gui
   update calls explicit.
 - add a flags argument to repopulateGUIFromModel() with the option to only rebuild parts
   of the gui (i.e. leave the slotgrid intact if nothing changed there)
 - as an alternative to the flags improve the slotgrid repopulate code to reuse existing
   items and only clear things that are not needed anymore.
 - store the result of the compilations somewhere. specifically the Step part must know if
   the Begin part has an error and thus Step was never compiled.

 */

namespace analysis
{
namespace ui
{

//
// InputSelectButton
//

InputSelectButton::InputSelectButton(Slot *destSlot, EventWidget *eventWidget, QWidget *parent)
    : QPushButton(QSL("<select>"), parent)
    , m_eventWidget(eventWidget)
    , m_destSlot(destSlot)
{
    setCheckable(true);
    setMouseTracking(true);
    installEventFilter(this);
}

bool InputSelectButton::eventFilter(QObject *watched, QEvent *event)
{
    assert(watched == this);

    if (!isChecked() && (event->type() == QEvent::Enter || event->type() == QEvent::Leave))
    {
        m_eventWidget->highlightInputOf(m_destSlot, event->type() == QEvent::Enter);
    }

    return false; // Do not filter the event out.
}

//
// ExpressionOperatorPipeView
//
static const int PipeView_ValidColumn = 0;
static const int PipeView_DataColumn  = 1;

ExpressionOperatorPipeView::ExpressionOperatorPipeView(QWidget *parent)
    : QWidget(parent)
    , m_unitLabel(new QLabel(this))
    , m_tableWidget(new QTableWidget(this))
    , m_a2Pipe{}
    , m_dataEditable(false)
{
    auto infoLayout = new QFormLayout;
    infoLayout->addRow(QSL("Unit:"), m_unitLabel);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(infoLayout);
    layout->addWidget(m_tableWidget);

    // columns:
    // Valid, Value, lower Limit, upper Limit
    m_tableWidget->setColumnCount(4);
    m_tableWidget->setHorizontalHeaderLabels({"Valid", "Value", "Lower Limit", "Upper Limit"});

    connect(m_tableWidget, &QTableWidget::cellChanged,
            this, &ExpressionOperatorPipeView::onCellChanged);

    m_tableWidget->installEventFilter(this);

    refresh();
}

void ExpressionOperatorPipeView::showEvent(QShowEvent *event)
{
    m_tableWidget->resizeRowsToContents();
    m_tableWidget->resizeColumnsToContents();

    QWidget::showEvent(event);
}

void ExpressionOperatorPipeView::setPipe(const a2::PipeVectors &a2_pipe,
                                         const QString &unit)
{
    m_a2Pipe = a2_pipe;
    m_unitLabel->setText(unit.isEmpty() ? QSL("&lt;empty&gt;") : unit);
    refresh();
}

void ExpressionOperatorPipeView::refresh()
{
    QSignalBlocker sb(m_tableWidget);

    const auto &pipe = m_a2Pipe;

    if (pipe.data.size < 0)
    {
        m_tableWidget->setRowCount(0);
        return;
    }

    m_tableWidget->setRowCount(pipe.data.size);

    for (s32 pi = 0; pi < pipe.data.size; pi++)
    {
        double param = pipe.data[pi];
        double lowerLimit = pipe.lowerLimits[pi];
        double upperLimit = pipe.upperLimits[pi];

        QStringList columns =
        {
            a2::is_param_valid(param) ? QSL("Y") : QSL("N"),
            a2::is_param_valid(param) ? QString::number(param) : QSL(""),
            QString::number(lowerLimit),
            QString::number(upperLimit),
        };

        for (s32 ci = 0; ci < columns.size(); ci++)
        {
            auto item = m_tableWidget->item(pi, ci);

            if (!item)
            {
                item = new QTableWidgetItem;
                m_tableWidget->setItem(pi, ci, item);
            }


            Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;

            if (ci == PipeView_DataColumn && isDataEditable())
            {
                item->setData(Qt::DisplayRole, param);
                flags |= Qt::ItemIsEditable;
            }
            else
            {
                item->setText(columns[ci]);
            }

            item->setFlags(flags);
        }

        if (!m_tableWidget->verticalHeaderItem(pi))
        {
            m_tableWidget->setVerticalHeaderItem(pi, new QTableWidgetItem);
        }

        m_tableWidget->verticalHeaderItem(pi)->setText(QString::number(pi));
    }

    m_tableWidget->resizeColumnsToContents();
    m_tableWidget->resizeRowsToContents();
}

void ExpressionOperatorPipeView::onCellChanged(int row, int col)
{
    if (!isDataEditable()) return;

    if (col == PipeView_DataColumn && row < m_a2Pipe.data.size)
    {
        m_a2Pipe.data[row] = m_tableWidget->item(row, PipeView_DataColumn)
            ->data(Qt::EditRole).toDouble();

        m_tableWidget->item(row, PipeView_ValidColumn)->setText(QSL("Y"));
    }
}

bool ExpressionOperatorPipeView::eventFilter(QObject *watched, QEvent *event)
{
    assert(watched == m_tableWidget);

    if (event->type() == QEvent::KeyPress)
    {
        auto keyEvent = reinterpret_cast<QKeyEvent *>(event);

        if (keyEvent->matches(QKeySequence::Copy))
        {
            copy();
            return true;
        }
        else if (keyEvent->matches(QKeySequence::Paste))
        {
            paste();
            return true;
        }
    }

    return false;
}

void ExpressionOperatorPipeView::copy()
{
    qDebug() << __PRETTY_FUNCTION__;
    if (!isDataEditable()) return;
    // TODO: implement copy()
}

void ExpressionOperatorPipeView::paste()
{
    qDebug() << __PRETTY_FUNCTION__;
    if (!isDataEditable()) return;
    // TODO: implement paste()
}

//
// ExpressionOperatorPipesComboView
//
ExpressionOperatorPipesComboView::ExpressionOperatorPipesComboView(QWidget *parent)
    : QWidget(parent)
    , m_selectCombo(new QComboBox)
    , m_pipeStack(new QStackedWidget)
    , m_pipeDataEditable(false)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(m_selectCombo);
    layout->addWidget(m_pipeStack);

    connect(m_selectCombo, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
            m_pipeStack, &QStackedWidget::setCurrentIndex);
}

void ExpressionOperatorPipesComboView::setPipes(const std::vector<a2::PipeVectors> &pipes,
                                           const QStringList &titles,
                                           const QStringList &units)
{
    assert(static_cast<s32>(pipes.size()) == titles.size());
    assert(static_cast<s32>(pipes.size()) == units.size());


    s32 newSize   = titles.size();
    s32 prevIndex = m_selectCombo->currentIndex();

    while (m_selectCombo->count() > newSize)
    {
        s32 idx = m_selectCombo->count() - 1;

        m_selectCombo->removeItem(idx);

        if (auto pv = m_pipeStack->widget(idx))
        {
            m_pipeStack->removeWidget(pv);
            pv->deleteLater();
        }
    }

    //qDebug() << this << "sizes: stack:" << m_pipeStack->count()
    //    << ", combo:" << m_selectCombo->count();

    for (s32 pi = 0; pi < newSize; pi++)
    {
        if (pi < m_selectCombo->count())
        {
            if (auto pv = qobject_cast<ExpressionOperatorPipeView *>(m_pipeStack->widget(pi)))
            {
                m_selectCombo->setItemText(pi, titles[pi]);
                pv->setPipe(pipes[pi], units[pi]);
                pv->setDataEditable(isPipeDataEditable());
                //qDebug() << this << "existing pi:" << pi << ", pv: " << pv;
            }
        }
        else
        {
            m_selectCombo->addItem(titles[pi]);
            auto pv = new ExpressionOperatorPipeView;
            pv->setPipe(pipes[pi], units[pi]);
            pv->setDataEditable(isPipeDataEditable());
            m_pipeStack->addWidget(pv);
            //qDebug() << this << "new pi:" << pi << ", pv: " << pv;
        }
    }

    //qDebug() << this << "stack:" << m_pipeStack->count()
    //    << ", combo:" << m_selectCombo->count();

    if (newSize > 0)
    {
        prevIndex = prevIndex >= 0 ? prevIndex : 0;
    }
    else
    {
        prevIndex = -1;
    }

    m_selectCombo->setCurrentIndex(prevIndex);

    //qDebug() << this << "count = " << m_selectCombo->count();
}

void ExpressionOperatorPipesComboView::refresh()
{
    for (s32 pi = 0; pi < m_pipeStack->count(); pi++)
    {
        if (auto pv = qobject_cast<ExpressionOperatorPipeView *>(m_pipeStack->widget(pi)))
        {
            pv->refresh();
        }
    }
}

void ExpressionOperatorPipesComboView::setPipeDataEditable(bool editable)
{
    for (s32 pi = 0; pi < m_pipeStack->count(); pi++)
    {
        if (auto pv = qobject_cast<ExpressionOperatorPipeView *>(m_pipeStack->widget(pi)))
        {
            pv->setDataEditable(editable);
        }
    }

    m_pipeDataEditable = editable;
}

bool ExpressionOperatorPipesComboView::isPipeDataEditable() const
{
    return m_pipeDataEditable;
}

//
// ExpressionErrorWidget
//
ExpressionErrorWidget::ExpressionErrorWidget(QWidget *parent)
    : QWidget(parent)
    , m_errorTable(new QTableWidget)
{
    QStringList headerLabels = { QSL("Type"), QSL("Message"), QSL("Line"), QSL("Column") };

    m_errorTable->setColumnCount(headerLabels.size());
    m_errorTable->setHorizontalHeaderLabels(headerLabels);
    m_errorTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto layout = new QHBoxLayout(this);
    layout->addWidget(m_errorTable);
    layout->setContentsMargins(0, 0, 0, 0);

    connect(m_errorTable, &QTableWidget::cellClicked,
            this, &ExpressionErrorWidget::onCellClicked);

    connect(m_errorTable, &QTableWidget::cellDoubleClicked,
            this, &ExpressionErrorWidget::onCellDoubleClicked);
}

void ExpressionErrorWidget::setError(const std::exception_ptr &ep)
{
    clear();
    prepareEntries(ep);
    populateTable();
    assertConsistency();
}

void ExpressionErrorWidget::prepareEntries(const std::exception_ptr &ep)
{
    try
    {
        std::rethrow_exception(ep);
    }
    catch (const a2::a2_exprtk::ParserErrorList &errorList)
    {
        for (const auto &pe: errorList)
        {
            Entry entry(Entry::Type::ParserError);
            entry.parserError = pe;
            m_entries.push_back(entry);
        }
    }
    catch (const a2::ExpressionOperatorSemanticError &e)
    {
        Entry entry(Entry::Type::SemanticError);
        entry.semanticError = e;
        m_entries.push_back(entry);
    }
    catch (const a2::a2_exprtk::SymbolError &e)
    {
        Entry entry(Entry::Type::SymbolError);
        entry.symbolError = e;
        m_entries.push_back(entry);
    }
    catch (const std::runtime_error &e)
    {
        Entry entry(Entry::Type::RuntimeError);
        entry.runtimeError = e;
        m_entries.push_back(entry);
    }
    catch (...)
    {
        InvalidCodePath;

        Entry entry(Entry::Type::RuntimeError);
        m_entries.push_back(entry);
    }
}

void ExpressionErrorWidget::populateTable()
{
    m_errorTable->clearContents();
    m_errorTable->setRowCount(m_entries.size());

    s32 row = 0;

    for (const auto &entry: m_entries)
    {
        s32 col = 0;

        switch (entry.type)
        {
#define set_next_item(text)\
            do\
            {\
                auto item = std::make_unique<QTableWidgetItem>(text);\
                item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);\
                m_errorTable->setItem(row, col++, item.release());\
            } while (0);

            case Entry::Type::ParserError:
                {
#if 0
                    auto verboseErrorText = QString::fromStdString(entry.parserError.diagnostic)
                        + ", src_loc=" + QString::fromStdString(entry.parserError.src_location)
                        + ", err_line=" + QString::fromStdString(entry.parserError.error_line);
#endif

                    set_next_item(QString::fromStdString(entry.parserError.mode));
                    set_next_item(QString::fromStdString(entry.parserError.diagnostic));
                    //set_next_item(verboseErrorText);
                    set_next_item(QString::number(entry.parserError.line + 1));
                    set_next_item(QString::number(entry.parserError.column + 1));
                } break;

            case Entry::Type::SymbolError:
                {

                    QString msg;

                    using Reason = a2::a2_exprtk::SymbolError::Reason;

                    switch (entry.symbolError.reason)
                    {

                        case Reason::IsReservedSymbol:
                            msg = (QSL("Could not register symbol '%1': reserved symbol name.")
                                   .arg(entry.symbolError.symbolName.c_str()));
                            break;

                        case Reason::SymbolExists:
                            msg = (QSL("Could not register symbol '%1': symbol name exists.")
                                   .arg(entry.symbolError.symbolName.c_str()));
                            break;

                        case Reason::IsZeroLengthArray:
                            msg = (QSL("Could not register symbol '%1': cannot register zero length arrays.")
                                   .arg(entry.symbolError.symbolName.c_str()));
                            break;

                        case Reason::Unspecified:
                            msg = (QSL("Could not register symbol '%1': invalid symbol name.")
                                   .arg(entry.symbolError.symbolName.c_str()));
                            break;
                    }

                    set_next_item(QSL("SymbolError"));
                    set_next_item(msg);
                    set_next_item(QSL(""));
                    set_next_item(QSL(""));

                } break;

            case Entry::Type::SemanticError:
                {
                    set_next_item(QSL("SemanticError"));
                    set_next_item(QString::fromStdString(entry.semanticError.message));
                    set_next_item(QSL(""));
                    set_next_item(QSL(""));
                } break;

            case Entry::Type::RuntimeError:
                {
                    set_next_item(QSL("Unknown Error"));
                    set_next_item(entry.runtimeError.what());
                    set_next_item(QSL(""));
                    set_next_item(QSL(""));
                } break;
#undef set_next_item
        }

        row++;
    }

    m_errorTable->resizeColumnsToContents();
    m_errorTable->resizeRowsToContents();
}

void ExpressionErrorWidget::clear()
{
    assertConsistency();

    m_entries.clear();
    m_errorTable->clearContents();
    m_errorTable->setRowCount(0);

    assertConsistency();
}

void ExpressionErrorWidget::onCellClicked(int row, int column)
{
    (void) column;

    assertConsistency();
    assert(row < m_entries.size());
    assert(row < m_errorTable->rowCount());

    const auto &entry = m_entries[row];

    if (entry.type == Entry::Type::ParserError)
    {
        emit parserErrorClicked(
            static_cast<int>(entry.parserError.line + 1),
            static_cast<int>(entry.parserError.column + 1));
    }
}

void ExpressionErrorWidget::onCellDoubleClicked(int row, int column)
{
    (void) column;

    assertConsistency();
    assert(row < m_entries.size());
    assert(row < m_errorTable->rowCount());

    const auto &entry = m_entries[row];

    if (entry.type == Entry::Type::ParserError)
    {
        emit parserErrorDoubleClicked(
            static_cast<int>(entry.parserError.line + 1),
            static_cast<int>(entry.parserError.column + 1));
    }
}

void ExpressionErrorWidget::assertConsistency()
{
    assert(m_entries.size() == m_errorTable->rowCount());
}

//
// ExpressionCodeEditor
//
static const int TabStop = 2;

ExpressionCodeEditor::ExpressionCodeEditor(QWidget *parent)
    : QWidget(parent)
    , m_codeEditor(new CodeEditor)
{
    m_codeEditor->setTabStopCharCount(TabStop);
    new ExpressionOperatorSyntaxHighlighter(m_codeEditor->document());

    auto widgetLayout = new QHBoxLayout(this);
    widgetLayout->addWidget(m_codeEditor);
    widgetLayout->setContentsMargins(0, 0, 0, 0);

    connect(m_codeEditor, &QPlainTextEdit::modificationChanged,
            this, &ExpressionCodeEditor::modificationChanged);
}

void ExpressionCodeEditor::setExpressionText(const QString &text)
{
    if (text != m_codeEditor->toPlainText())
    {
        m_codeEditor->setPlainText(text);
    }
}

QString ExpressionCodeEditor::expressionText() const
{
    return m_codeEditor->toPlainText();
}

void ExpressionCodeEditor::highlightError(int row, int col)
{
    //qDebug() << __PRETTY_FUNCTION__ << row << col;

    auto cursor = m_codeEditor->textCursor();

    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, row - 1);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, col - 1);

    QColor lineColor = QColor(Qt::red).lighter(160);

    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(lineColor);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = cursor;
    selection.cursor.clearSelection();

    QList<QTextEdit::ExtraSelection> extraSelections = { selection };
    m_codeEditor->setExtraSelections(extraSelections);
}

void ExpressionCodeEditor::jumpToError(int row, int col)
{
    //qDebug() << __PRETTY_FUNCTION__ << row << col;

    auto cursor = m_codeEditor->textCursor();

    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, row - 1);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, col - 1);

    m_codeEditor->setTextCursor(cursor);
    m_codeEditor->setFocus();
}

void ExpressionCodeEditor::clearErrorHighlight()
{
    //qDebug() << __PRETTY_FUNCTION__;

    QList<QTextEdit::ExtraSelection> extraSelections;
    m_codeEditor->setExtraSelections(extraSelections);
}

//
// ExpressionEditorWidget
//
ExpressionEditorWidget::ExpressionEditorWidget(QWidget *parent)
    : QWidget(parent)
    , m_exprCodeEditor(new ExpressionCodeEditor)
    , m_exprErrorWidget(new ExpressionErrorWidget)
{
    auto splitter = new QSplitter(Qt::Vertical);
    //splitter->setHandleWidth(0);
    splitter->addWidget(m_exprCodeEditor);
    splitter->addWidget(m_exprErrorWidget);
    splitter->setStretchFactor(0, 80);
    splitter->setStretchFactor(1, 20);

    auto widgetLayout = new QHBoxLayout(this);;
    widgetLayout->setContentsMargins(0, 0, 0, 0);
    widgetLayout->addWidget(splitter);

    connect(m_exprErrorWidget, &ExpressionErrorWidget::parserErrorClicked,
            m_exprCodeEditor, &ExpressionCodeEditor::highlightError);

    connect(m_exprErrorWidget, &ExpressionErrorWidget::parserErrorDoubleClicked,
            m_exprCodeEditor, &ExpressionCodeEditor::jumpToError);

    connect(m_exprCodeEditor, &ExpressionCodeEditor::modificationChanged,
            this, &ExpressionEditorWidget::modificationChanged);
}

void ExpressionEditorWidget::setExpressionText(const QString &text)
{
    m_exprCodeEditor->setExpressionText(text);
}

QString ExpressionEditorWidget::expressionText() const
{
    return m_exprCodeEditor->expressionText();
}

void ExpressionEditorWidget::setError(const std::exception_ptr &ep)
{
    m_exprErrorWidget->setError(ep);
}

void ExpressionEditorWidget::clearError()
{
    m_exprErrorWidget->clear();
    m_exprCodeEditor->clearErrorHighlight();
}

//
// ExpressionOperatorEditorComponent
//
ExpressionOperatorEditorComponent::ExpressionOperatorEditorComponent(QWidget *parent)
    : QWidget(parent)
    , m_inputPipesView(new ExpressionOperatorPipesComboView)
    , m_outputPipesView(new ExpressionOperatorPipesComboView)
    , m_toolBar(make_toolbar())
    , m_editorWidget(new ExpressionEditorWidget)
    , m_hSplitter(new QSplitter(Qt::Horizontal))
{
    auto editorFrame = new QFrame(this);
    auto editorFrameLayout = new QVBoxLayout(editorFrame);
    editorFrameLayout->addWidget(m_toolBar);
    editorFrameLayout->addWidget(m_editorWidget);

    auto splitter = m_hSplitter;
    splitter->addWidget(m_inputPipesView);
    splitter->addWidget(editorFrame);
    splitter->addWidget(m_outputPipesView);
    splitter->setStretchFactor(0, 25);
    splitter->setStretchFactor(1, 50);
    splitter->setStretchFactor(2, 25);
    splitter->setChildrenCollapsible(false);

    auto widgetLayout = new QHBoxLayout(this);
    widgetLayout->addWidget(splitter);

    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

#define tb_aa(arg, ...) m_toolBar->addAction(arg, ##__VA_ARGS__)
#define tb_sep() m_toolBar->addSeparator()
    QAction *action;

    action = tb_aa(QIcon(":/hammer.png"), QSL("&Compile"),
                   this, &ExpressionOperatorEditorComponent::compile);
    action->setToolTip(QSL("Recompile the current expression"));

    m_actionStep = tb_aa(QIcon(":/gear--arrow.png"), QSL("S&tep"),
                   this, &ExpressionOperatorEditorComponent::step);
    m_actionStep->setToolTip(
        QSL("Perform one step of the operator.<br/>"
            "Recompilation will only be done if any of the scripts was modified."));
    m_actionStep->setEnabled(false);


    tb_sep();

    action = tb_aa(QIcon(":/binocular--arrow.png"), QSL("&Sample Inputs"),
                   this, &ExpressionOperatorEditorComponent::sampleInputs);
    action->setToolTip(
        QSL("Samples the current values of the inputs from a running/paused analysis run."));

    action = tb_aa(QIcon(":/arrow-switch.png"), QSL("&Randomize Inputs"),
                   this, &ExpressionOperatorEditorComponent::randomizeInputs);
    action->setToolTip(
        QSL("Randomizes the parameter values of each input array."));

    tb_sep();

    CodeEditor *codeEditor = m_editorWidget->getTextEditor()->codeEditor();

    QAction* actionUndo = tb_aa(QIcon::fromTheme("edit-undo"), "Undo",
                                codeEditor, &QPlainTextEdit::undo);
    actionUndo->setEnabled(false);

    QAction *actionRedo = tb_aa(QIcon::fromTheme("edit-redo"), "Redo",
                                codeEditor, &QPlainTextEdit::redo);
    actionRedo->setEnabled(false);

    connect(codeEditor, &QPlainTextEdit::undoAvailable, actionUndo, &QAction::setEnabled);
    connect(codeEditor, &QPlainTextEdit::redoAvailable, actionRedo, &QAction::setEnabled);
    connect(m_editorWidget, &ExpressionEditorWidget::modificationChanged,
            this, &ExpressionOperatorEditorComponent::expressionModificationChanged);

    tb_sep();

    tb_aa(QIcon(":/help.png"), "&Help", this, &ExpressionOperatorEditorComponent::onActionHelp_triggered);
    //tb_aa("Show Symbol Table");
    //tb_aa("Generate Code", this, &ExpressionOperatorEditorComponent::generateDefaultCode);

#undef tb_sep
#undef tb_aa

    m_inputPipesView->setPipeDataEditable(true);
}

void ExpressionOperatorEditorComponent::setHSplitterSizes()
{
    int totalWidth = m_hSplitter->width();
    QList<int> sizes = { 0, 0, 0 };

    sizes[0] = m_inputPipesView->sizeHint().width();
    totalWidth -= sizes[0];

    //qDebug() << __PRETTY_FUNCTION__ << "width of input pipes view from sizehint:" << sizes[0];

    sizes[2] = m_outputPipesView->sizeHint().width();
    totalWidth -= sizes[2];

    static const s32 EditorMinWidth = 800;

    sizes[1] = std::max(totalWidth, EditorMinWidth);

    //qDebug() << __PRETTY_FUNCTION__ << "width of the editor area:" << sizes[1];

    m_hSplitter->setSizes(sizes);
}

void ExpressionOperatorEditorComponent::showEvent(QShowEvent *event)
{
    setHSplitterSizes();

    QWidget::showEvent(event);
}

void ExpressionOperatorEditorComponent::resizeEvent(QResizeEvent *event)
{
    setHSplitterSizes();

    QWidget::resizeEvent(event);
}

void ExpressionOperatorEditorComponent::setExpressionText(const QString &text)
{
    m_editorWidget->setExpressionText(text);
}

QString ExpressionOperatorEditorComponent::expressionText() const
{
    return m_editorWidget->expressionText();
}

void ExpressionOperatorEditorComponent::setInputs(const std::vector<a2::PipeVectors> &pipes,
                                                  const QStringList &titles,
                                                  const QStringList &units)
{
    m_inputPipesView->setPipes(pipes, titles, units);
    setHSplitterSizes();
}

void ExpressionOperatorEditorComponent::setOutputs(const std::vector<a2::PipeVectors> &pipes,
                                                  const QStringList &titles,
                                                  const QStringList &units)
{
    m_outputPipesView->setPipes(pipes, titles, units);
    setHSplitterSizes();
}

void ExpressionOperatorEditorComponent::setEvaluationError(const std::exception_ptr &ep)
{
    m_editorWidget->setError(ep);
}

void ExpressionOperatorEditorComponent::clearEvaluationError()
{
    m_editorWidget->clearError();
}

void ExpressionOperatorEditorComponent::setExpressionModified(bool modified)
{
    m_editorWidget->getTextEditor()->codeEditor()->document()->setModified(modified);
}

bool ExpressionOperatorEditorComponent::isExpressionModified() const
{
    return m_editorWidget->getTextEditor()->codeEditor()->document()->isModified();
}

namespace
{
    static const char *HelpWidgetObjectName = "ExpressionOperatorHelp";
    static const char *HelpWidgetStyleSheet =
        "QTextBrowser {"
        "   background: #e1e1e1;"
        "}"
        ;

    QWidget *make_help_widget()
    {
        auto result = new QTextBrowser;
        result->setStyleSheet(HelpWidgetStyleSheet);
        result->setAttribute(Qt::WA_DeleteOnClose);
        result->setObjectName(HelpWidgetObjectName);
        result->setWindowTitle("Expression Operator Help");
        result->setOpenExternalLinks(true);
        result->resize(800, 800);
        add_widget_close_action(result);

        auto geoSaver = new WidgetGeometrySaver(result);
        geoSaver->addAndRestore(result, QSL("WindowGeometries/") + result->objectName());

        {
            QFile inFile(":/analysis/expr_data/expr_help.html");

            if (inFile.open(QIODevice::ReadOnly))
            {
                QTextStream inStream(&inFile);
                result->document()->setHtml(inStream.readAll());
            }
            else
            {
                InvalidCodePath;
                result->document()->setHtml(QSL("<b>Error: help file not found!</b>"));
            }

            // scroll to top
            auto cursor = result->textCursor();
            cursor.setPosition(0);
            result->setTextCursor(cursor);
        }

        return result;
    }
} // end anon namespace

void ExpressionOperatorEditorComponent::onActionHelp_triggered()
{
    auto widgets = QApplication::topLevelWidgets();

    auto it = std::find_if(widgets.begin(), widgets.end(), [](const QWidget *widget) {
        return widget->objectName() == HelpWidgetObjectName;
    });

    if (it != widgets.end())
    {
        auto widget = *it;
        show_and_activate(widget);
    }
    else
    {
        auto widget = make_help_widget();
        widget->show();
    }
}

//
// ExpressionOperatorDialog and Private implementation
//

/* Notes about the Model structure:
 *
 * This is used to hold the current state of the ExpressionOperator UI. The GUI
 * can be populated using this information and both the a1 and a2 versions of
 * the ExpressionOperator can be created from the information in the model.
 *
 * User interactions with the gui will trigger code that updates the model.
 *
 * When the user wants to evaluate one of the expressions
 * a2::make_expression_operator() is used to create the operator. Errors can be
 * displayed in the respective ExpressionOperatorEditorComponent.
 *
 * This code is not fast. Memory allocations from std::vector happen
 * frequently!
 *
 * Remember: the operator can not be built if any of the inputs is unconnected.
 * The resulting data will be a vector with length 0 which can not be
 * registered in an exprtk symbol table.
 */

class A2PipeStorage
{
    public:
        A2PipeStorage() {}

        explicit A2PipeStorage(Pipe *pipe)
        {
            s32 size = pipe->getSize();

            data.resize(size);
            lowerLimits.resize(size);
            upperLimits.resize(size);

            for (s32 pi = 0; pi < size; pi++)
            {
                const auto &a1_param = pipe->getParameter(pi);

                data[pi]        = a1_param->value;
                lowerLimits[pi] = a1_param->lowerLimit;
                upperLimits[pi] = a1_param->upperLimit;
            }
        }

        A2PipeStorage(A2PipeStorage &&) = default;
        A2PipeStorage &operator=(A2PipeStorage &&) = default;

        A2PipeStorage(const A2PipeStorage &) = delete;
        A2PipeStorage &operator=(const A2PipeStorage &) = delete;

        a2::PipeVectors make_pipe_vectors()
        {
            s32 size = static_cast<s32>(data.size());

            return a2::PipeVectors
            {
                { data.data(), size },
                { lowerLimits.data(), size },
                { upperLimits.data(), size }
            };
        }

        void assert_consistency(const a2::PipeVectors &a2_pipe) const
        {
            //qDebug() << __PRETTY_FUNCTION__ << a2_pipe.data.size;

            assert(a2_pipe.data.size == a2_pipe.lowerLimits.size);
            assert(a2_pipe.data.size == a2_pipe.upperLimits.size);

            const s32 expected_size = static_cast<s32>(data.size());

            assert(a2_pipe.data.size == expected_size);
            assert(a2_pipe.lowerLimits.size == expected_size);
            assert(a2_pipe.upperLimits.size == expected_size);

            assert(a2_pipe.data.data == data.data());
            assert(a2_pipe.lowerLimits.data == lowerLimits.data());
            assert(a2_pipe.upperLimits.data == upperLimits.data());
        }

    private:
        std::vector<double> data;
        std::vector<double> lowerLimits;
        std::vector<double> upperLimits;

};

namespace detail
{

struct Model
{
    /* A clone of the original operator that's being edited. This is here so that we have
     * proper Slot pointers to pass to EventWidget::selectInputFor() on input selection.
     *
     * Note: using opClone for input selection has the side effect that input selection
     * sees the source operator as being different than the operator begin edited (m_op).
     * That's why self connections where possible until the additionalInvalidSources
     * argument was added to EventWidget::selectInputFor().
     *
     * Note that pipe -> slot connections are not made on this clone as the source pipes
     * would be modified by that operation. Instead the selected input pipes and indexes
     * are stored in the model aswell. Once the user accepts the changes the original
     * operator will be modified according to the data stored in the model. */
    std::unique_ptr<ExpressionOperator> opClone;

    std::vector<a2::PipeVectors> inputs;
    std::vector<A2PipeStorage> inputStorage;
    std::vector<s32> inputIndexes;
    std::vector<std::string> inputPrefixes;
    std::vector<std::string> inputUnits;
    std::string beginExpression;
    std::string stepExpression;

    /* Pointers to the original input pipes are stored here so that the
     * analysis::ExpressionOperator can be modified properly once the user
     * accepts the changes.
     * Also used for sampling input data from a running analysis. */
    std::vector<Pipe *> a1_inputPipes;

    QString operatorName;

    Model() = default;

    Model(const Model &) = delete;
    Model &operator=(const Model &) = delete;

    Model(Model &&) = default;
    Model &operator=(Model &&) = default;
};

void assert_internal_consistency(const Model &model)
{
    assert(model.inputs.size() == model.inputStorage.size());
    assert(model.inputs.size() == model.inputIndexes.size());
    assert(model.inputs.size() == model.inputPrefixes.size());
    assert(model.inputs.size() == model.inputUnits.size());
    assert(model.inputs.size() == model.a1_inputPipes.size());

    for (size_t ii = 0; ii < model.inputs.size(); ii++)
    {
        model.inputStorage[ii].assert_consistency(model.inputs[ii]);
    }
}

void assert_consistency(const Model &model)
{
    assert(model.opClone);
    assert(model.opClone->getNumberOfSlots() > 0);
    assert(static_cast<size_t>(model.opClone->getNumberOfSlots()) == model.inputs.size());

    assert_internal_consistency(model);
}

} // end namespace detail

using detail::Model;

struct ExpressionOperatorDialog::Private
{
    static const size_t WorkArenaSegmentSize = Kilobytes(4);
    static const int TabIndex_Begin = 1;
    static const int TabIndex_Step  = 2;

    explicit Private(ExpressionOperatorDialog *q)
        : m_q(q)
        , m_arena(WorkArenaSegmentSize)
    {}

    ExpressionOperatorDialog *m_q;

    // The operator being added or edited
    std::shared_ptr<ExpressionOperator> m_op;
    // The userlevel the operator is placed in
    int m_userLevel;
    // new or edit
    ObjectEditorMode m_mode;
    // destination directory for new operators. may be null
    DirectoryPtr m_destDir;
    // backpointer to the eventwidget used for input selection
    EventWidget *m_eventWidget;
    // data transfer to/from gui and storage of inputs
    std::unique_ptr<Model> m_model;
    // work arena for a2 operator creation
    memory::Arena m_arena;
    // the a2 operator recreated when the user wants to evaluate one of the
    // scripts
    a2::Operator m_a2Op;
    bool m_lastStepCompileSucceeded = false;


    QTabWidget *m_tabWidget;

    // tab0: operator name and input select
    QComboBox *combo_eventSelect;
    QLineEdit *le_operatorName;
    SlotGrid *m_slotGrid;

    // tab1: begin expression
    ExpressionOperatorEditorComponent *m_beginExpressionEditor;

    // tab2: step expression
    ExpressionOperatorEditorComponent *m_stepExpressionEditor;

    QDialogButtonBox *m_buttonBox;

    void updateModelFromOperator(); // op  -> model
    void updateModelFromGUI();      // gui -> model

    void repopulateGUIFromModel();
    void repopulateSlotGridFromModel();
    void refreshInputPipesViews();
    void refreshOutputPipesViews();

    void postInputsModified();

    void onAddSlotButtonClicked();
    void onRemoveSlotButtonClicked();
    void onInputSelected(Slot *destSlot, s32 slotIndex,
                         Pipe *sourcePipe, s32 sourceParamIndex);
    void onInputCleared(s32 slotIndex);
    void onInputPrefixEdited(s32 slotIndex, const QString &text);

    void model_compileBeginExpression();
    void model_compileStepExpression();
    void model_stepOperator();
    void model_sampleInputs();
    void model_randomizeInputs();

    QString generateNameForNewOperator();
};

namespace
{

QStringList qStringList_from_vector(const std::vector<std::string> &strings)
{
    QStringList result;
    result.reserve(strings.size());

    for (const auto &str: strings)
    {
        result.push_back(QString::fromStdString(str));
    }

    return result;
}

/* IMPORTANT: This will potentially leave the model in an inconsistent state as
 * no slot will be added to model.opClone! */
void add_model_only_input(Model &model)
{
    model.inputs.push_back({});
    model.inputStorage.push_back({});
    model.inputIndexes.push_back(a2::NoParamIndex);
    model.inputPrefixes.push_back({});
    model.inputUnits.push_back({});
    model.a1_inputPipes.push_back(nullptr);
}

void add_new_input_slot(Model &model)
{
    assert_consistency(model);

    s32 si = model.opClone->getNumberOfSlots();
    // this generates a new input prefix if needed
    model.opClone->addSlot();
    add_model_only_input(model);
    model.inputPrefixes[si] = model.opClone->getInputPrefix(si).toStdString();

    assert_consistency(model);
}

void pop_input_slot(Model &model)
{
    assert_consistency(model);

    if (model.opClone->removeLastSlot())
    {
        model.inputs.pop_back();
        model.inputStorage.pop_back();
        model.inputIndexes.pop_back();
        model.inputPrefixes.pop_back();
        model.inputUnits.pop_back();
        model.a1_inputPipes.pop_back();
    }

    assert_consistency(model);
}

void connect_input(Model &model, s32 inputIndex, Pipe *inPipe, s32 paramIndex)
{
    assert(0 <= inputIndex && inputIndex < model.opClone->getNumberOfSlots());
    assert(inPipe);

    A2PipeStorage storage(inPipe);

    model.inputs[inputIndex] = storage.make_pipe_vectors();
    model.inputStorage[inputIndex] = std::move(storage);
    model.inputIndexes[inputIndex] = paramIndex;
    // Note: input prefix is not touched here
    //model.inputPrefixes[inputIndex] = model.opClone->getInputPrefix(inputIndex).toStdString();
    model.inputUnits[inputIndex] = inPipe->getParameters().unit.toStdString();
    model.a1_inputPipes[inputIndex] = inPipe;
}

void disconnect_input(Model &model, s32 inputIndex)
{
    assert_consistency(model);

    assert(0 <= inputIndex && inputIndex < model.opClone->getNumberOfSlots());

    model.inputs[inputIndex] = {};
    model.inputStorage[inputIndex] = {};
    model.inputIndexes[inputIndex] = a2::NoParamIndex;
    // Note: Keeps input prefix intact.
    model.inputUnits[inputIndex] = {};
    model.a1_inputPipes[inputIndex] = nullptr;

    assert_consistency(model);
}

void load_from_operator(Model &model, ExpressionOperator &op)
{
    model.inputs.clear();
    model.inputStorage.clear();
    model.inputIndexes.clear();
    model.inputPrefixes.clear();
    model.inputUnits.clear();
    model.a1_inputPipes.clear();

    model.opClone = std::unique_ptr<ExpressionOperator>(op.cloneViaSerialization());

    assert(op.getNumberOfSlots() == model.opClone->getNumberOfSlots());

    model.operatorName = op.objectName();

    for (s32 si = 0; si < op.getNumberOfSlots(); si++)
    {
        Slot *slot = op.getSlot(si);

        add_model_only_input(model);
        model.inputPrefixes[si] = model.opClone->getInputPrefix(si).toStdString();

        if (slot && slot->isConnected())
        {
            connect_input(model, si, slot->inputPipe, slot->paramIndex);
        }
    }

    model.beginExpression = op.getBeginExpression().toStdString();
    model.stepExpression  = op.getStepExpression().toStdString();

    assert_consistency(model);
}

void save_to_operator(const Model &model, ExpressionOperator &op)
{
    assert_consistency(model);

    while (op.removeLastSlot()) {};

    const s32 slotCount = static_cast<s32>(model.inputs.size());

    while (op.getNumberOfSlots() < slotCount)
    {
        op.addSlot();
    }

    for (s32 slotIndex = 0; slotIndex < slotCount; slotIndex++)
    {
        // Note: it's ok to pass a nullptr Pipe here. It will leave the slot in
        // disconnected state.
        op.connectInputSlot(slotIndex,
                            model.a1_inputPipes[slotIndex],
                            model.inputIndexes[slotIndex]);
    }

    op.setBeginExpression(QString::fromStdString(model.beginExpression));
    op.setStepExpression(QString::fromStdString(model.stepExpression));
    op.setInputPrefixes(qStringList_from_vector(model.inputPrefixes));
    op.setObjectName(model.operatorName);

    // Note: opClone should not need to be udpated: the only parts used are its
    // slots and those should have been updated in add_new_input_slot() and
    // pop_input_slot().
    assert(model.opClone->getNumberOfSlots() == op.getNumberOfSlots());
}

} // end anon namespace

//
// SlotGrid
//
SlotGrid::SlotGrid(QWidget *parent)
    : QFrame(parent)
{
    auto slotFrame  = new QFrame;
    m_slotLayout = new QGridLayout(slotFrame);
    m_slotLayout->setContentsMargins(2, 2, 2, 2);
    m_slotLayout->setColumnStretch(0, 0); // index
    m_slotLayout->setColumnStretch(1, 1); // select button with input name
    m_slotLayout->setColumnStretch(2, 1); // variable name inside the script
    m_slotLayout->setColumnStretch(3, 0); // clear selection button

    m_addSlotButton = new QPushButton(QIcon(QSL(":/list_add.png")), QString());
    m_addSlotButton->setToolTip(QSL("Add input"));

    m_removeSlotButton = new QPushButton(QIcon(QSL(":/list_remove.png")), QString());
    m_removeSlotButton->setToolTip(QSL("Remove last input"));

    // a "row" below the slotFrame (which contains the grid layout) for the add/remove
    // slot buttons
    auto addRemoveSlotButtonsLayout = new QHBoxLayout;
    addRemoveSlotButtonsLayout->setContentsMargins(2, 2, 2, 2);
    addRemoveSlotButtonsLayout->addStretch();
    addRemoveSlotButtonsLayout->addWidget(m_addSlotButton);
    addRemoveSlotButtonsLayout->addWidget(m_removeSlotButton);

    auto outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(slotFrame);
    outerLayout->addLayout(addRemoveSlotButtonsLayout);
    outerLayout->setStretch(0, 1);


    connect(m_addSlotButton, &QPushButton::clicked, this, &SlotGrid::addSlot);
    connect(m_removeSlotButton, &QPushButton::clicked, this, &SlotGrid::removeSlot);
}

void SlotGrid::repopulate(const detail::Model &model, EventWidget *eventWidget, s32 /*userLevel*/)
{
    assert_consistency(model);

    // Clear the slot grid and the select buttons
    while (QLayoutItem *child = m_slotLayout->takeAt(0))
    {
        if (auto widget = child->widget())
        {
            widget->deleteLater();
        }

        delete child;
    }

    Q_ASSERT(m_slotLayout->count() == 0);

    // These have been deleted by the layout clearing code above.
    selectButtons.clear();
    clearButtons.clear();
    inputPrefixLineEdits.clear();

    // Repopulate

    const auto &op = model.opClone;
    assert(op);

    const s32 slotCount = op->getNumberOfSlots();
    assert(slotCount > 0);

    s32 row = 0;
    s32 col = 0;

    m_slotLayout->addWidget(new QLabel(QSL("Input#")), row, col++);
    m_slotLayout->addWidget(new QLabel(QSL("Select")), row, col++);
    m_slotLayout->addWidget(new QLabel(QSL("Clear")), row, col++);
    m_slotLayout->addWidget(new QLabel(QSL("Variable Name")), row, col++);

    row++;

    for (s32 slotIndex = 0; slotIndex < slotCount; slotIndex++)
    {
        Slot *slot = op->getSlot(slotIndex);

        auto selectButton = new InputSelectButton(slot, eventWidget);
        selectButtons.push_back(selectButton);

        auto clearButton = new QPushButton(QIcon(":/dialog-close.png"), QString());
        clearButtons.push_back(clearButton);

        auto le_inputPrefix  = new QLineEdit;
        inputPrefixLineEdits.push_back(le_inputPrefix);

        if (model.a1_inputPipes[slotIndex])
        {
            auto inputPipe   = model.a1_inputPipes[slotIndex];
            s32 paramIndex   = model.inputIndexes[slotIndex];

            selectButton->setText(make_input_source_text(inputPipe, paramIndex));
        }

        le_inputPrefix->setText(QString::fromStdString(model.inputPrefixes[slotIndex]));

        QObject::connect(selectButton, &QPushButton::toggled,
                         this, [this, eventWidget, slotIndex] (bool checked) {

            // Cancel any previous input selection. Has no effect if no input
            // selection was active.
            eventWidget->endSelectInput();

            if (checked)
            {
                // Uncheck the other buttons.
                for (s32 bi = 0; bi < selectButtons.size(); bi++)
                {
                    if (bi != slotIndex)
                        selectButtons[bi]->setChecked(false);
                }

                emit beginInputSelect(slotIndex);
            }
        });

        QObject::connect(clearButton, &QPushButton::clicked,
                         this, [this, selectButton, eventWidget, slotIndex]() {

            selectButton->setText(QSL("<select>"));
            selectButton->setChecked(false);
            endInputSelect();
            eventWidget->endSelectInput();
            emit clearInput(slotIndex);
        });

        connect(le_inputPrefix, &QLineEdit::editingFinished,
                this, [this, &model, slotIndex, le_inputPrefix]() {

            assert(static_cast<size_t>(slotIndex) < model.inputPrefixes.size());

            if (le_inputPrefix->text().toStdString() != model.inputPrefixes[slotIndex])
            {
                emit inputPrefixEdited(slotIndex, le_inputPrefix->text());
            }
        });

        col = 0;

        m_slotLayout->addWidget(new QLabel(slot->name), row, col++);
        m_slotLayout->addWidget(selectButton, row, col++);
        m_slotLayout->addWidget(clearButton, row, col++);
        m_slotLayout->addWidget(le_inputPrefix, row, col++);

        row++;
    }

    m_slotLayout->setRowStretch(row, 1);

    m_slotLayout->setColumnStretch(0, 0);
    m_slotLayout->setColumnStretch(1, 1);
    m_slotLayout->setColumnStretch(2, 0);
    m_slotLayout->setColumnStretch(3, 1);

    m_removeSlotButton->setEnabled(slotCount > 1);

    assert(selectButtons.size() == static_cast<s32>(model.inputs.size()));
    assert(selectButtons.size() == clearButtons.size());
    assert(selectButtons.size() == inputPrefixLineEdits.size());
}

void SlotGrid::endInputSelect()
{
    for (auto &selectButton: selectButtons)
    {
        selectButton->setChecked(false);
    }
}

void ExpressionOperatorDialog::Private::repopulateSlotGridFromModel()
{
    m_slotGrid->repopulate(*m_model, m_eventWidget, m_userLevel);
}

void ExpressionOperatorDialog::Private::updateModelFromOperator()
{
    load_from_operator(*m_model, *m_op);

    model_compileBeginExpression();
    model_compileStepExpression();
}

void ExpressionOperatorDialog::Private::updateModelFromGUI()
{
    m_model->operatorName = le_operatorName->text();

    assert(m_model->inputPrefixes.size()
           == static_cast<size_t>(m_slotGrid->inputPrefixLineEdits.size()));

    for (s32 i = 0; i < m_slotGrid->inputPrefixLineEdits.size(); i++)
    {
        m_model->inputPrefixes[i] = m_slotGrid->inputPrefixLineEdits[i]->text().toStdString();
    }

    m_model->beginExpression = m_beginExpressionEditor->expressionText().toStdString();
    m_model->stepExpression  = m_stepExpressionEditor->expressionText().toStdString();

    m_beginExpressionEditor->setExpressionModified(false);
    m_stepExpressionEditor->setExpressionModified(false);
}

void ExpressionOperatorDialog::Private::repopulateGUIFromModel()
{
    le_operatorName->setText(m_model->operatorName);

    repopulateSlotGridFromModel();

    // expression text
    m_beginExpressionEditor->setExpressionText(
        QString::fromStdString(m_model->beginExpression));

    m_stepExpressionEditor->setExpressionText(
        QString::fromStdString(m_model->stepExpression));

    // input pipes and variable names
    m_beginExpressionEditor->setInputs(m_model->inputs,
                                       qStringList_from_vector(m_model->inputPrefixes),
                                       qStringList_from_vector(m_model->inputUnits)
                                       );

    m_stepExpressionEditor->setInputs(m_model->inputs,
                                       qStringList_from_vector(m_model->inputPrefixes),
                                       qStringList_from_vector(m_model->inputUnits)
                                     );

    // output pipes from the a2 operator
    std::vector<a2::PipeVectors> outputs;
    QStringList outputNames;
    QStringList outputUnits;

    if (m_a2Op.type == a2::Operator_Expression)
    {
        auto a2OpData = reinterpret_cast<a2::ExpressionOperatorData *>(m_a2Op.d);

        assert(m_a2Op.outputCount == a2OpData->output_units.size());

        for (u32 outIdx = 0; outIdx < m_a2Op.outputCount; outIdx++)
        {
            a2::PipeVectors pipe
            {
                { m_a2Op.outputs[outIdx].data, m_a2Op.outputs[outIdx].size },
                { m_a2Op.outputLowerLimits[outIdx].data, m_a2Op.outputLowerLimits[outIdx].size },
                { m_a2Op.outputUpperLimits[outIdx].data, m_a2Op.outputUpperLimits[outIdx].size }
            };

            outputs.push_back(pipe);
            outputNames.push_back(QString::fromStdString(a2OpData->output_names[outIdx]));
            outputUnits.push_back(QString::fromStdString(a2OpData->output_units[outIdx]));
        }
    }

    m_beginExpressionEditor->setOutputs(outputs, outputNames, outputUnits);
    m_stepExpressionEditor->setOutputs(outputs, outputNames, outputUnits);

    bool enableStepAction = (m_a2Op.type == a2::Operator_Expression && m_lastStepCompileSucceeded);
    m_stepExpressionEditor->getActionStep()->setEnabled(enableStepAction);
}

void ExpressionOperatorDialog::Private::postInputsModified()
{
    updateModelFromGUI();
    model_compileBeginExpression();
    model_compileStepExpression();
}

void ExpressionOperatorDialog::Private::refreshInputPipesViews()
{
    m_beginExpressionEditor->getInputPipesView()->refresh();
    m_stepExpressionEditor->getInputPipesView()->refresh();
}

void ExpressionOperatorDialog::Private::refreshOutputPipesViews()
{
    m_beginExpressionEditor->getOutputPipesView()->refresh();
    m_stepExpressionEditor->getOutputPipesView()->refresh();
}

void ExpressionOperatorDialog::Private::onAddSlotButtonClicked()
{
    m_eventWidget->endSelectInput();

    add_new_input_slot(*m_model);
    repopulateSlotGridFromModel();
    postInputsModified();
}

void ExpressionOperatorDialog::Private::onRemoveSlotButtonClicked()
{
    if (m_model->opClone->getNumberOfSlots() > 1)
    {
        m_eventWidget->endSelectInput();

        pop_input_slot(*m_model);
        repopulateSlotGridFromModel();
        postInputsModified();
    }
}

void ExpressionOperatorDialog::Private::onInputSelected(
    Slot *destSlot, s32 slotIndex,
    Pipe *sourcePipe, s32 sourceParamIndex)
{
    assert(destSlot);
    assert(sourcePipe);
    assert(sourcePipe->getSource());

    qDebug() << __PRETTY_FUNCTION__ << destSlot << slotIndex << sourcePipe << sourceParamIndex;
    connect_input(*m_model, slotIndex, sourcePipe, sourceParamIndex);

    // If no valid event has been selected yet, use the event of the newly
    // selected input pipe.
    if (combo_eventSelect->currentData().toUuid().isNull())
    {
        auto eventId = sourcePipe->getSource()->getEventId();
        int idx = combo_eventSelect->findData(eventId);
        if (idx >= 0)
            combo_eventSelect->setCurrentIndex(idx);
    }

    postInputsModified();
}

void ExpressionOperatorDialog::Private::onInputCleared(s32 slotIndex)
{
    qDebug() << __PRETTY_FUNCTION__ << slotIndex;
    disconnect_input(*m_model, slotIndex);

    postInputsModified();
}

void ExpressionOperatorDialog::Private::onInputPrefixEdited(s32 slotIndex,
                                                            const QString &text)
{
    qDebug() << __PRETTY_FUNCTION__ << slotIndex << text;
    postInputsModified();
}

void ExpressionOperatorDialog::Private::model_compileBeginExpression()
{
    /* build a2 operator using m_arena and the data from the model.
     * store the operator in a member variable.
     *
     * catch exceptions from the build process and use them to populate the
     * error table of the editor component.
     *
     * if the build succeeds use the operators output pipes and the
     * a2::ExpressionOperatorData struct to populate the output pipes view.
     */

    try
    {
        m_a2Op = a2::make_expression_operator(
            &m_arena,
            m_model->inputs,
            m_model->inputIndexes,
            m_model->inputPrefixes,
            m_model->inputUnits,
            m_model->beginExpression,
            m_model->stepExpression,
            a2::ExpressionOperatorBuildOptions::InitOnly);

        m_beginExpressionEditor->clearEvaluationError();
    }
    catch (const std::runtime_error &e)
    {
        qDebug() << __FUNCTION__ << "runtime_error:" << QString::fromStdString(e.what());

        m_a2Op = {};
        m_beginExpressionEditor->setEvaluationError(std::current_exception());
    }

    repopulateGUIFromModel();
}

void ExpressionOperatorDialog::Private::model_compileStepExpression()
{
    /* Full build split into InitOnly and an additional compilation of the step
     * expression if the init part succeeded. This is done so that errors from
     * the begin expr do not show up in the step expr editor.
     * */

    try
    {
        m_a2Op = a2::make_expression_operator(
            &m_arena,
            m_model->inputs,
            m_model->inputIndexes,
            m_model->inputPrefixes,
            m_model->inputUnits,
            m_model->beginExpression,
            m_model->stepExpression,
            a2::ExpressionOperatorBuildOptions::InitOnly);
    }
    catch (const std::runtime_error &e)
    {
        qDebug() << __FUNCTION__ << "InitOnly failed:" << QString::fromStdString(e.what());

        m_a2Op = {};
        std::runtime_error error("Evaluation of the Output Defintion expression failed.");
        m_stepExpressionEditor->setEvaluationError(std::make_exception_ptr(error));
    }

    if (m_a2Op.type == a2::Operator_Expression)
    {
        try
        {
            a2::expression_operator_compile_step_expression(&m_a2Op);
            a2::expression_operator_step(&m_a2Op);

            m_stepExpressionEditor->clearEvaluationError();
            m_lastStepCompileSucceeded = true;
        }
        catch (const std::runtime_error &e)
        {
            qDebug() << __FUNCTION__ << "runtime_error:" << QString::fromStdString(e.what());

            m_stepExpressionEditor->setEvaluationError(std::current_exception());
            m_lastStepCompileSucceeded = false;
        }
    }

    repopulateGUIFromModel();
}

void ExpressionOperatorDialog::Private::model_stepOperator()
{
    assert(m_a2Op.type == a2::Operator_Expression);

    if (m_a2Op.type == a2::Operator_Expression)
    {
        a2::expression_operator_step(&m_a2Op);
        refreshOutputPipesViews();
    }
}

QString ExpressionOperatorDialog::Private::generateNameForNewOperator()
{
    auto operators = m_eventWidget->getAnalysis()->getOperators();

    QSet<QString> names;

    for (const auto &op: operators)
    {
        names.insert(op->objectName());
    }

    static const QString pattern = QSL("expr%1");
    int suffix = 0;
    QString result;

    do
    {
        result = pattern.arg(suffix++);
    } while (names.contains(result));

    return result;
}

namespace
{

void copy_pipe_data(const a2::PipeVectors &sourcePipe, a2::PipeVectors &destPipe)
{
    const size_t count = std::min(sourcePipe.data.size, destPipe.data.size);

    for (size_t i = 0; i < count; i++)
    {
        destPipe.data[i] = sourcePipe.data[i];
    }
}

/* C++17 has an std::clamp template with roughly the same interface. */
inline double my_clamp(double v, double lo, double hi)
{
    assert(!(hi < lo));
    if (v < lo) return lo;
    if (hi < v) return hi;
    return v;
}

void randomize_pipe_data(a2::PipeVectors &pipe)
{
    /* Note: starting out with the assumption that the limits are the same for
     * each element of the pipe. When filling the element the specific limits
     * are checked and if the generated random value is out of range it will be
     * clamped to be inside the range. This may yield a value equal to the
     * upper limit which is not correct as the limit specification in a2 is
     * supposed to be exclusive. I'm ignoring this case here as it's just test
     * data being generated. */

    if (pipe.data.size == 0) return;

    double ll = pipe.lowerLimits[0];
    double ul = pipe.upperLimits[0];

    std::uniform_real_distribution<double> dist(ll, ul);
    pcg32_fast rng;

#ifndef Q_OS_WIN
    std::random_device rd;
    rng.seed(rd());
#else
    // std::random_device is bad under mingw
    rng.seed(std::time(nullptr));
#endif

    for (s32 pi = 0; pi < pipe.data.size; pi++)
    {
        pipe.data[pi] = my_clamp(dist(rng), pipe.lowerLimits[pi], pipe.upperLimits[pi]);
    }
}

} // end anon namespace

void ExpressionOperatorDialog::Private::model_sampleInputs()
{
    assert_consistency(*m_model);

    auto analysis = m_eventWidget->getAnalysis();
    auto a2State  = analysis->getA2AdapterState();

    if (!a2State) return;

    for (size_t ii = 0; ii < m_model->inputs.size(); ii++)
    {
        auto a1_pipe = m_model->a1_inputPipes[ii];

        if (!a1_pipe) continue;

        auto a2_sourcePipe = find_output_pipe(a2State, a1_pipe).first;
        auto a2_destPipe   = m_model->inputs[ii];

        copy_pipe_data(a2_sourcePipe, a2_destPipe);
    }

    refreshInputPipesViews();
}

void ExpressionOperatorDialog::Private::model_randomizeInputs()
{
    assert_consistency(*m_model);

    for (size_t ii = 0; ii < m_model->inputs.size(); ii++)
    {
        randomize_pipe_data(m_model->inputs[ii]);
    }

    refreshInputPipesViews();
}

ExpressionOperatorDialog::ExpressionOperatorDialog(const std::shared_ptr<ExpressionOperator> &op,
                                                   int userLevel,
                                                   ObjectEditorMode mode,
                                                   const DirectoryPtr &destDir,
                                                   EventWidget *eventWidget)
    : ObjectEditorDialog(eventWidget)
    , m_d(std::make_unique<Private>(this))
{
    setWindowFlags((Qt::Window
                   | Qt::CustomizeWindowHint
                   | Qt::WindowTitleHint
                   | Qt::WindowMinMaxButtonsHint
                   | Qt::WindowCloseButtonHint)
                   & ~Qt::WindowShadeButtonHint);

    m_d->m_op          = op;
    m_d->m_userLevel   = userLevel;
    m_d->m_mode        = mode;
    m_d->m_destDir     = destDir;
    m_d->m_eventWidget = eventWidget;
    m_d->m_tabWidget   = new QTabWidget;
    m_d->m_model       = std::make_unique<Model>();

    auto update_ok_button = [this] ()
    {
        bool enableOkButton = !m_d->combo_eventSelect->currentData().toUuid().isNull();
        m_d->m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(enableOkButton);
    };

    // tab0: event select, operator name and input select
    {
        m_d->combo_eventSelect = make_event_selection_combo(
            eventWidget->getVMEConfig()->getEventConfigs(), op, destDir);

        connect(m_d->combo_eventSelect, qOverload<int>(&QComboBox::currentIndexChanged),
                this, update_ok_button);


        m_d->le_operatorName = new QLineEdit;

        m_d->m_slotGrid = new SlotGrid(this);
        auto gb_slotGrid = new QGroupBox(QSL("Inputs"), this);
        auto gb_slotGridLayout = new QHBoxLayout(gb_slotGrid);
        gb_slotGridLayout->setContentsMargins(2, 2, 2, 2);
        gb_slotGridLayout->addWidget(m_d->m_slotGrid);

        auto page = new QWidget(this);
        auto l    = new QFormLayout(page);

        l->addRow(QSL("Parent Event"), m_d->combo_eventSelect);
        l->addRow(QSL("Operator Name"), m_d->le_operatorName);
        l->addRow(gb_slotGrid);

        m_d->m_tabWidget->addTab(page, QSL("&Inputs && Name"));
    }

    // tab1: begin expression
    {
        m_d->m_beginExpressionEditor = new ExpressionOperatorEditorComponent;
        m_d->m_beginExpressionEditor->getActionStep()->setVisible(false);

        auto page = new QWidget(this);
        auto l    = new QHBoxLayout(page);

        l->addWidget(m_d->m_beginExpressionEditor);

        m_d->m_tabWidget->addTab(page, QSL("&Output Definition"));
    }

    // tab2: step expression
    {
        m_d->m_stepExpressionEditor = new ExpressionOperatorEditorComponent;

        auto page = new QWidget(this);
        auto l    = new QHBoxLayout(page);

        l->addWidget(m_d->m_stepExpressionEditor);

        m_d->m_tabWidget->addTab(page, QSL("&Step Expression"));
    }

    // buttonbox: ok/cancel
    m_d->m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                            | QDialogButtonBox::Cancel
                                            | QDialogButtonBox::Apply,
                                            this);

    m_d->m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(false);

    connect(m_d->m_buttonBox, &QDialogButtonBox::accepted, this, &ExpressionOperatorDialog::accept);
    connect(m_d->m_buttonBox, &QDialogButtonBox::rejected, this, &ExpressionOperatorDialog::reject);
    connect(m_d->m_buttonBox, &QDialogButtonBox::clicked, this, [this] (QAbstractButton *button) {
        if (m_d->m_buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole)
            apply();
    });

    // main layout
    auto dialogLayout = new QVBoxLayout(this);
    dialogLayout->addWidget(m_d->m_tabWidget);
    dialogLayout->addWidget(m_d->m_buttonBox);
    //dialogLayout->setContentsMargins(2, 2, 2, 2);
    dialogLayout->setStretch(0, 1);

    // Slotgrid interactions
    connect(m_d->m_slotGrid, &SlotGrid::addSlot, this, [this]() {
        m_d->onAddSlotButtonClicked();
    });

    connect(m_d->m_slotGrid, &SlotGrid::removeSlot, this, [this]() {
        m_d->onRemoveSlotButtonClicked();
    });

    connect(m_d->m_slotGrid, &SlotGrid::beginInputSelect,
            this, [this] (s32 slotIndex) {

        // This is the callback that will be invoked by the eventwidget when input
        // selection is complete.
        auto callback = [this, slotIndex](Slot *destSlot, Pipe *sourcePipe, s32 sourceParamIndex)
        {
            m_d->onInputSelected(destSlot, slotIndex, sourcePipe, sourceParamIndex);
        };

        auto slot = m_d->m_model->opClone->getSlot(slotIndex);

        auto invalidSources = collect_dependent_objects(m_d->m_op.get());

        invalidSources.insert(m_d->m_op.get());

        m_d->m_eventWidget->selectInputFor(slot, m_d->m_userLevel, callback, invalidSources);
    });

    connect(m_d->m_slotGrid, &SlotGrid::clearInput,
            this, [this] (s32 slotIndex) {
        m_d->onInputCleared(slotIndex);
    });

    connect(m_d->m_slotGrid, &SlotGrid::inputPrefixEdited,
            this, [this] (s32 slotIndex, const QString &text) {
        m_d->onInputPrefixEdited(slotIndex, text);
    });

    // Editor component interaction (eval script, step, sample inputs)

    // compile
    connect(m_d->m_beginExpressionEditor, &ExpressionOperatorEditorComponent::compile,
            this, [this] () {
        m_d->updateModelFromGUI();
        m_d->model_compileBeginExpression();
    });

    connect(m_d->m_stepExpressionEditor, &ExpressionOperatorEditorComponent::compile,
            this, [this] () {
        m_d->updateModelFromGUI();
        m_d->model_compileStepExpression();
    });

    // step
    connect(m_d->m_stepExpressionEditor, &ExpressionOperatorEditorComponent::step,
            this, [this] () {
        if (m_d->m_beginExpressionEditor->isExpressionModified()
            || m_d->m_stepExpressionEditor->isExpressionModified())
        {
            m_d->updateModelFromGUI();
            m_d->model_compileStepExpression();
        }
        else
        {
            m_d->model_stepOperator();
        }
    });

    // sample input data
    connect(m_d->m_beginExpressionEditor, &ExpressionOperatorEditorComponent::sampleInputs,
            this, [this] () {
        m_d->model_sampleInputs();
    });

    connect(m_d->m_stepExpressionEditor, &ExpressionOperatorEditorComponent::sampleInputs,
            this, [this] () {
        m_d->model_sampleInputs();
    });

    // randomize input data
    connect(m_d->m_beginExpressionEditor, &ExpressionOperatorEditorComponent::randomizeInputs,
            this, [this] () {
        m_d->model_randomizeInputs();
    });

    connect(m_d->m_stepExpressionEditor, &ExpressionOperatorEditorComponent::randomizeInputs,
            this, [this] () {
        m_d->model_randomizeInputs();
    });

    auto set_tab_modified = [this] (int index, bool modified)
    {
        static const QString Marker = QSL(" *");

        auto text = m_d->m_tabWidget->tabText(index);
        bool hasMarker = text.endsWith(Marker);

        if (modified && !hasMarker)
            text += Marker;
        else if (!modified && hasMarker)
            text = text.mid(0, text.size() - Marker.size());

        m_d->m_tabWidget->setTabText(index, text);
    };

    // modified
    connect(m_d->m_beginExpressionEditor,
            &ExpressionOperatorEditorComponent::expressionModificationChanged,
            this, [set_tab_modified] (bool modified) {
        set_tab_modified(Private::TabIndex_Begin, modified);
    });

    connect(m_d->m_stepExpressionEditor,
            &ExpressionOperatorEditorComponent::expressionModificationChanged,
            this, [set_tab_modified] (bool modified) {
        set_tab_modified(Private::TabIndex_Step, modified);
    });

    // Initialize and misc setup
    switch (m_d->m_mode)
    {
        case ObjectEditorMode::New:
            {
                m_d->m_op->setObjectName(m_d->generateNameForNewOperator());
                setWindowTitle(QString("New  %1").arg(m_d->m_op->getDisplayName()));
            } break;
        case ObjectEditorMode::Edit:
            {
                setWindowTitle(QString("Edit %1").arg(m_d->m_op->getDisplayName()));
            } break;
    }

    add_widget_close_action(this);
    resize(800, 600);

    update_ok_button();
    m_d->updateModelFromOperator();
    m_d->repopulateGUIFromModel();
}

ExpressionOperatorDialog::~ExpressionOperatorDialog()
{
}

void ExpressionOperatorDialog::apply()
{
    AnalysisPauser pauser(m_d->m_eventWidget->getServiceProvider());

    m_d->updateModelFromGUI();
    save_to_operator(*m_d->m_model, *m_d->m_op);
    m_d->model_compileBeginExpression();
    m_d->model_compileStepExpression();
    m_d->m_op->setEventId(m_d->combo_eventSelect->currentData().toUuid());

    auto analysis = m_d->m_eventWidget->getAnalysis();

    switch (m_d->m_mode)
    {
        case ObjectEditorMode::New:
            {
                m_d->m_op->setUserLevel(m_d->m_userLevel);
                analysis->addOperator(m_d->m_op);

                m_d->m_mode = ObjectEditorMode::Edit;

                if (m_d->m_destDir)
                {
                    m_d->m_destDir->push_back(m_d->m_op);
                }
            } break;

        case ObjectEditorMode::Edit:
            {
                analysis->setOperatorEdited(m_d->m_op);
            } break;
    }

    analysis->beginRun(Analysis::KeepState, m_d->m_eventWidget->getVMEConfig());
    emit applied();
}

void ExpressionOperatorDialog::accept()
{
    apply();
    QDialog::accept();
}

void ExpressionOperatorDialog::reject()
{
    QDialog::reject();
}

//
// ExpressionOperatorSyntaxHighlighter
//

namespace
{
    static const int BlockState_InCode    = 0;
    static const int BlockState_InComment = 1;
} // end anon namespace

struct ExpressionOperatorSyntaxHighlighter::Private
{
    struct HighlightingRule
    {
        QRegExp pattern;
        QTextCharFormat format;
    };

    Private();


    QVector<HighlightingRule> highlightingRules;

    QRegularExpression reSingleLineComment;
    QRegExp reMultiLineCommentStart;
    QRegExp reMultiLineCommentEnd;

    QTextCharFormat commentFormat,
                    keywordFormat,
                    baseFunctionFormat,
                    stringFormat;
};

ExpressionOperatorSyntaxHighlighter::Private::Private()
    : reSingleLineComment("(#|//).*$")
    , reMultiLineCommentStart("/\\*")
    , reMultiLineCommentEnd("\\*/")
{
    /* Format colors are based on the Qt Creator Default Color Scheme. */
    commentFormat.setForeground(QColor("#000080"));
    stringFormat.setForeground(QColor("#008000"));

    keywordFormat.setForeground(QColor("#808000"));
    keywordFormat.setFontWeight(QFont::Bold);

    baseFunctionFormat.setForeground(QColor("#800080"));
    baseFunctionFormat.setFontWeight(QFont::Bold);

    HighlightingRule rule;

    // keywords
    auto keywords = QStringList()
        << "break" <<  "case" <<  "continue" <<  "default" <<  "false" <<  "for"
        << "if" << "else" << "ilike" <<  "in" << "like" << "and" <<  "nand" << "nor"
        << "not" <<  "null" <<  "or" <<   "repeat" << "return" <<  "shl" <<  "shr"
        << "swap" << "switch" << "true" <<  "until" << "var" <<  "while" << "xnor"
        << "xor";

    for (const auto &str: keywords)
    {
        rule.pattern = QRegExp("\\b" + str + "\\b");
        rule.format  = keywordFormat;
        highlightingRules.append(rule);
    }

    // base functions
    auto baseFunctions = QStringList()
        << "abs" << "acos" <<  "acosh" << "asin" <<  "asinh" << "atan" <<  "atanh"
        << "atan2" <<  "avg" <<  "ceil" <<  "clamp" <<  "cos" <<  "cosh" <<  "cot"
        << "csc" <<  "equal" <<  "erf" <<  "erfc" <<  "exp" <<  "expm1" << "floor"
        << "frac" << "hypot" << "iclamp" <<  "like" << "log" << "log10" <<  "log2"
        << "logn" << "log1p" << "mand" << "max" << "min" << "mod" << "mor" <<  "mul"
        << "ncdf" <<  "pow" <<  "root" <<  "round" <<  "roundn" <<  "sec" << "sgn"
        << "sin" << "sinc" << "sinh" << "sqrt" << "sum" << "swap" << "tan" << "tanh"
        << "trunc" <<  "not_equal" <<  "inrange" <<  "deg2grad" <<   "deg2rad"
        << "rad2deg" << "grad2deg";

    // a2 runtime lib functions
    baseFunctions << "is_valid" << "is_invalid" << "make_invalid" << "is_nan" << "valid_or";

    for (const auto &str: baseFunctions)
    {
        rule.pattern = QRegExp("\\b" + str + "\\b");
        rule.format  = baseFunctionFormat;
        highlightingRules.append(rule);
    }

    // other rules
    rule.pattern = QRegExp("'.*'");
    rule.format = stringFormat;
    highlightingRules.append(rule);
}

ExpressionOperatorSyntaxHighlighter::ExpressionOperatorSyntaxHighlighter(QTextDocument *parentDoc)
    : QSyntaxHighlighter(parentDoc)
    , m_d(std::make_unique<Private>())
{ }

ExpressionOperatorSyntaxHighlighter::~ExpressionOperatorSyntaxHighlighter()
{ }

void ExpressionOperatorSyntaxHighlighter::highlightBlock(const QString &text)
{
    //qDebug() << __PRETTY_FUNCTION__ << "text.size() =" << text.size()
    //    << "rule count =" << m_d->highlightingRules.size();


    for (const auto &rule: m_d->highlightingRules)
    {
        auto re(rule.pattern);

        int index = re.indexIn(text);

        //qDebug() << __PRETTY_FUNCTION__ << "pattern =" << re << "first index =" << index;

        while (index >= 0)
        {
            int length = re.matchedLength();
            setFormat(index, length, rule.format);
            index = re.indexIn(text, index + length);
            //qDebug() << __PRETTY_FUNCTION__ << "pattern =" << re << "new index =" << index;
        }
    }

    setCurrentBlockState(BlockState_InCode);
    int startIndex = 0;

    // C-style multiline comments

    if (previousBlockState() != BlockState_InComment)
    {
        startIndex = text.indexOf(m_d->reMultiLineCommentStart);
    }

    while (startIndex >= 0)
    {
        int endIndex = text.indexOf(m_d->reMultiLineCommentEnd, startIndex);
        int commentLength;

        if (endIndex == -1)
        {
            setCurrentBlockState(BlockState_InComment);
            commentLength = text.length() - startIndex;
        }
        else
        {
            commentLength = endIndex - startIndex + m_d->reMultiLineCommentEnd.matchedLength();
        }

        setFormat(startIndex, commentLength, m_d->commentFormat);
        startIndex = text.indexOf(m_d->reMultiLineCommentStart, startIndex + commentLength);
    }

    // single line comments
    QRegularExpressionMatch match;
    int index = text.indexOf(m_d->reSingleLineComment, 0, &match);

    if (index >= 0)
    {
        int length = match.capturedLength(); // matches to EoL
        setFormat(index, length, m_d->commentFormat);
    }
}

} // end namespace ui
} // end namespace analysis
