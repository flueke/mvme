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
#ifndef __MVME_CONDITION_UI_H__
#define __MVME_CONDITION_UI_H__

#include <memory>
#include <QWidget>

#include "analysis_service_provider.h"

#if 0

namespace analysis
{
namespace ui
{

class ConditionWidget: public QWidget
{
    Q_OBJECT
    signals:
        void conditionLinkSelected(const ConditionLink &cl);
        void applyConditionAccept();
        void applyConditionReject();
        void editCondition(const ConditionLink &cond);
        void objectSelected(const AnalysisObjectPtr &obj);

    public:
        ConditionWidget(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        virtual ~ConditionWidget() override;

    public slots:
        void repopulate();
        void repopulate(int eventIndex);
        void repopulate(const QUuid &eventId);
        void doPeriodicUpdate();

        void selectEvent(int eventIndex);
        void selectEventById(const QUuid &eventId);
        void clearTreeSelections();
        void clearTreeHighlights();

        void highlightConditionLink(const ConditionLink &cl);
        void setModificationButtonsVisible(const ConditionLink &cl, bool visible);


    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // ns ui
} // ns analysis

#endif

#endif /* __MVME_CONDITION_UI_H__ */
