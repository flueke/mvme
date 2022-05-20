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
#ifndef __EXPRESSION_OPERATOR_DIALOG_P_H__
#define __EXPRESSION_OPERATOR_DIALOG_P_H__

#include "analysis/a2/a2.h"
#include "analysis/analysis_fwd.h"
#include "analysis/code_editor.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QSyntaxHighlighter>
#include <QTableWidget>
#include <QToolBar>
#include <QToolBox>

namespace analysis
{
namespace ui
{

class EventWidget;

class InputSelectButton: public QPushButton
{
    Q_OBJECT
    public:
        InputSelectButton(Slot *destSlot, EventWidget *eventWidget, QWidget *parent = nullptr);

        virtual bool eventFilter(QObject *watched, QEvent *event) override;

    private:
        EventWidget *m_eventWidget;
        Slot *m_destSlot;
};

class SlotGrid: public QFrame
{
    Q_OBJECT
    signals:
        void slotAdded();
        void slotRemoved();
        void beginInputSelect(s32 slotIndex);
        void clearInput(s32 slotIndex);
        void inputPrefixEdited(s32 slotIndex, const QString &text);

    public:
        explicit SlotGrid(QWidget *parent = nullptr);

        QVector<InputSelectButton *> selectButtons;
        QVector<QPushButton *> clearButtons;
        QVector<QLineEdit *> inputPrefixLineEdits;

        QGridLayout *slotLayout;

        QPushButton *addSlotButton,
                    *removeSlotButton;

    public slots:
        void endInputSelect();
};

template<typename Model>
void repopulate(SlotGrid *slotGrid, const Model &model, EventWidget *eventWidget, s32 userLevel);

/** Display for a single analysis pipe with optionally editable parameter
 * values. */
class ExpressionOperatorPipeView: public QWidget
{
    Q_OBJECT
    public:
        explicit ExpressionOperatorPipeView(QWidget *parent = nullptr);

        void setPipe(const a2::PipeVectors &a2_pipe, const QString &unit = {});

        void setDataEditable(bool b) { m_dataEditable = b; refresh(); }
        bool isDataEditable() const  { return m_dataEditable; }

        virtual bool eventFilter(QObject *watched, QEvent *event) override;

    public slots:
        void refresh();

    protected:
        virtual void showEvent(QShowEvent *event) override;

    private slots:
        void onCellChanged(int row, int col);

    private:
        void copy();
        void paste();

        QLabel *m_unitLabel;
        QTableWidget *m_tableWidget;
        a2::PipeVectors m_a2Pipe;
        bool m_dataEditable;
};

/* Displays multiple ExpressionOperatorPipeViews in a stacked widget. The
 * current pipe can be selected via a combobox. */
class ExpressionOperatorPipesComboView: public QWidget
{
    Q_OBJECT
    public:
        explicit ExpressionOperatorPipesComboView(QWidget *parent = nullptr);

        void setPipes(const std::vector<a2::PipeVectors> &pipes,
                      const QStringList &titles,
                      const QStringList &units);

        void setPipeDataEditable(bool b);
        bool isPipeDataEditable() const;
        s32 pipeCount() const { return m_pipeStack->count(); }

    public slots:
        void refresh();

    private:
        QComboBox *m_selectCombo;
        QStackedWidget *m_pipeStack;
        bool m_pipeDataEditable;
};

/** Display of expression (exprtk) interal errors and analysis specific
 * semantic errors in a table widget. */
class ExpressionErrorWidget: public QWidget
{
    Q_OBJECT
    signals:
        void parserErrorClicked(int line, int col);
        void parserErrorDoubleClicked(int line, int col);

    public:
        explicit ExpressionErrorWidget(QWidget *parent = nullptr);

        void setError(const std::exception_ptr &ep);

    public slots:
        void clear();

    private slots:
        void onCellClicked(int row, int col);
        void onCellDoubleClicked(int row, int col);

    private:
        struct Entry
        {
            enum class Type
            {
                ParserError,
                SymbolError,
                SemanticError,
                RuntimeError,
            };

            explicit Entry(Type t = Type::RuntimeError)
                : type(t)
                , semanticError("<unspecified>")
                , runtimeError("<unspecified>")
            {}

            Type type;
            a2::a2_exprtk::ParserError parserError;
            a2::a2_exprtk::SymbolError symbolError;;
            a2::ExpressionOperatorSemanticError semanticError;
            std::runtime_error runtimeError;
        };

