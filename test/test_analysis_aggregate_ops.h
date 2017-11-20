#ifndef __TEST_ANALYSIS_AGGREGATE_OPS_H__
#define __TEST_ANALYSIS_AGGREGATE_OPS_H__

#include <QtTest/QtTest>

class TestAggregateOps: public QObject
{
    Q_OBJECT
    public:
        virtual ~TestAggregateOps() {}

    private slots:
        void test_all_ops_data();
        void test_all_ops();
};

#endif /* __TEST_ANALYSIS_AGGREGATE_OPS_H__ */
