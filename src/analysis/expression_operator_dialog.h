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
#ifndef __EXPRESSION_OPERATOR_DIALOG_H__
#define __EXPRESSION_OPERATOR_DIALOG_H__

#include <memory>
#include <QDialog>
#include "analysis_ui_p.h"
#include "object_editor_dialog.h"

class MVMEContext;

namespace analysis
{

class ExpressionOperator;

namespace ui
{

class EventWidget;

class ExpressionOperatorDialog: public ObjectEditorDialog
{
    Q_OBJECT
    public:
        ExpressionOperatorDialog(const std::shared_ptr<ExpressionOperator> &op,
                                 int userLevel,
                                 ObjectEditorMode mode,
                                 const DirectoryPtr &destDir,
                                 EventWidget *eventWidget);

        virtual ~ExpressionOperatorDialog();

    public slots:
        void apply();
        virtual void accept() override;
        virtual void reject() override;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

class ExpressionConditionDialog: public ObjectEditorDialog
{
    Q_OBJECT
    public:
        ExpressionConditionDialog(const std::shared_ptr<ExpressionCondition> &op,
                                 int userLevel,
                                 ObjectEditorMode mode,
                                 const DirectoryPtr &destDir,
                                 EventWidget *eventWidget);

        virtual ~ExpressionConditionDialog();

    public slots:
        void apply();
        virtual void accept() override;
        virtual void reject() override;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};


} // end namespace ui
} // end namespace analysis

#endif /* __EXPRESSION_OPERATOR_DIALOG_H__ */
