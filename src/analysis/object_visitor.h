/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVME_ANALYSIS_OBJECT_VISITOR_H__
#define __MVME_ANALYSIS_OBJECT_VISITOR_H__

#include "analysis_fwd.h"
#include "libmvme_export.h"

namespace analysis
{

class LIBMVME_EXPORT ObjectVisitor
{
    public:
        virtual void visit(SourceInterface *source) = 0;
        virtual void visit(OperatorInterface *op) = 0;
        virtual void visit(SinkInterface *sink) = 0;
        virtual void visit(ConditionInterface *cond) = 0;
        virtual void visit(Directory *dir) = 0;
        virtual void visit(PlotGridView *view) = 0;
};

template<typename It>
void visit_objects(It begin_, It end_, ObjectVisitor &visitor)
{
    while (begin_ != end_)
    {
        (*begin_++)->accept(visitor);
    }
}

} // end namespace analysis

#endif /* __MVME_ANALYSIS_OBJECT_VISITOR_H__ */
