#include "test_analysis_aggregate_ops.h"

#include "analysis/analysis.h"

using namespace analysis;

static ParameterVector make_parameter_vector(const QVector<double> &values, const QPair<double, double> limits)
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

void TestAggregateOps::test_sum()
{
    const QVector<double> values = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0 };
    const QPair<double, double> limits(qMakePair(0.0, 10.0));

    Pipe inputPipe;
    inputPipe.parameters = make_parameter_vector(values, limits);

    AggregateOps op;
    op.setOperation(AggregateOps::Op_Sum);

    Slot *inputSlot = op.getSlot(0);
    inputSlot->connectPipe(&inputPipe, Slot::NoParamIndex);

    op.beginRun({});

    Pipe *output = op.getOutput(0);
    QCOMPARE(output->getSize(), 1);

    op.step();
    auto p = output->parameters[0];
    QVERIFY(p.valid);
    QCOMPARE(p.lowerLimit, limits.first);
    QCOMPARE(p.upperLimit, 100.0); // 10 times max value of 10.0
    QCOMPARE(p.value, std::accumulate(values.begin(), values.end(), 0.0));
}

void TestAggregateOps::test_mean()
{
    const QVector<double> values = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0 };
    const QPair<double, double> limits(qMakePair(0.0, 10.0));

    Pipe inputPipe;
    inputPipe.parameters = make_parameter_vector(values, limits);

    AggregateOps op;
    op.setOperation(AggregateOps::Op_Mean);

    Slot *inputSlot = op.getSlot(0);
    inputSlot->connectPipe(&inputPipe, Slot::NoParamIndex);

    op.beginRun({});

    Pipe *output = op.getOutput(0);
    QCOMPARE(output->getSize(), 1);

    op.step();
    auto p = output->parameters[0];
    QVERIFY(p.valid);
    QCOMPARE(p.lowerLimit, limits.first);
    QCOMPARE(p.upperLimit, limits.second);
    QCOMPARE(p.value, std::accumulate(values.begin(), values.end(), 0.0) / values.size());
}

void TestAggregateOps::test_sigma()
{
    const QVector<double> values = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0 };
    const QPair<double, double> limits(qMakePair(0.0, 10.0));
    const double sigma = 2.872281323269;

    Pipe inputPipe;
    inputPipe.parameters = make_parameter_vector(values, limits);

    AggregateOps op;
    op.setOperation(AggregateOps::Op_Sigma);

    Slot *inputSlot = op.getSlot(0);
    inputSlot->connectPipe(&inputPipe, Slot::NoParamIndex);

    op.beginRun({});

    Pipe *output = op.getOutput(0);
    QCOMPARE(output->getSize(), 1);

    op.step();
    auto p = output->parameters[0];
    QVERIFY(p.valid);
    QCOMPARE(p.lowerLimit, 0.0);
    QCOMPARE(p.upperLimit, std::sqrt(10.0));
    QCOMPARE(p.value, sigma);
}

void TestAggregateOps::test_min()
{
    const QVector<double> values = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0 };
    const QPair<double, double> limits(qMakePair(0.0, 10.0));

    Pipe inputPipe;
    inputPipe.parameters = make_parameter_vector(values, limits);

    AggregateOps op;
    op.setOperation(AggregateOps::Op_Min);

    Slot *inputSlot = op.getSlot(0);
    inputSlot->connectPipe(&inputPipe, Slot::NoParamIndex);

    op.beginRun({});

    Pipe *output = op.getOutput(0);
    QCOMPARE(output->getSize(), 1);

    op.step();
    auto p = output->parameters[0];
    QVERIFY(p.valid);
    QCOMPARE(p.lowerLimit, 0.0);
    QCOMPARE(p.upperLimit, 10.0);
    QCOMPARE(p.value, 1.0);
}

void TestAggregateOps::test_max()
{
    const QVector<double> values = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0 };
    const QPair<double, double> limits(qMakePair(0.0, 10.0));

    Pipe inputPipe;
    inputPipe.parameters = make_parameter_vector(values, limits);

    AggregateOps op;
    op.setOperation(AggregateOps::Op_Max);

    Slot *inputSlot = op.getSlot(0);
    inputSlot->connectPipe(&inputPipe, Slot::NoParamIndex);

    op.beginRun({});

    Pipe *output = op.getOutput(0);
    QCOMPARE(output->getSize(), 1);

    op.step();
    auto p = output->parameters[0];
    QVERIFY(p.valid);
    QCOMPARE(p.lowerLimit, 0.0);
    QCOMPARE(p.upperLimit, 10.0);
    QCOMPARE(p.value, 10.0);
}

void TestAggregateOps::test_multiplicity()
{
    const QVector<double> values = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0 };
    const QPair<double, double> limits(qMakePair(0.0, 10.0));

    Pipe inputPipe;
    inputPipe.parameters = make_parameter_vector(values, limits);

    AggregateOps op;
    op.setOperation(AggregateOps::Op_Multiplicity);

    Slot *inputSlot = op.getSlot(0);
    inputSlot->connectPipe(&inputPipe, Slot::NoParamIndex);

    op.beginRun({});

    Pipe *output = op.getOutput(0);
    QCOMPARE(output->getSize(), 1);

    op.step();
    auto p = output->parameters[0];
    QVERIFY(p.valid);
    QCOMPARE(p.lowerLimit, 0.0);
    QCOMPARE(p.upperLimit, 10.0);
    QCOMPARE(p.value, 10.0);
}

QTEST_MAIN(TestAggregateOps)
