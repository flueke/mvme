#ifndef __TEST_ANALYSIS_AGGREGATE_OPS_H__
#define __TEST_ANALYSIS_AGGREGATE_OPS_H__

#include <QtTest/QtTest>

class TestAggregateOps: public QObject
{
    Q_OBJECT
    public:
        virtual ~TestAggregateOps() {}

    private slots:
        void test_sum();
        void test_mean();
        void test_sigma();
        void test_min();
        void test_max();
        void test_multiplicity();
};

#endif /* __TEST_ANALYSIS_AGGREGATE_OPS_H__ */
