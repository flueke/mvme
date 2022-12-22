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
#include "gtest/gtest.h"
#include "analysis.h"
#include "object_visitor.h"

using namespace analysis;

class CountingVisitor: public ObjectVisitor
{
    public:
        virtual void visit(SourceInterface *source) override
        {
            Q_UNUSED(source);
            nSources++;
        }

        virtual void visit(OperatorInterface *op) override
        {
            Q_UNUSED(op);
            nOperators++;
        }

        virtual void visit(ConditionInterface *cond) override
        {
            Q_UNUSED(cond);
            nConditions++;
        }

        virtual void visit(SinkInterface *sink) override
        {
            Q_UNUSED(sink);
            nSinks++;
        }

        virtual void visit(Directory *dir) override
        {
            Q_UNUSED(dir);
            nDirs++;
        }

        void visit(PlotGridView *view) override
        {
            Q_UNUSED(view);
            nObjects++;
        }

        int nSources = 0,
            nOperators = 0,
            nConditions = 0,
            nSinks = 0,
            nDirs = 0,
            nObjects = 0;
};

TEST(objectVisitor, BasicVisitation)
{
    AnalysisObjectVector vec;

    vec.push_back(std::make_shared<Extractor>());
    vec.push_back(std::make_shared<ListFilterExtractor>());

    vec.push_back(std::make_shared<CalibrationMinMax>());
    vec.push_back(std::make_shared<PreviousValue>());

    vec.push_back(std::make_shared<Histo1DSink>());
    vec.push_back(std::make_shared<RateMonitorSink>());

    vec.push_back(std::make_shared<Directory>());

    CountingVisitor cv;

    visit_objects(vec.begin(), vec.end(), cv);

    ASSERT_EQ(cv.nSources, 2);
    ASSERT_EQ(cv.nOperators, 2);
    ASSERT_EQ(cv.nConditions, 0);
    ASSERT_EQ(cv.nSinks, 2);
    ASSERT_EQ(cv.nDirs, 1);
}
