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
#ifndef __CONDITION_UI_P_H__
#define __CONDITION_UI_P_H__

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>

#include "analysis.h"
#include "analysis_service_provider.h"

class MVMEContext;

namespace analysis
{
namespace ui
{

class ConditionTreeWidget: public QTreeWidget
{
    Q_OBJECT
    signals:
        void applyConditionAccept();
        void applyConditionReject();
        void editConditionGraphically(const ConditionLink &cond);
        void editConditionInEditor(const ConditionLink &cond);

    public:
        ConditionTreeWidget(AnalysisServiceProvider *asp, const QUuid &eventId, int eventIndex,
                            QWidget *parent = nullptr);
        virtual ~ConditionTreeWidget() override;

        void repopulate();
        void doPeriodicUpdate();

        void highlightConditionLink(const ConditionLink &cl);
        void clearHighlights();
        void setModificationButtonsVisible(const ConditionLink &cl, bool visible);

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

class NodeModificationButtons: public QWidget
{
    Q_OBJECT
    signals:
        void accept();
        void reject();

    public:
        explicit NodeModificationButtons(QWidget *parent = nullptr);

        QPushButton *getAcceptButton() const { return pb_accept; }
        QPushButton *getRejectButton() const { return pb_reject; }

        void setButtonsVisible(bool visible)
        {
            pb_accept->setVisible(visible);
            pb_reject->setVisible(visible);
        }

    private:
        QPushButton *pb_accept;
        QPushButton *pb_reject;
};

class ConditionIntervalEditor: public QDialog
{
    Q_OBJECT
    signals:
        void applied();

    public:
        ConditionIntervalEditor(ConditionInterval *cond, AnalysisServiceProvider *asp,
                                QWidget *parent = nullptr);
        virtual ~ConditionIntervalEditor();

        virtual void accept();
        virtual void reject();

    private:
        ConditionInterval *m_cond;
        AnalysisServiceProvider *m_asp;

        QLineEdit *le_name;
        QTableWidget *m_table = nullptr;
        QDialogButtonBox *m_bb = nullptr;
};

} // end namespace ui
} // end namespace analysis

#endif /* __CONDITION_UI_P_H__ */
