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
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
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
        void inputSelected(Pipe *selectedInputPipe, s32 selectedParamIndex);

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
    QVector<QLineEdit *> varNameLineEdits;
};

/** Display for a single analysis pipe with optionally editable parameter
 * value. */
class ExpressionOperatorPipeView: public QWidget
{
    Q_OBJECT
    public:
        ExpressionOperatorPipeView(QWidget *parent = nullptr);

        void setPipe(Pipe *a1_pipe);
        void setPipe(const a2::PipeVectors &a2_pipe);

    public slots:
        void refresh();

    private:
        QTableWidget *m_tableWidget;

        Pipe *m_a1Pipe;
        a2::PipeVectors m_a2Pipe;
        memory::Arena m_pipeAdapterStorage;
};

/** Vertical arrangement of a group of ExpressionOperatorPipeViews in a
 * QToolBox. */
class ExpressionOperatorPipesToolBox: public QToolBox
{
    Q_OBJECT
    public:
        ExpressionOperatorPipesToolBox(QWidget *parent = nullptr);

        int addPipe(Pipe *a1_pipe, const QString &title = QString());
        int addPipe(const a2::PipeVectors &a2_pipe, const QString &title = QString());

        bool setPipe(int index, Pipe *a1_pipe, const QString &title = QString());
        bool setPipe(int index, const a2::PipeVectors &a2_pipe, const QString &title = QString());

        void popPipe();

        int addEmptyPipe(const QString &title = QString()); // creates a fake entry
};

/** Display of expression (exprtk) interal errors and analysis specific
 * semantic errors in a table widget. */
class ExpressionErrorWidget: public QWidget
{
    Q_OBJECT
    public:
        ExpressionErrorWidget(QWidget *parent = nullptr);

    private:
        QTableWidget *m_errorTable;
};

/** Specialized editor widget for exprtk expressions. */
class ExpressionTextEditor: public QWidget
{
    Q_OBJECT
    public:
        ExpressionTextEditor(QWidget *parent = nullptr);

        QPlainTextEdit *textEdit() { return m_textEdit; }

    private:
        QPlainTextEdit *m_textEdit;
};

/** Combines ExpressionTextEditor and ExpressionErrorWidget. */
class ExpressionEditorWidget: public QWidget
{
    Q_OBJECT
    public:
        ExpressionEditorWidget(QWidget *parent = nullptr);

        void setText(const QString &);
        QString text() const;

    private:
        ExpressionTextEditor *m_exprEdit;
        ExpressionErrorWidget *m_exprErrors;
};

/** Complete editor component for one of the subexpressions of the
 * ExpressionOperator.
 * From left to right:
 * - input pipes toolbox
 * - editor with clickable error display and an "exec/eval" button.
 * - output pipes toolbox
 */
class ExpressionOperatorEditorComponent: public QWidget
{
    Q_OBJECT
    public:
        ExpressionOperatorEditorComponent(QWidget *parent = nullptr);

        void setExpressionText(const QString &text);
        QString expressionText() const;

    private:
        ExpressionOperatorPipesToolBox *m_toolBox_inputPipes;
        ExpressionOperatorPipesToolBox *m_toolBox_outputPipes;
        ExpressionEditorWidget *m_editorWidget;
        QPushButton *m_evalButton;
};

} // end namespace analysis

#endif /* __EXPRESSION_OPERATOR_DIALOG_P_H__ */
