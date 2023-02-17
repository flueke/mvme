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
#include <benchmark/benchmark.h>
#include <iostream>

/* Circumvent compile errors related to the 'Q' numeric literal suffix.
 * See https://svn.boost.org/trac10/ticket/9240 and
 * https://www.boost.org/doc/libs/1_68_0/libs/math/doc/html/math_toolkit/config_macros.html
 * for details. */
#define BOOST_MATH_DISABLE_FLOAT128
#define BOOST_ALLOW_DEPRECATED_HEADERS
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#undef BOOST_ALLOW_DEPRECATED_HEADERS

namespace bg = boost::geometry;
using Point   = bg::model::d2::point_xy<double>;
// template parameters: point type, clockwise, closed
using Polygon = bg::model::polygon<Point, true, true>;

using benchmark::Counter;

/* list of polygons. stored as strings and read via boost WKT function
 * -> list of boost::geometry::model::polygon<Point>;
 * for each polygon: list of (point, expected result of within test)
 *
 * Test data has been created using the ./dev_histo2d_polygon_cuts program.
 */

struct InputPointAndResult
{
    std::string point;
    bool result;
};

struct InputBenchmarkData
{
    std::string polygon;
    std::vector<InputPointAndResult> pointsAndResults;
};

static const std::vector<InputBenchmarkData> Benchmarks =
{
    // [0], single line
    {
        "POLYGON((58.6399 74.0902,29.6544 33.1878,58.6399 74.0902))" ,
        {
            { "POINT(55.1839 72.6346)", false },
            { "POINT(56.6332 71.179)", false },
            { "POINT(64.1026 58.8064)", false },
            { "POINT(43.2553 51.9651)", false },
            { "POINT(42.9208 51.9651)", false },
            { "POINT(42.029 50.8006)", false },
            { "POINT(38.796 44.5415)", false },
            { "POINT(35.2285 29.4032)", false },
        },
    },

    // [1], 4 edges, non-rectangular
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

    // [2], triangle
    {
        "POLYGON((55.9643 82.8239,78.8183 32.1689,21.1817 34.9345,55.9643 82.8239))" ,
        {
            { "POINT(44.9275 83.2606)", false },
            { "POINT(10.0334 26.492)", false },
            { "POINT(59.6433 17.0306)", false },
            { "POINT(76.7001 75.2547)", false },
            { "POINT(55.4069 79.476)", true },
            { "POINT(29.7659 37.7001)", true },
            { "POINT(71.2375 36.6812)", true },
            { "POINT(54.2921 50.2183)", true },
        },
    },

    // [3], 6 edges
    {
        "POLYGON((42.5864 85.8806,59.7547 69.869,58.8629 46.7249,41.0256 30.8588,25.0836 45.2693,25.3066 71.9068,42.5864 85.8806))" ,
        {
            { "POINT(35.786 88.5007)", false },
            { "POINT(19.0635 57.2052)", false },
            { "POINT(33.7793 26.0553)", false },
            { "POINT(55.9643 38.5735)", false },
            { "POINT(42.6979 79.3304)", true },
            { "POINT(28.6511 66.23)", true },
            { "POINT(36.0089 36.9723)", true },
            { "POINT(57.5251 68.2678)", true },
            { "POINT(42.8094 61.1354)", true },
            { "POINT(59.9777 83.5517)", false },
        },
    },

    // [4], 8 edges
    {
        "POLYGON((34.1137 85.7351,56.1873 93.1587,71.0145 80.0582,79.2642 54.8763,70.903 27.2198,38.573 20.2329,20.2899 36.6812,19.8439 63.6099,34.1137 85.7351))" ,
        {
            { "POINT(42.029 92.722)", false },
            { "POINT(20.1784 73.6536)", false },
            { "POINT(11.4827 46.2882)", false },
            { "POINT(20.6243 17.9039)", false },
            { "POINT(43.7012 4.51237)", false },
            { "POINT(80.9365 22.853)", false },
            { "POINT(88.6288 51.3828)", false },
            { "POINT(80.602 83.2606)", false },
            { "POINT(35.8974 82.6783)", true },
            { "POINT(28.9855 67.9767)", true },
            { "POINT(24.8606 56.3319)", true },
            { "POINT(32.2185 31.1499)", true },
            { "POINT(50.2787 48.1805)", true },
            { "POINT(69.5652 30.131)", true },
            { "POINT(68.6734 70.4512)", true },
        },
    },

    // [5], 6 edges, concave
    {
        "POLYGON((27.2018 78.1659,39.4649 50.0728,58.7514 75.5459,58.6399 42.2125,37.5697 26.2009,17.2798 44.5415,27.2018 78.1659))" ,
        {
            { "POINT(24.6377 75.4003)", false },
            { "POINT(13.4894 36.6812)", false },
            { "POINT(33.3333 18.7773)", false },
            { "POINT(58.5284 32.6055)", false },
            { "POINT(62.7648 75.1092)", false },
            { "POINT(49.0524 68.2678)", false },
            { "POINT(39.6878 52.6929)", false },
            { "POINT(34.8941 68.2678)", false },
            { "POINT(29.5429 64.0466)", true },
            { "POINT(19.621 45.5604)", true },
            { "POINT(33.8907 32.0233)", true },
            { "POINT(38.3501 28.3843)", true },
            { "POINT(43.5897 49.4905)", true },
            { "POINT(55.4069 44.1048)", true },
            { "POINT(56.1873 64.9199)", true },
        },
    },

    // [6], ugly star with 7 outward spikes, concave
    {
        "POLYGON((29.097 87.6274,41.6945 60.5531,44.2586 91.8486,47.7146 62.2999,66.4437 87.6274,51.6165 58.952,81.0479 44.2504,50.1672 47.4527,52.0624 12.0815,40.5797 46.2882,17.1683 19.5051,36.0089 49.1994,8.47269 52.984,35.0056 56.3319,29.097 87.6274))" ,
        {
            { "POINT(41.0256 66.0844)", false },
            { "POINT(32.2185 57.3508)", false },
            { "POINT(31.1037 47.1616)", false },
            { "POINT(37.9041 33.0422)", false },
            { "POINT(54.961 40.4658)", false },
            { "POINT(58.5284 59.0975)", false },
            { "POINT(50.7246 72.7802)", false },
            { "POINT(12.8205 94.6143)", false },
            { "POINT(3.67893 29.6943)", false },
            { "POINT(26.087 15.2838)", false },
            { "POINT(61.427 19.214)", false },
            { "POINT(76.2542 56.7686)", false },
            { "POINT(44.7046 81.6594)", true },
            { "POINT(32.2185 77.147)", true },
            { "POINT(25.3066 52.4017)", true },
            { "POINT(31.2152 38.2824)", true },
            { "POINT(49.6098 29.8399)", true },
            { "POINT(64.3255 49.7817)", true },
            { "POINT(57.4136 72.9258)", true },
            { "POINT(44.4816 81.5138)", true },
        }
    },

    // [7], round, convex
    {
        "POLYGON((45.039 86.754,47.7146 85.5895,48.272 79.7671,46.4883 71.6157,43.5897 64.9199,38.2386 60.4076,32.33 58.5153,26.087 59.8253,21.7391 65.3566,19.621 72.6346,21.5162 78.8937,25.0836 83.9884,30.8807 88.3552,38.3501 89.5197,45.039 86.754))" ,
        {
            { "POINT(34.2252 87.4818)", true },
            { "POINT(42.6979 86.0262)", true },
            { "POINT(46.3768 80.786)", true },
            { "POINT(42.6979 67.3945)", true },
            { "POINT(35.2285 61.4265)", true },
            { "POINT(23.5229 65.6477)", true },
            { "POINT(20.7358 85.7351)", false },
            { "POINT(36.9008 94.6143)", false },
            { "POINT(65.3289 73.3624)", false },
            { "POINT(60.7581 42.9403)", false },
            { "POINT(24.4147 44.5415)", false },
            { "POINT(10.7023 35.9534)", false },
            { "POINT(21.0702 64.9199)", false },

        },
    },

    // [8], bowtie, intersects
    {
        "POLYGON((18.5061 75.2547,17.6143 55.1674,66.6667 80.2038,65.8863 54.294,18.5061 75.2547))" ,
        {
            { "POINT(24.1918 77.147)", false },
            { "POINT(12.4861 71.4702)", false },
            { "POINT(15.2731 46.8705)", false },
            { "POINT(27.3133 49.1994)", false },
            { "POINT(39.6878 60.4076)", false },
            { "POINT(38.9075 71.7613)", false },
            { "POINT(73.0212 77.2926)", false },
            { "POINT(72.5753 50.5095)", false },
            { "POINT(61.7614 74.0902)", true },
            { "POINT(63.2107 60.5531)", true },
            { "POINT(46.7113 65.6477)", true },
            { "POINT(20.1784 69.7234)", true },
            { "POINT(19.2865 61.5721)", true },
            { "POINT(29.7659 65.0655)", true },
        }
    },

    // [9], uncomfortable designer chair
    {
        "POLYGON((12.932 91.1208,13.8239 97.9622,19.2865 97.671,22.7425 91.5575,20.9588 84.425,22.0736 73.2169,20.0669 64.1921,17.8372 55.0218,14.9387 43.2314,10.4794 36.8268,9.81048 27.6565,13.7124 18.7773,20.5128 14.7016,28.9855 15.7205,35.8974 17.9039,42.8094 22.2707,49.9443 27.9476,57.8595 30.7132,64.66 30.7132,68.2274 26.9287,69.0078 19.6507,64.1026 12.8093,55.0725 5.67686,41.806 3.05677,28.9855 0.436681,12.8205 1.31004,7.24638 4.65793,3.9019 12.9549,2.67559 24.7453,4.34783 40.6114,7.24638 67.3945,9.81048 83.115,12.932 91.1208))" ,
        {
            { "POINT(15.0502 95.6332)", true },
            { "POINT(20.1784 93.1587)", true },
            { "POINT(20.8473 88.5007)", true },
            { "POINT(17.6143 83.8428)", true },
            { "POINT(13.8239 74.0902)", true },
            { "POINT(10.0334 58.0786)", true },
            { "POINT(12.709 52.2562)", true },
            { "POINT(5.46265 26.2009)", true },
            { "POINT(6.68896 14.4105)", true },
            { "POINT(10.9253 13.3916)", true },
            { "POINT(13.4894 6.69578)", true },
            { "POINT(26.1984 2.03785)", true },
            { "POINT(33.6678 7.56914)", true },
            { "POINT(36.6778 12.2271)", true },
            { "POINT(53.9576 21.9796)", true },
            { "POINT(62.6533 23.4352)", true },
            { "POINT(65.3289 17.6128)", true },
            { "POINT(9.58751 91.9942)", false },
            { "POINT(6.24303 79.9127)", false },
            { "POINT(2.00669 59.9709)", false },
            { "POINT(1.67224 26.3464)", false },
            { "POINT(3.56745 2.18341)", false },
            { "POINT(15.7191 28.5298)", false },
            { "POINT(22.408 45.5604)", false },
            { "POINT(37.2352 94.3231)", false },
            { "POINT(23.1884 99.7089)", false },
            { "POINT(80.825 20.3785)", false },
            { "POINT(62.4303 4.94905)", false },
            { "POINT(46.5998 0.873362)", false },
        }
    },
};

