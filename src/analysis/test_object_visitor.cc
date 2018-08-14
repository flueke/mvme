#include "gtest/gtest.h"
#include "analysis.h"
#include "object_visitor.h"

using namespace analysis;

class CountingVisitor: public ObjectVisitor
{
    public:
        virtual void visit(SourceInterface *source) override
        {
            nSources++;
        }

        virtual void visit(OperatorInterface *op) override
        {
            nOperators++;
        }

        virtual void visit(SinkInterface *sink) override
        {
            nSinks++;
        }

        virtual void visit(Directory *dir) override
        {
            nDirs++;
        }

        int nSources = 0,
            nOperators = 0,
            nSinks = 0,
            nDirs = 0;
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
    ASSERT_EQ(cv.nSinks, 2);
    ASSERT_EQ(cv.nDirs, 1);
}
