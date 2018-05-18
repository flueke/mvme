#include "expression_operator_dialog.h"
#include "expression_operator_dialog_p.h"

#include "a2_adapter.h"
#include "a2/a2_impl.h"
#include "analysis_ui_p.h"
#include "mvme_context_lib.h"

#include <QHeaderView>
#include <QTabWidget>

/* NOTES:
 - new slot grid implementation with space for the input variable name column.
   2nd step: try to abstract this so the slot grid can be instantiated and customized and is reusable

 - workflow:
   select inputs, write init script, run init script, check output definition is as desired,
   write step script, test with sample data, accept changes

 - required:
   clickable error display for scripts. errors can come from expr_tk (wrapped
   in a2_exprtk layer) and from the ExpressionOperator itself (e.g. malformed beginExpr output, SemanticError).

 - utility: symbol table inspection

 - FIXME: unit labels for inputs _and_ outputs are missing

 - on eval error the output part should keep it's size. the whole splitter size
   thing is ugly.

 - resize cols and rows to contents on widget resize event. rate limit this or
   maybe use a delayed time or something if needed.

 - implement the save functionality. use the a1 pipes to bring the original
   operator into the desired state. also the "add/edited" part of the analysis
   has to be called.

 */

namespace analysis
{

//
// InputSelectButton
//

InputSelectButton::InputSelectButton(Slot *destSlot, s32 userLevel,
                                     EventWidget *eventWidget, QWidget *parent)
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
ExpressionOperatorPipeView::ExpressionOperatorPipeView(QWidget *parent)
    : QWidget(parent)
    , m_unitLabel(new QLabel(this))
    , m_tableWidget(new QTableWidget(this))
    , m_a2Pipe{}
{
    auto infoLayout = new QFormLayout;
    infoLayout->addRow(QSL("Unit:"), m_unitLabel);

    auto layout = new QVBoxLayout(this);
    //layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(infoLayout);
    layout->addWidget(m_tableWidget);

    // columns:
    // Valid, Value, lower Limit, upper Limit
    m_tableWidget->setColumnCount(4);
    m_tableWidget->setHorizontalHeaderLabels({"Valid", "Value", "Lower Limit", "Upper Limit"});

    refresh();
}

void ExpressionOperatorPipeView::showEvent(QShowEvent *event)
{
    qDebug() << __PRETTY_FUNCTION__ << this;

    //m_tableWidget->resizeColumnsToContents();
    //m_tableWidget->resizeRowsToContents();

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

            item->setText(columns[ci]);
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
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

//
// ExpressionOperatorPipesView
//
ExpressionOperatorPipesView::ExpressionOperatorPipesView(QWidget *parent)
    : QToolBox(parent)
{
}

void ExpressionOperatorPipesView::showEvent(QShowEvent *event)
{
    qDebug() << __PRETTY_FUNCTION__ << this;

    QWidget::showEvent(event);
}

void ExpressionOperatorPipesView::setPipes(const std::vector<a2::PipeVectors> &pipes,
                                           const QStringList &titles,
                                           const QStringList &units)
{
    assert(static_cast<s32>(pipes.size()) == titles.size());
    assert(static_cast<s32>(pipes.size()) == units.size());

    s32 prevIndex = currentIndex();
    s32 newSize = titles.size();

    while (count() > newSize)
    {
        auto w = widget(count() - 1);
        removeItem(count() - 1);
        delete w;
    }

    for (s32 pi = 0; pi < newSize; pi++)
    {
        ExpressionOperatorPipeView *pv = nullptr;

        if ((pv = qobject_cast<ExpressionOperatorPipeView *>(widget(pi))))
        {
            pv->setPipe(pipes[pi], units[pi]);
            setItemText(pi, titles[pi]);
        }
        else
        {
            pv = new ExpressionOperatorPipeView;
            pv->setPipe(pipes[pi], units[pi]);
            addItem(pv, titles[pi]);
        }
    }
}

QSize ExpressionOperatorPipesView::sizeHint() const
{
    auto result = QToolBox::sizeHint();
    qDebug() << __PRETTY_FUNCTION__ << "width" << result.width();
    return result;
}

void ExpressionOperatorPipesView::refresh()
{
    for (s32 i = 0; i < count(); i++)
    {
        if (auto pv = qobject_cast<ExpressionOperatorPipeView *>(widget(i)))
        {
            pv->refresh();
        }
    }
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

    connect(m_errorTable, &QTableWidget::cellDoubleClicked,
            this, &ExpressionErrorWidget::onCellDoubleClicked);
}

void ExpressionErrorWidget::showEvent(QShowEvent *event)
{
    qDebug() << __PRETTY_FUNCTION__ << this;

    m_errorTable->resizeRowsToContents();
    m_errorTable->resizeColumnsToContents();

    QWidget::showEvent(event);
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
                    set_next_item(QString::fromStdString(entry.parserError.mode));
                    set_next_item(QString::fromStdString(entry.parserError.diagnostic));
                    set_next_item(QString::number(entry.parserError.line));
                    set_next_item(QString::number(entry.parserError.column));
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

    m_errorTable->resizeRowsToContents();
    m_errorTable->resizeColumnsToContents();
}

void ExpressionErrorWidget::clear()
{
    assertConsistency();

    m_entries.clear();
    m_errorTable->clearContents();
    m_errorTable->setRowCount(0);

    assertConsistency();
}

void ExpressionErrorWidget::onCellDoubleClicked(int row, int column)
{
    assertConsistency();
    assert(row < m_entries.size());
    assert(row < m_errorTable->rowCount());

    const auto &entry = m_entries[row];

    if (entry.type == Entry::Type::ParserError)
    {
        emit parserErrorClicked(
            static_cast<int>(entry.parserError.line),
            static_cast<int>(entry.parserError.column));
    }
}

void ExpressionErrorWidget::assertConsistency()
{
    qDebug() << __PRETTY_FUNCTION__ << "entries:" << m_entries.size()
        << ", table rows:" << m_errorTable->rowCount();
    assert(m_entries.size() == m_errorTable->rowCount());
}

//
// ExpressionTextEditor
//
int calculate_tabstop_width(const QFont &font, int tabstop)
{
    QString spaces;
    for (int i = 0; i < tabstop; ++i) spaces += " ";
    QFontMetrics metrics(font);
    return metrics.width(spaces);
}

static const int TabStop = 4;

ExpressionTextEditor::ExpressionTextEditor(QWidget *parent)
    : QWidget(parent)
    , m_textEdit(new QPlainTextEdit)
{
    auto font = make_monospace_font();
    font.setPointSize(8);
    m_textEdit->setFont(font);
    m_textEdit->setTabStopWidth(calculate_tabstop_width(font, TabStop));

    auto widgetLayout = new QHBoxLayout(this);
    widgetLayout->addWidget(m_textEdit);
    widgetLayout->setContentsMargins(0, 0, 0, 0);
}

void ExpressionTextEditor::setExpressionText(const QString &text)
{
    if (text != m_textEdit->toPlainText())
    {
        m_textEdit->setPlainText(text);
    }
}

QString ExpressionTextEditor::expressionText() const
{
    return m_textEdit->toPlainText();
}

void ExpressionTextEditor::highlightError(int row, int col)
{
    qDebug() << __PRETTY_FUNCTION__ << row << col;
}

void ExpressionTextEditor::clearErrorHighlight()
{
    qDebug() << __PRETTY_FUNCTION__;
}

//
// ExpressionEditorWidget
//
ExpressionEditorWidget::ExpressionEditorWidget(QWidget *parent)
    : QWidget(parent)
    , m_exprTextEdit(new ExpressionTextEditor)
    , m_exprErrors(new ExpressionErrorWidget)
{
    auto splitter = new QSplitter(Qt::Vertical);
    //splitter->setHandleWidth(0);
    splitter->addWidget(m_exprTextEdit);
    splitter->addWidget(m_exprErrors);
    splitter->setStretchFactor(0, 80);
    splitter->setStretchFactor(1, 20);

    auto widgetLayout = new QHBoxLayout(this);;
    widgetLayout->setContentsMargins(0, 0, 0, 0);
    widgetLayout->addWidget(splitter);

    connect(m_exprErrors, &ExpressionErrorWidget::parserErrorClicked,
            m_exprTextEdit, &ExpressionTextEditor::highlightError);
}

void ExpressionEditorWidget::setExpressionText(const QString &text)
{
    m_exprTextEdit->setExpressionText(text);
}

QString ExpressionEditorWidget::expressionText() const
{
    return m_exprTextEdit->expressionText();
}

void ExpressionEditorWidget::setError(const std::exception_ptr &ep)
{
    m_exprErrors->setError(ep);
}

void ExpressionEditorWidget::clearError()
{
    m_exprErrors->clear();
    m_exprTextEdit->clearErrorHighlight();
}

//
// ExpressionOperatorEditorComponent
//
ExpressionOperatorEditorComponent::ExpressionOperatorEditorComponent(QWidget *parent)
    : QWidget(parent)
    , m_inputPipesView(new ExpressionOperatorPipesView)
    , m_outputPipesView(new ExpressionOperatorPipesView)
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

    action = tb_aa(QIcon(":/window_icon.png"), QSL("&Compile"),
                   this, &ExpressionOperatorEditorComponent::compile);
    action->setToolTip(QSL("Recompile the current expression"));

    m_actionStep = tb_aa(QIcon(":/window_icon.png"), QSL("S&tep"),
                   this, &ExpressionOperatorEditorComponent::step);
    m_actionStep->setToolTip(QSL("Perform one step of the operator without prior recompilation."));
    m_actionStep->setEnabled(false);


    tb_sep();

    tb_aa(QIcon(":/window_icon.png"), QSL("&Sample Inputs"),
                   this, &ExpressionOperatorEditorComponent::sampleInputs);

    tb_aa(QIcon(":/window_icon.png"), QSL("&Randomize Inputs"),
                   this, &ExpressionOperatorEditorComponent::randomizeInputs);

    tb_sep();

    QPlainTextEdit *textEdit = m_editorWidget->getTextEditor()->textEdit();

    QAction* actionUndo = tb_aa(QIcon::fromTheme("edit-undo"), "Undo",
                                textEdit, &QPlainTextEdit::undo);
    actionUndo->setEnabled(false);

    QAction *actionRedo = tb_aa(QIcon::fromTheme("edit-redo"), "Redo",
                                textEdit, &QPlainTextEdit::redo);
    actionRedo->setEnabled(false);

    connect(textEdit, &QPlainTextEdit::undoAvailable, actionUndo, &QAction::setEnabled);
    connect(textEdit, &QPlainTextEdit::redoAvailable, actionRedo, &QAction::setEnabled);

    tb_sep();

    //tb_aa("Show Symbol Table");
    //tb_aa("Generate Code", this, &ExpressionOperatorEditorComponent::generateDefaultCode);
    //tb_aa("&Help", this, &ExpressionOperatorEditorComponent::onActionHelp_triggered);

#undef tb_sep
#undef tb_aa

}

void ExpressionOperatorEditorComponent::setHSplitterSizes()
{
    int totalWidth = m_hSplitter->width();
    QList<int> sizes = { 0, 0, 0 };

    sizes[0] = m_inputPipesView->sizeHint().width();
    totalWidth -= sizes[0];

    qDebug() << __PRETTY_FUNCTION__ << "width of input pipes view from sizehint:" << sizes[0];

    sizes[2] = m_outputPipesView->sizeHint().width();
    totalWidth -= sizes[2];

    sizes[1] = std::max(totalWidth, 800);

    qDebug() << __PRETTY_FUNCTION__ << "width of the editor area:" << sizes[1];

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
}

void ExpressionOperatorEditorComponent::setOutputs(const std::vector<a2::PipeVectors> &pipes,
                                                  const QStringList &titles,
                                                  const QStringList &units)
{
    m_outputPipesView->setPipes(pipes, titles, units);
}

void ExpressionOperatorEditorComponent::setEvaluationError(const std::exception_ptr &ep)
{
    m_editorWidget->setError(ep);
}

void ExpressionOperatorEditorComponent::clearEvaluationError()
{
    m_editorWidget->clearError();
}

//
// ExpressionOperatorDialog and Private implementation
//

namespace
{
    struct Model;
} // end anon namespace

struct ExpressionOperatorDialog::Private
{
    static const size_t WorkArenaSegmentSize = Kilobytes(4);