struct PointAndResult
{
    Point point;
    bool result;
};

/* Arg(0) is the index of the benchmark data to use in the Benchmarks vector. */
static void BM_pip_test(benchmark::State &state)
{
    const size_t dataIndex = static_cast<size_t>(state.range(0));
    assert(dataIndex < Benchmarks.size());

    const auto &benchData = Benchmarks[dataIndex];

    // read input data
    Polygon polygon;
    std::vector<PointAndResult> pars;

    bg::read_wkt(benchData.polygon, polygon);

    for (const auto &ipar: benchData.pointsAndResults)
    {
        Point p;
        bg::read_wkt(ipar.point, p);
        pars.push_back({p, ipar.result});
    }

    // run the Point in Polygon tests
    size_t pipCount = 0;

    for (auto _: state)
    {
        for (const auto &par: pars)
        {
            const bool is_within = bg::within(par.point, polygon);
            benchmark::DoNotOptimize(is_within);
            assert(is_within == par.result);
            pipCount++;
        }
    }

    state.counters["PiP_count"] = Counter(pipCount);
    state.counters["Pip_rate"]  = Counter(pipCount, Counter::kIsRate);
    state.counters["poly_outer_points"] = polygon.outer().size();
}
BENCHMARK(BM_pip_test)->DenseRange(0, Benchmarks.size() - 1);

