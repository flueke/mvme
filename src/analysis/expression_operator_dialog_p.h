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
#ifndef __EXPRESSION_OPERATOR_DIALOG_P_H__
#define __EXPRESSION_OPERATOR_DIALOG_P_H__

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QToolBar>
#include <QToolBox>

#include "a2/a2.h"

namespace analysis
{

class EventWidget;
class Pipe;
struct Slot;

class InputSelectButton: public QPushButton
{
    Q_OBJECT
    signals:
        void beginInputSelect();
        void inputSelected(Slot *destSlot, s32 destSlotIndex,
                           Pipe *selectedPipe, s32 selectedParamIndex);

    public:
        InputSelectButton(Slot *destSlot, s32 userLevel,
                          EventWidget *eventWidget, QWidget *parent = nullptr);

        virtual bool eventFilter(QObject *watched, QEvent *event) override;

    private:
        EventWidget *m_eventWidget;
        Slot *m_destSlot;
};

struct SlotGrid
{
    QFrame *outerFrame,
           *slotFrame;

    QGridLayout *slotLayout;

    QPushButton *addSlotButton,
                *removeSlotButton;

    QVector<InputSelectButton *> selectButtons;
    QVector<QPushButton *> clearButtons;
    QVector<QLineEdit *> inputPrefixLineEdits;
};

/** Display for a single analysis pipe with optionally editable parameter
 * value. */
class ExpressionOperatorPipeView: public QWidget
{
    Q_OBJECT
    public:
        ExpressionOperatorPipeView(QWidget *parent = nullptr);

        void setPipe(const a2::PipeVectors &a2_pipe, const QString &unit = {});

    public slots:
        void refresh();

    protected:
        virtual void showEvent(QShowEvent *event) override;

    private:
        QLabel *m_unitLabel;
        QTableWidget *m_tableWidget;
        a2::PipeVectors m_a2Pipe;
};

/** Vertical arrangement of a group of ExpressionOperatorPipeViews in a
 * QToolBox. */
class ExpressionOperatorPipesView: public QToolBox
{
    Q_OBJECT
    public:
        ExpressionOperatorPipesView(QWidget *parent = nullptr);

        void setPipes(const std::vector<a2::PipeVectors> &pipes,
                      const QStringList &titles,
                      const QStringList &units);

        virtual QSize sizeHint() const override;

    public slots:
        void refresh();

    protected:
        virtual void showEvent(QShowEvent *event) override;
};

/** Display of expression (exprtk) interal errors and analysis specific
 * semantic errors in a table widget. */
class ExpressionErrorWidget: public QWidget
{
    Q_OBJECT
    signals:
        void parserErrorClicked(int line, int col);

    public:
        ExpressionErrorWidget(QWidget *parent = nullptr);

        void setError(const std::exception_ptr &ep);

    public slots:
        void clear();

    protected:
        virtual void showEvent(QShowEvent *event) override;

    private slots:
        void onCellDoubleClicked(int row, int column);

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

            Entry(Type t = Type::RuntimeError)
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
class ExpressionTextEditor: public QWidget
{
    Q_OBJECT
    public:
        ExpressionTextEditor(QWidget *parent = nullptr);

        void setExpressionText(const QString &text);
        QString expressionText() const;

        QPlainTextEdit *textEdit() { return m_textEdit; }

    public slots:
        void highlightError(int row, int col);
        void clearErrorHighlight();

    private:
        QPlainTextEdit *m_textEdit;
};

/** Combines ExpressionTextEditor and ExpressionErrorWidget. */
class ExpressionEditorWidget: public QWidget
{
    Q_OBJECT
    public:
        ExpressionEditorWidget(QWidget *parent = nullptr);

        void setExpressionText(const QString &);
        QString expressionText() const;

        void setError(const std::exception_ptr &ep);

        ExpressionTextEditor *getTextEditor() { return m_exprTextEdit; }
        ExpressionErrorWidget *getErrorWidget() { return m_exprErrors; }

    public slots:
        void clearError();

    private:
        ExpressionTextEditor *m_exprTextEdit;
        ExpressionErrorWidget *m_exprErrors;
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
        void eval();
        void step();
        void sampleInputs();
        void randomizeInputs();
        void generateDefaultCode();

    public:
        ExpressionOperatorEditorComponent(QWidget *parent = nullptr);

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
        ExpressionOperatorPipesView *getInputPipesView() { return m_inputPipesView; }
        ExpressionOperatorPipesView *getOutputPipesView() { return m_outputPipesView; }

        QAction *getActionStep() { return m_actionStep; }

    protected:
        virtual void showEvent(QShowEvent *event) override;
        virtual void resizeEvent(QResizeEvent *event) override;

    private:
        void setHSplitterSizes();

        //void onActionHelp_triggered();


        ExpressionOperatorPipesView *m_inputPipesView;
        ExpressionOperatorPipesView *m_outputPipesView;
        QToolBar *m_toolBar;
        ExpressionEditorWidget *m_editorWidget;
        QSplitter *m_hSplitter;
        QAction *m_actionStep = nullptr;
};

} // end namespace analysis

#endif /* __EXPRESSION_OPERATOR_DIALOG_P_H__ */
