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
#include <memory>
#include "gtest/gtest.h"
#include "analysis/analysis.h"
#include "analysis/analysis_session.h"
#include "analysis/analysis_session_p.h"

using namespace analysis;

namespace
{

std::unique_ptr<Analysis> make_test_analysis()
{
    auto result = std::make_unique<Analysis>();

    // data source, 4 address and 4 data bits
    QVector<DataFilter> filters = { DataFilter("AAAADDDD") };
    auto ds = std::make_shared<Extractor>();
    ds->setFilter(MultiWordDataFilter(filters));
    result->addSource(std::dynamic_pointer_cast<SourceInterface>(ds));

    // h1dsink, using the full data source output
    auto h1dsink = std::make_shared<Histo1DSink>();
    h1dsink->connectArrayToInputSlot(0, ds->getOutput(0));
    result->addOperator(std::dynamic_pointer_cast<OperatorInterface>(h1dsink));

    // h2dsink, accumulating ds[0] vs ds[1]
    auto h2dsink = std::make_shared<Histo2DSink>();
    h2dsink->connectInputSlot(0, ds->getOutput(0), 0);
    h2dsink->connectInputSlot(0, ds->getOutput(0), 1);
    result->addOperator(std::dynamic_pointer_cast<OperatorInterface>(h2dsink));

    // rate monitor sink
    auto rms = std::make_shared<RateMonitorSink>();
    rms->connectArrayToInputSlot(0, ds->getOutput(0));
    result->addOperator(std::dynamic_pointer_cast<OperatorInterface>(rms));

    // TODO: call beginRun() on the analysis. needs a vme mapping (afaik used for
    // operator grouping only)
    // check that all operators are ok and that the analysis is working
    //

    return result;
}

}

TEST(AnalysisSession, SaveLoadH1D)
{
    auto analysis = make_test_analysis();
}