/* Arg(0) is the index of the benchmark data to use in the Benchmarks vector.
 * Runs boost::geometry::correct() on the polygon prior to benchmarking. */
static void BM_pip_test_with_correction(benchmark::State &state)
{
    const size_t dataIndex = static_cast<size_t>(state.range(0));
    assert(dataIndex < Benchmarks.size());

    const auto &benchData = Benchmarks[dataIndex];

    // read input data
    Polygon polygon;
    std::vector<PointAndResult> pars;

    bg::read_wkt(benchData.polygon, polygon);
    bg::correct(polygon);

    for (const auto &ipar: benchData.pointsAndResults)
    {
        Point p;
        bg::read_wkt(ipar.point, p);
        pars.push_back({p, ipar.result});
    }

    // run the Point in Polygon tests
    size_t pipCount = 0;

    for (auto _: state)
    {
        for (const auto &par: pars)
        {
            const bool is_within = bg::within(par.point, polygon);
            benchmark::DoNotOptimize(is_within);
            assert(is_within == par.result);
            pipCount++;
        }
    }

    state.counters["PiP_count"] = Counter(pipCount);
    state.counters["Pip_rate"]  = Counter(pipCount, Counter::kIsRate);
    state.counters["poly_outer_points"] = polygon.outer().size();
}
BENCHMARK(BM_pip_test_with_correction)->DenseRange(0, Benchmarks.size() - 1);

BENCHMARK_MAIN();
