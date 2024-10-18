#include <gtest/gtest.h>

#include "mdpp-sampling/mdpp_sampling.h"
#include <mesytec-mvlc/cpp_compat.h>
#include <mesytec-mvlc/util/fmt.h>
#include <string_view>

using namespace mesytec::mvme;
using std::basic_string_view;
using mdpp_sampling::interpolate;

static const std::vector<s16> Samples = {4, 5, 6, 5, 3, 5, 23, 340, 1445, 2734, 3090, 2755, 2629, 2723, 2727, 2712, 2732, 2715, 2696, 2706, 2707, 2692, 2690, 2691, 2681, 2679, 2676, 2679};


#define MY_DEBUG_THING 1

#if MY_DEBUG_THING
TEST(MdppSampling, interpolate_no_interpolation)
{
    const basic_string_view<s16> samples(Samples.data(), 5);

    {
        auto result = interpolate(samples, 1, 1.0);
        auto expected = QVector<std::pair<double, double>>{
            {0.0, 4.0},
            {1.0, 5.0},
            {2.0, 6.0},
            {3.0, 5.0},
            {4.0, 3.0}
        };

        ASSERT_EQ(result, expected);
    }

    {
        auto result = interpolate(samples, 2, 1.0);
        auto expected = QVector<std::pair<double, double>>{
            {0.0, 4.0},
            {1.0, 5.0},
            {2.0, 6.0},
            {3.0, 5.0},
            {4.0, 3.0}
        };

        ASSERT_EQ(result, expected);
    }

    {
        auto result = interpolate(samples, 0, 2.0);
        auto expected = QVector<std::pair<double, double>>{
            {0.0, 4.0},
            {2.0, 5.0},
            {4.0, 6.0},
            {6.0, 5.0},
            {8.0, 3.0}
        };

        ASSERT_EQ(result, expected);
    }
}

TEST(MdppSampling, interpolate_min_required_samples)
{
    {
        const basic_string_view<s16> samples(Samples.data(), 6);
        auto result = interpolate(samples, 1, 1.0);
        auto expected = QVector<std::pair<double, double>>{
            {0.0, 4.0},
            {1.0, 5.0},
            {2.0, 6.0},
            {3.0, 5.0},
            {4.0, 3.0},
            {5.0, 5.0}
        };

        ASSERT_EQ(result, expected);
    }

    {
        const basic_string_view<s16> samples(Samples.data(), 6);
        auto result = interpolate(samples, 1, 2.0);
        auto expected = QVector<std::pair<double, double>>{
            {0.0, 4.0},
            {2.0, 5.0},
            {4.0, 6.0},
            {6.0, 5.0},
            {8.0, 3.0},
            {10.0, 5.0}
        };

        ASSERT_EQ(result, expected);
    }

    {
        const basic_string_view<s16> samples(Samples.data(), 6);
        //fmt::print("samples = {}\n", fmt::join(samples, " "));
        auto result = interpolate(samples, 10, 1.0);
        auto expected = QVector<std::pair<double, double>>{
            {0, 4}, {1, 5}, {2, 6}, {2.1, 6.00896}, {2.2, 5.99537}, {2.3, 5.96031}, {2.4, 5.90397}, {2.5, 5.82529}, {2.6, 5.72194}, {2.7, 5.59065}, {2.8, 5.42797}, {2.9, 5.2313}, {3, 5}, {4, 3}, {5, 5}
        };

        ASSERT_EQ(result.size(), expected.size());
        const double epsilon = 0.00001;

        for (auto i = 0; i < result.size(); ++i)
        {
            auto [x, y] = result[i];
            auto [ex, ey] = expected[i];
            ASSERT_NEAR(x, ex, epsilon);
            ASSERT_NEAR(y, ey, epsilon);
        }
    }

    {
        const basic_string_view<s16> samples(Samples.data(), 6);
        //fmt::print("samples = {}\n", fmt::join(samples, " "));
        auto result = interpolate(samples, 10, 10.0);
        auto expected = QVector<std::pair<double, double>>{
              {0, 4}, {10, 5}, {20, 6}, {21, 6.00896}, {22, 5.99537}, {23, 5.96031}, {24, 5.90397}, {25, 5.82529}, {26, 5.72194}, {27, 5.59065}, {28, 5.42797}, {29, 5.2313}, {30, 5}, {40, 3}, {50, 5}
        };

        ASSERT_EQ(result.size(), expected.size());
        const double epsilon = 0.00001;

        for (auto i = 0; i < result.size(); ++i)
        {
            auto [x, y] = result[i];
            auto [ex, ey] = expected[i];
            ASSERT_NEAR(x, ex, epsilon);
            ASSERT_NEAR(y, ey, epsilon);
        }
    }
}
#endif

#if 0
TEST(MdppSampling, interpolate_move_window)
{
    {
        const basic_string_view<s16> samples(Samples.data(), 7);
        fmt::print("samples = {}\n", fmt::join(samples, " "));
        auto result = interpolate(samples, 10, 1.0);
        auto expected = QVector<std::pair<double, double>>{
        };

        //ASSERT_EQ(result.size(), expected.size());
        ASSERT_EQ(result, expected);
        const double epsilon = 0.00001;

        for (auto i = 0; i < result.size(); ++i)
        {
            auto [x, y] = result[i];
            auto [ex, ey] = expected[i];
            ASSERT_NEAR(x, ex, epsilon);
            ASSERT_NEAR(y, ey, epsilon);
        }
    }
}
#endif