        void assertConsistency();
        void prepareEntries(const std::exception_ptr &ep);
        void populateTable();

        QVector<Entry> m_entries;
        QTableWidget *m_errorTable;
};

/** Specialized editor widget for exprtk expressions. */
class ExpressionCodeEditor: public QWidget
{
    Q_OBJECT
    signals:
        void modificationChanged(bool changed);

    public:
        explicit ExpressionCodeEditor(QWidget *parent = nullptr);

        void setExpressionText(const QString &text);
        QString expressionText() const;

        CodeEditor *codeEditor() { return m_codeEditor; }

    public slots:
        void highlightError(int row, int col);
        void jumpToError(int row, int col);
        void clearErrorHighlight();

    private:
        CodeEditor *m_codeEditor;
};

/** Combines ExpressionCodeEditor and ExpressionErrorWidget. */
class ExpressionEditorWidget: public QWidget
{
    Q_OBJECT
    signals:
        void modificationChanged(bool changed);

    public:
        explicit ExpressionEditorWidget(QWidget *parent = nullptr);

        void setExpressionText(const QString &);
        QString expressionText() const;

        void setError(const std::exception_ptr &ep);

        ExpressionCodeEditor *getTextEditor() { return m_exprCodeEditor; }
        ExpressionErrorWidget *getErrorWidget() { return m_exprErrorWidget; }

    public slots:
        void clearError();

    private:
        ExpressionCodeEditor *m_exprCodeEditor;
        ExpressionErrorWidget *m_exprErrorWidget;
};

/** Complete editor component for one of the subexpressions of the
 * ExpressionOperator.
 * From left to right:
 * - input pipes toolbox
 * - editor with clickable error display and an "exec/eval" button.
 *   toolbar
 * - output pipes toolbox
 */
class ExpressionOperatorEditorComponent: public QWidget
{
    Q_OBJECT
    signals:
        void compile();
        void step();
        void sampleInputs();
        void randomizeInputs();

        void expressionModificationChanged(bool changed);

    public:
        explicit ExpressionOperatorEditorComponent(QWidget *parent = nullptr);

        void setExpressionText(const QString &text);
        QString expressionText() const;

        void setInputs(const std::vector<a2::PipeVectors> &pipes,
                       const QStringList &titles,
                       const QStringList &units);

        void setOutputs(const std::vector<a2::PipeVectors> &pipes,
                        const QStringList &titles,
                        const QStringList &units);

        void setEvaluationError(const std::exception_ptr &ep);
        void clearEvaluationError();

        QToolBar *getToolBar() { return m_toolBar; }
        ExpressionEditorWidget *getEditorWidget() { return m_editorWidget; }
        ExpressionOperatorPipesComboView *getInputPipesView() { return m_inputPipesView; }
        ExpressionOperatorPipesComboView *getOutputPipesView() { return m_outputPipesView; }

        QAction *getActionStep() { return m_actionStep; }

        s32 inputPipeCount()  { return m_inputPipesView->pipeCount(); }
        s32 outputPipeCount() { return m_outputPipesView->pipeCount(); }

        bool isExpressionModified() const;

    public slots:
        void setExpressionModified(bool modified);

    protected:
        virtual void showEvent(QShowEvent *event) override;
        virtual void resizeEvent(QResizeEvent *event) override;

    private slots:
        void onActionHelp_triggered();

    private:
        void setHSplitterSizes();

        ExpressionOperatorPipesComboView *m_inputPipesView;
        ExpressionOperatorPipesComboView *m_outputPipesView;
        QToolBar *m_toolBar;
        ExpressionEditorWidget *m_editorWidget;
        QSplitter *m_hSplitter;
        QAction *m_actionStep = nullptr;
};

/* This class is based on the Qt Syntax Highlighter Example. */
struct ExpressionOperatorSyntaxHighlighter: public QSyntaxHighlighter
{
    Q_OBJECT
    using QSyntaxHighlighter::QSyntaxHighlighter;
    public:
        explicit ExpressionOperatorSyntaxHighlighter(QTextDocument *parentDoc);
        virtual ~ExpressionOperatorSyntaxHighlighter();

    protected:
        virtual void highlightBlock(const QString &text) override;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;

};

} // end namespace ui
} // end namespace analysis

#endif /* __EXPRESSION_OPERATOR_DIALOG_P_H__ */
