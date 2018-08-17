#include <benchmark/benchmark.h>
#include <iostream>

/* list of polygons. stored as strings and read via boost WKT function
 * -> list of boost::geometry::model::polygon<Point>;
 * for each polygon: list of (point, expected result of within test)
 */

struct PointAndResult_Input
{
    std::string point;
    bool result;
};

struct BenchmarkData_Input
{
    std::string polygon;
    std::vector<PointAndResult_Input> pointsAndResults;
};

static const std::vector<BenchmarkData_Input> Benchmarks =
{
    {
        "POLYGON((20.0669 73.7991,71.7949 78.6026,77.4805 18.9229,19.398 20.0873,20.0669 73.7991))" ,
        {
            { "POINT(33.4448 82.2416)", false },
            { "POINT(10.8138 46.2882)", false },
            { "POINT(31.6611 7.56914)", false },
            { "POINT(82.1628 50.0728)", false },
            { "POINT(28.874 66.23)", true },
            { "POINT(27.7592 26.6376)", true },
            { "POINT(65.4404 24.163)", true },
            { "POINT(63.8796 64.6288)", true },
        },
    },

#if 0
    {
        "POLYGON((34.1137 94.032,11.8172 88.064,8.13824 54.0029,69.4537 55.8952,81.2709 83.6972,68.5619 88.6463,56.2988 77.4381,52.6198 91.8486,44.0357 82.3872,28.6511 79.0393,38.796 88.5007,46.4883 93.0131,31.7726 85.8806,23.7458 77.7293,57.1906 58.0786,59.8662 83.8428,68.4504 83.9884,76.1427 70.7424,71.2375 40.4658,54.2921 28.2387,22.408 29.4032,8.24972 41.7758,4.68227 59.2431,1.56076 66.9578,8.24972 69.869,15.3846 66.5211,17.5028 75.6914,10.7023 82.5328,10.3679 91.2664,17.9487 93.0131,23.4114 92.1397,28.3166 83.5517,32.553 92.4309,38.2386 97.2344,51.2821 96.0699,57.971 95.3421,51.9509 86.3173,40.3567 78.0204,33.1104 91.8486,30.1003 96.9432,23.4114 96.7977,34.1137 94.032))",
        {
            { "point", true },
            { "point2", false },
        }
    },
#endif
};

static void BM_poly_within_test(benchmark::State &state)
{
    fprintf(stderr, "%s: %lu benchmarks\n", __PRETTY_FUNCTION__, Benchmarks.size());

    for (const auto &bench: Benchmarks)
    {
        // TODO: build polygons and point vectors
        // run through vectors doing within() tests and checking results
        // put out timing information for each polygon
        // this could mean structuring the data and how the test is invoked differently.
    }

    while (state.KeepRunning())
    {
    }
}
BENCHMARK(BM_poly_within_test);

BENCHMARK_MAIN();
