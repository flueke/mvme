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
#ifndef __MVME_ANALYSIS_CONDITION_EDITOR_INTERFACE_H__
#define __MVME_ANALYSIS_CONDITION_EDITOR_INTERFACE_H__

#include <QtPlugin>
#include "analysis/condition_link.h"
#include "libmvme_export.h"

namespace analysis
{

class LIBMVME_EXPORT ConditionEditorInterface
{
    public:
        virtual ~ConditionEditorInterface() {}

        /* Sets the condition to be edited.
         * Must return false if the condition can't be edited in this editor
         * instance, true otherwise.
         * In case the condition can't be edited getCondition() may return an
         * invalid ConditionLink instead of the one passed here. */
        virtual bool setEditCondition(const ConditionPtr &cond) = 0;

        /* Returns the condition and subindex currently being edited.
         *
         * Note that the returned subindex may be different than the one
         * originally passed in when editCondition() was called. This is
         * because the editor can offer to edit all subindexes in one
         * graphical widget, e.g. Histo1DWidget.
         */
        virtual ConditionPtr getEditCondition() const = 0;

        /* Starts editing the condition that was previously set via
         * setEditCondition. */
        virtual void beginEditCondition() = 0;
};

}

#define ConditionEditorInterface_iid "com.mesytec.mvme.analysis.ConditionEditorInterface.1"
Q_DECLARE_INTERFACE(analysis::ConditionEditorInterface, ConditionEditorInterface_iid)

#endif /* __MVME_ANALYSIS_CONDITION_EDITOR_INTERFACE_H__ */