    Private(ExpressionOperatorDialog *q)
        : m_q(q)
        , m_arena(WorkArenaSegmentSize)
    {}

    ExpressionOperatorDialog *m_q;

    // The operator being added or edited
    std::shared_ptr<ExpressionOperator> m_op;
    // The userlevel the operator is placed in
    int m_userLevel;
    // new or edit
    OperatorEditorMode m_mode;
    // backpointer to the eventwidget used for input selection
    EventWidget *m_eventWidget;
    // data transfer to/from gui and storage of inputs
    std::unique_ptr<Model> m_model;
    // work arena for a2 operator creation
    memory::Arena m_arena;
    // the a2 operator recreated when the user wants to evaluate one of the
    // scripts
    a2::Operator m_a2Op;


    QTabWidget *m_tabWidget;

    // tab0: operator name and input select
    QLineEdit *le_operatorName;
    SlotGrid m_slotGrid;

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

        friend void assert_consistency(const a2::PipeVectors &a2_pipe,
                                       const A2PipeStorage &storage);

    private:
        std::vector<double> data;
        std::vector<double> lowerLimits;
        std::vector<double> upperLimits;

};

struct Model
{
    /* A clone of the original operator that's being edited. This is here so
     * that we have proper Slot pointers to pass to
     * EventWidget::selectInputFor() on input selection.
     *
     * Note that pipe -> slot connections are not made on this clone as the
     * source pipes would be modified by that operation. Instead the selected
     * input pipes and indexes are stored in the model aswell. Once the user
     * accepts the changes the original operator will be modified according to
     * the data stored in the model. */
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

void assert_consistency(const a2::PipeVectors &a2_pipe, const A2PipeStorage &storage)
{
    //qDebug() << __PRETTY_FUNCTION__ << a2_pipe.data.size;

    assert(a2_pipe.data.size == a2_pipe.lowerLimits.size);
    assert(a2_pipe.data.size == a2_pipe.upperLimits.size);

    const s32 expected_size = static_cast<s32>(storage.data.size());

    assert(a2_pipe.data.size == expected_size);
    assert(a2_pipe.lowerLimits.size == expected_size);
    assert(a2_pipe.upperLimits.size == expected_size);

    assert(a2_pipe.data.data == storage.data.data());
    assert(a2_pipe.lowerLimits.data == storage.lowerLimits.data());
    assert(a2_pipe.upperLimits.data == storage.upperLimits.data());
}

void assert_internal_consistency(const Model &model)
{
    assert(model.inputs.size() == model.inputStorage.size());
    assert(model.inputs.size() == model.inputIndexes.size());
    assert(model.inputs.size() == model.inputPrefixes.size());
    assert(model.inputs.size() == model.inputUnits.size());
    assert(model.inputs.size() == model.a1_inputPipes.size());

    for (size_t ii = 0; ii < model.inputs.size(); ii++)
    {
        assert_consistency(model.inputs[ii], model.inputStorage[ii]);
    }
}

void assert_consistency(const Model &model)
{
    assert(model.opClone);
    assert(model.opClone->getNumberOfSlots() > 0);
    assert(static_cast<size_t>(model.opClone->getNumberOfSlots()) == model.inputs.size());

    assert_internal_consistency(model);
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
    qDebug() << model.opClone->getInputPrefix(si);
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
    assert_consistency(model);

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

    assert_consistency(model);
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

        if (slot && slot->isConnected() && slot->isArrayConnection())
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

SlotGrid make_slotgrid(QWidget *parent = nullptr)
{
    SlotGrid sg = {};

    sg.slotFrame  = new QFrame;
    sg.slotLayout = new QGridLayout(sg.slotFrame);

    sg.slotLayout->setContentsMargins(2, 2, 2, 2);
    sg.slotLayout->setColumnStretch(0, 0); // index
    sg.slotLayout->setColumnStretch(1, 1); // select button with input name
    sg.slotLayout->setColumnStretch(2, 1); // variable name inside the script
    sg.slotLayout->setColumnStretch(3, 0); // clear selection button

    sg.addSlotButton = new QPushButton(QIcon(QSL(":/list_add.png")), QString());
    sg.addSlotButton->setToolTip(QSL("Add input"));

    sg.removeSlotButton = new QPushButton(QIcon(QSL(":/list_remove.png")), QString());
    sg.removeSlotButton->setToolTip(QSL("Remove last input"));

    auto addRemoveSlotButtonsLayout = new QHBoxLayout;
    addRemoveSlotButtonsLayout->setContentsMargins(2, 2, 2, 2);
    addRemoveSlotButtonsLayout->addStretch();
    addRemoveSlotButtonsLayout->addWidget(sg.addSlotButton);
    addRemoveSlotButtonsLayout->addWidget(sg.removeSlotButton);

    sg.outerFrame = new QFrame(parent);
    auto outerLayout = new QVBoxLayout(sg.outerFrame);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(sg.slotFrame);
    outerLayout->addLayout(addRemoveSlotButtonsLayout);
    outerLayout->setStretch(0, 1);

    return sg;
}

void slotgrid_endInputSelect(SlotGrid *sg)
{
    for (auto &selectButton: sg->selectButtons)
    {
        selectButton->setChecked(false);
    }
}

void repopulate_slotgrid(SlotGrid *sg, Model &model, EventWidget *eventWidget, s32 userLevel)
{
    assert_consistency(model);

    // Clear the slot grid and the select buttons
    while (QLayoutItem *child = sg->slotLayout->takeAt(0))
    {
        if (auto widget = child->widget())
        {
            //delete widget;
            widget->deleteLater();
        }
        delete child;
    }

    Q_ASSERT(sg->slotLayout->count() == 0);

    // These have been deleted by the layout clearing code above.
    sg->selectButtons.clear();
    sg->clearButtons.clear();
    sg->inputPrefixLineEdits.clear();

    // Repopulate

    const auto &op = model.opClone;
    assert(op);

    const s32 slotCount = op->getNumberOfSlots();
    assert(slotCount > 0);

    s32 row = 0;
    s32 col = 0;

    sg->slotLayout->addWidget(new QLabel(QSL("Input#")), row, col++);
    sg->slotLayout->addWidget(new QLabel(QSL("Select")), row, col++);
    sg->slotLayout->addWidget(new QLabel(QSL("Clear")), row, col++);
    sg->slotLayout->addWidget(new QLabel(QSL("Variable Name")), row, col++);

    row++;

    for (s32 slotIndex = 0; slotIndex < slotCount; slotIndex++)
    {
        Slot *slot = op->getSlot(slotIndex);

        auto selectButton = new InputSelectButton(slot, userLevel, eventWidget);
        sg->selectButtons.push_back(selectButton);

        auto clearButton = new QPushButton(QIcon(":/dialog-close.png"), QString());
        sg->clearButtons.push_back(clearButton);

        auto le_inputPrefix  = new QLineEdit;
        sg->inputPrefixLineEdits.push_back(le_inputPrefix);

        if (model.a1_inputPipes[slotIndex])
        {
            QString sourceText = model.a1_inputPipes[slotIndex]->source->objectName();

            if (model.inputIndexes[slotIndex] != a2::NoParamIndex)
            {
                sourceText = (QSL("%1[%2]")
                              .arg(sourceText)
                              .arg(model.inputIndexes[slotIndex]));
            }

            selectButton->setText(sourceText);
        }

        le_inputPrefix->setText(QString::fromStdString(model.inputPrefixes[slotIndex]));

        QObject::connect(selectButton, &QPushButton::toggled,
                sg->outerFrame, [=] (bool checked) {

            // Cancel any previous input selection. Has no effect if no input
            // selection was active.
            eventWidget->endSelectInput();

            if (checked)
            {
                emit selectButton->beginInputSelect();

                eventWidget->selectInputFor(
                    slot, userLevel,
                    [selectButton, slotIndex] (Slot *destSlot,
                                               Pipe *sourcePipe, s32 sourceParamIndex) {
                    // This is the callback that will be invoked by the
                    // eventwidget when input selection is complete.

                    selectButton->setChecked(false);
                    emit selectButton->inputSelected(destSlot, slotIndex,
                                                     sourcePipe, sourceParamIndex);
                });

                // Uncheck the other buttons.
                for (s32 bi = 0; bi < sg->selectButtons.size(); bi++)
                {
                    if (bi != slotIndex)
                        sg->selectButtons[bi]->setChecked(false);
                }
            }
        });

        QObject::connect(clearButton, &QPushButton::clicked,
                         sg->outerFrame, [selectButton, eventWidget, sg]() {
            selectButton->setText(QSL("<select>"));
            selectButton->setChecked(false);
            slotgrid_endInputSelect(sg);
            eventWidget->endSelectInput();
        });

        col = 0;

        sg->slotLayout->addWidget(new QLabel(slot->name), row, col++);
        sg->slotLayout->addWidget(selectButton, row, col++);
        sg->slotLayout->addWidget(clearButton, row, col++);
        sg->slotLayout->addWidget(le_inputPrefix, row, col++);

        row++;
    }

    sg->slotLayout->setRowStretch(row, 1);

    sg->slotLayout->setColumnStretch(0, 0);
    sg->slotLayout->setColumnStretch(1, 1);
    sg->slotLayout->setColumnStretch(2, 0);
    sg->slotLayout->setColumnStretch(3, 1);

    sg->removeSlotButton->setEnabled(slotCount > 1);
}

void ExpressionOperatorDialog::Private::repopulateSlotGridFromModel()
{
    repopulate_slotgrid(&m_slotGrid, *m_model, m_eventWidget, m_userLevel);

    for (auto &selectButton: m_slotGrid.selectButtons)
    {
        QObject::connect(selectButton, &InputSelectButton::inputSelected,
                         m_q, [this] (Slot *destSlot, s32 slotIndex,
                                      Pipe *sourcePipe, s32 sourceParamIndex) {
            this->onInputSelected(destSlot, slotIndex, sourcePipe, sourceParamIndex);
        });
    }

    for (s32 bi = 0; bi < m_slotGrid.clearButtons.size(); ++bi)
    {
        auto &clearButton = m_slotGrid.clearButtons[bi];

        QObject::connect(clearButton, &QPushButton::clicked,
                         m_q, [this, bi] () {
            this->onInputCleared(bi);
        });

        auto le = m_slotGrid.inputPrefixLineEdits[bi];

        QObject::connect(le, &QLineEdit::editingFinished,
                         m_slotGrid.outerFrame, [this, bi, le] () {
            qDebug() << "inputPrefixLineEdit signaled editingFinished";
            assert(static_cast<size_t>(bi) < m_model->inputPrefixes.size());
            this->onInputPrefixEdited(bi, le->text());
        });
    }
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
           == static_cast<size_t>(m_slotGrid.inputPrefixLineEdits.size()));

    for (s32 i = 0; i < m_slotGrid.inputPrefixLineEdits.size(); i++)
    {
        m_model->inputPrefixes[i] = m_slotGrid.inputPrefixLineEdits[i]->text().toStdString();
    }

    m_model->beginExpression = m_beginExpressionEditor->expressionText().toStdString();
    m_model->stepExpression  = m_stepExpressionEditor->expressionText().toStdString();
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
    m_stepExpressionEditor->getActionStep()->setEnabled(m_a2Op.type == a2::Operator_Expression);
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
    qDebug() << __PRETTY_FUNCTION__ << destSlot << slotIndex << sourcePipe << sourceParamIndex;
    connect_input(*m_model, slotIndex, sourcePipe, sourceParamIndex);

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
        }
        catch (const std::runtime_error &e)
        {
            qDebug() << __FUNCTION__ << "runtime_error:" << QString::fromStdString(e.what());

            m_stepExpressionEditor->setEvaluationError(std::current_exception());
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

        auto a2_sourcePipe = find_output_pipe(a2State, a1_pipe);
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

ExpressionOperatorDialog::ExpressionOperatorDialog(
    const std::shared_ptr<ExpressionOperator> &op,
    int userLevel, OperatorEditorMode mode, EventWidget *eventWidget)

    : QDialog(eventWidget)
    , m_d(std::make_unique<Private>(this))
{
    m_d->m_op          = op;
    m_d->m_userLevel   = userLevel;
    m_d->m_mode        = mode;
    m_d->m_eventWidget = eventWidget;
    m_d->m_tabWidget   = new QTabWidget;
    m_d->m_model       = std::make_unique<Model>();

    // tab0: operator name and input select
    {
        m_d->le_operatorName = new QLineEdit;

        m_d->m_slotGrid = make_slotgrid(this);
        auto gb_slotGrid = new QGroupBox(QSL("Inputs"), this);
        auto gb_slotGridLayout = new QHBoxLayout(gb_slotGrid);
        gb_slotGridLayout->setContentsMargins(2, 2, 2, 2);
        gb_slotGridLayout->addWidget(m_d->m_slotGrid.outerFrame);

        auto page = new QWidget(this);
        auto l    = new QFormLayout(page);

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
    m_d->m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_d->m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(false);

    connect(m_d->m_buttonBox, &QDialogButtonBox::accepted, this, &ExpressionOperatorDialog::accept);
    connect(m_d->m_buttonBox, &QDialogButtonBox::rejected, this, &ExpressionOperatorDialog::reject);

    // main layout
    auto dialogLayout = new QVBoxLayout(this);
    dialogLayout->addWidget(m_d->m_tabWidget);
    dialogLayout->addWidget(m_d->m_buttonBox);
    //dialogLayout->setContentsMargins(2, 2, 2, 2);
    dialogLayout->setStretch(0, 1);

    // Slotgrid interactions
    connect(m_d->m_slotGrid.addSlotButton, &QPushButton::clicked, this, [this]() {
        m_d->onAddSlotButtonClicked();
    });

    connect(m_d->m_slotGrid.removeSlotButton, &QPushButton::clicked, this, [this] () {
        m_d->onRemoveSlotButtonClicked();
    });

    // Editor component interaction (eval script, step, sample inputs)

    // eval
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
        m_d->model_stepOperator();
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

    // Initialize and misc setup
    switch (m_d->m_mode)
    {
        case OperatorEditorMode::New:
            {
                setWindowTitle(QString("New  %1").arg(m_d->m_op->getDisplayName()));
            } break;
        case OperatorEditorMode::Edit:
            {
                setWindowTitle(QString("Edit %1").arg(m_d->m_op->getDisplayName()));
            } break;
    }

    add_widget_close_action(this);
    resize(800, 600);

    m_d->updateModelFromOperator();
    m_d->repopulateGUIFromModel();
}

ExpressionOperatorDialog::~ExpressionOperatorDialog()
{
}

void ExpressionOperatorDialog::accept()
{
    AnalysisPauser pauser(m_d->m_eventWidget->getContext());

    m_d->updateModelFromGUI();
    save_to_operator(*m_d->m_model, *m_d->m_op);

    auto analysis = m_d->m_eventWidget->getAnalysis();

    switch (m_d->m_mode)
    {
        case OperatorEditorMode::New:
            {
                analysis->addOperator(m_d->m_eventWidget->getEventId(),
                                      m_d->m_op, m_d->m_userLevel);
            } break;

        case OperatorEditorMode::Edit:
            {
                // TODO: do_beginRun_forward here instead of the full beginRun()
                // TODO: encapsulate further. too many details here

                auto runInfo = m_d->m_eventWidget->getRunInfo();
                auto vmeMap  = vme_analysis_common::build_id_to_index_mapping(
                    m_d->m_eventWidget->getVMEConfig());

                analysis->setModified();
                analysis->beginRun(runInfo, vmeMap);
            } break;
    }

    QDialog::accept();
}

void ExpressionOperatorDialog::reject()
{
    QDialog::reject();
}

} // end namespace analysis
