#include "test_analysis_aggregate_ops.h"

#include "analysis/analysis.h"

using namespace analysis;
using DVec  = QVector<double>;
using DPair = QPair<double, double>;

Q_DECLARE_METATYPE(AggregateOps::Operation);

static ParameterVector make_parameter_vector(const DVec &values, DPair limits)
{
    ParameterVector result;

    for (double v: values)
    {
        Parameter p;
        p.value = v;
        p.valid = !std::isnan(v);
        p.lowerLimit = limits.first;
        p.upperLimit = limits.second;

        result.push_back(p);
    }

    return result;
}

void TestAggregateOps::test_all_ops_data()
{
    QTest::addColumn<AggregateOps::Operation>("in_operation");
    QTest::addColumn<DVec>("in_values");
    QTest::addColumn<DPair>("in_limits");
    QTest::addColumn<bool>("out_valid");
    QTest::addColumn<DPair>("out_limits");
    QTest::addColumn<double>("out_value");

    DVec values { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0 };

    QTest::newRow("sum: 1..10 no thresholds")
        << AggregateOps::Op_Sum
        << values
        << qMakePair(0.0, 10.0)
        << true
        << qMakePair(0.0, 100.0)  // 10 times max value of 10.0
        << std::accumulate(values.begin(), values.end(), 0.0)
    ;

    QTest::newRow("mean: 1..10 no thresholds")
        << AggregateOps::Op_Mean
        << values
        << qMakePair(0.0, 10.0)
        << true
        << qMakePair(0.0, 10.0)
        << std::accumulate(values.begin(), values.end(), 0.0) / values.size()
    ;

    QTest::newRow("sigma: 1..10 no thresholds")
        << AggregateOps::Op_Sigma
        << values
        << qMakePair(0.0, 10.0)
        << true
        << qMakePair(0.0, std::sqrt(10.0))
        << 2.872281323269
    ;

    QTest::newRow("min: 1..10 no thresholds")
        << AggregateOps::Op_Min
        << values
        << qMakePair(0.0, 10.0)
        << true
        << qMakePair(0.0, 10.0)
        << 1.0
    ;

    QTest::newRow("max: 1..10 no thresholds")
        << AggregateOps::Op_Max
        << values
        << qMakePair(0.0, 10.0)
        << true
        << qMakePair(0.0, 10.0)
        << 10.0
    ;

    QTest::newRow("mult: 1..10 no thresholds")
        << AggregateOps::Op_Multiplicity
        << values
        << qMakePair(0.0, 10.0)
        << true
        << qMakePair(0.0, 10.0)
        << 10.0
    ;

    // minx
    values = { 1.0, 2.0, 0.0, 2.0, 1.0 };

    QTest::newRow("minx: no thresholds")
        << AggregateOps::Op_MinX
        << values
        << qMakePair(0.0, 10.0)
        << true
        << qMakePair(0.0, static_cast<double>(values.size() - 1))
        << 2.0
    ;

    // maxx
    values = { 1.0, 2.0, 8.0, 2.0, 1.0 };

    QTest::newRow("maxx: no thresholds")
        << AggregateOps::Op_MaxX
        << values
        << qMakePair(0.0, 10.0)
        << true
        << qMakePair(0.0, static_cast<double>(values.size() - 1))
        << 2.0
    ;

    // meanx
    values = { 1.0, 2.0, 3.0, 2.0, 1.0 };

    QTest::newRow("meanx 1: no thresholds")
        << AggregateOps::Op_MeanX
        << values
        << qMakePair(0.0, 10.0)
        << true
        << qMakePair(0.0, static_cast<double>(values.size() - 1))
        << 2.0
    ;

    // meanx
    values = { 1.0, 2.0, 3.0, 5.0, 2.0, 1.0 };

    QTest::newRow("meanx 2: no thresholds")
        << AggregateOps::Op_MeanX
        << values
        << qMakePair(0.0, 10.0)
        << true
        << qMakePair(0.0, static_cast<double>(values.size() - 1))
        << 2.5714285714285716
    ;

    // TODO: add sigmaX test
}

void TestAggregateOps::test_all_ops()
{
    QFETCH(AggregateOps::Operation, in_operation);
    QFETCH(DVec, in_values);
    QFETCH(DPair, in_limits);

    Pipe inputPipe;
    inputPipe.parameters = make_parameter_vector(in_values, in_limits);

    AggregateOps op;
    op.setOperation(in_operation);
    op.getSlot(0)->connectPipe(&inputPipe, Slot::NoParamIndex);

    op.beginRun({});

    Pipe *output = op.getOutput(0);
    QCOMPARE(output->getSize(), 1);

    op.step();

    auto p = output->parameters[0];
    QTEST(p.valid, "out_valid");
    QTEST(qMakePair(p.lowerLimit, p.upperLimit), "out_limits");
    QTEST(p.value, "out_value");
}

QTEST_MAIN(TestAggregateOps)
