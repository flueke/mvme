#include <gtest/gtest.h>
#include <memory>

#include "analysis.h"

TEST(MultiHitExtractorTest, ArrayPerHitShape)
{
    using namespace analysis;

    MultiHitExtractor mex;
    // 4 address bits -> 16 addresses
    // 5 data bits -> data upper bound is 32
    // maximum of 3 hits per address
    mex.setObjectName("channel_time");
    mex.setFilter(make_filter("AAAADDDDD"));
    mex.setMaxHits(3);
    mex.setShape(MultiHitExtractor::Shape::ArrayPerHit);

    mex.beginRun({});

    ASSERT_EQ(mex.getNumberOfOutputs(), 3+1);
    ASSERT_EQ(mex.getOutputName(0), "channel_time_hit0");
    ASSERT_EQ(mex.getOutputName(1), "channel_time_hit1");
    ASSERT_EQ(mex.getOutputName(2), "channel_time_hit2");
    ASSERT_EQ(mex.getOutputName(3), "hitCounts");

    for (int i=0; i<mex.getNumberOfOutputs()-1; ++i)
    {
        auto output = mex.getOutput(i);
        ASSERT_EQ(output->getSize(), 16);

        for (const auto &param: output->getParameters())
        {
            ASSERT_EQ(param.lowerLimit, 0.0);
            ASSERT_EQ(param.upperLimit, 32.0);
        }
    }

    {
        auto clone = mex.clone();
        auto mex2 = qobject_cast<MultiHitExtractor *>(clone.get());
        ASSERT_EQ(mex.getFilter(), mex2->getFilter());
        ASSERT_EQ(mex.getMaxHits(), mex2->getMaxHits());
        ASSERT_EQ(mex.getShape(), mex2->getShape());
        ASSERT_EQ(mex.getOptions(), mex2->getOptions());
    }
}

TEST(MultiHitExtractorTest, ArrayPerAddress)
{
    using namespace analysis;

    MultiHitExtractor mex;
    // 4 address bits -> 16 addresses
    // 5 data bits -> data upper bound is 32
    // maximum of 3 hits per address
    mex.setObjectName("channel_time");
    mex.setFilter(make_filter("AAAADDDDD"));
    mex.setMaxHits(3);
    mex.setShape(MultiHitExtractor::Shape::ArrayPerAddress);

    mex.beginRun({});

    ASSERT_EQ(mex.getNumberOfOutputs(), 16+1);

    for (int i=0; i<mex.getNumberOfOutputs()-1; ++i)
    {
        auto output = mex.getOutput(i);

        ASSERT_EQ(output->getSize(), 3);

        for (const auto &param: output->getParameters())
        {
            ASSERT_EQ(param.lowerLimit, 0.0);
            ASSERT_EQ(param.upperLimit, 32.0);
        }

        ASSERT_EQ(mex.getOutputName(i), QSL("channel_time_%1_hits").arg(i));
    }

    ASSERT_EQ(mex.getOutputName(16), "hitCounts");

    {
        auto clone = mex.clone();
        auto mex2 = qobject_cast<MultiHitExtractor *>(clone.get());
        ASSERT_EQ(mex.getFilter(), mex2->getFilter());
        ASSERT_EQ(mex.getMaxHits(), mex2->getMaxHits());
        ASSERT_EQ(mex.getShape(), mex2->getShape());
        ASSERT_EQ(mex.getOptions(), mex2->getOptions());
    }
}
