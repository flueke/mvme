#include <gtest/gtest.h>

#include "mdpp-sampling/waveform_interpolation.h"
#include <mesytec-mvlc/cpp_compat.h>
#include <mesytec-mvlc/util/fmt.h>
#include <string_view>
#include <QVector>

using namespace mesytec::mvme;
using std::basic_string_view;
using waveforms::interpolate;

static const std::vector<s16> Samples = {4, 5, 6, 5, 3, 5, 23, 340, 1445, 2734, 3090, 2755, 2629, 2723, 2727, 2712, 2732, 2715, 2696, 2706, 2707, 2692, 2690, 2691, 2681, 2679, 2676, 2679};


#define MY_DEBUG_THING 1

#if MY_DEBUG_THING


// No interpolation possible due to min window size.
TEST(MdppSampling, interpolate_no_interpolation)
{
    const basic_string_view<s16> samples(Samples.data(), 5);

    {
        waveforms::Trace result;
        interpolate(samples, 1.0, 1, result);

        waveforms::Trace expected(
            std::vector<double>{ 0.0, 1.0, 2.0, 3.0, 4.0 },
            std::vector<double>{ 4.0, 5.0, 6.0, 5.0, 3.0 });

        ASSERT_EQ(result, expected);
    }

    {
        waveforms::Trace result;
        interpolate(samples, 1.0, 2, result);

        waveforms::Trace expected(
            std::vector<double>{ 0.0, 1.0, 2.0, 3.0, 4.0 },
            std::vector<double>{ 4.0, 5.0, 6.0, 5.0, 3.0 });

        ASSERT_EQ(result, expected);
    }

    {
        waveforms::Trace result;
        interpolate(samples, 2.0, 0, result);

        waveforms::Trace expected(
            std::vector<double>{ 0.0, 2.0, 4.0, 6.0, 8.0 },
            std::vector<double>{ 4.0, 5.0, 6.0, 5.0, 3.0 });

        ASSERT_EQ(result, expected);
    }
}

// Interpolation with exactly the min required number of samples.
TEST(MdppSampling, interpolate_min_required_samples)
{
    const basic_string_view<s16> samples(Samples.data(), 6);

    {
        waveforms::Trace result;
        interpolate(samples, 1.0, 1, result);

        waveforms::Trace expected(
            std::vector<double>{ 0.0, 1.0, 2.0, 3.0, 4.0, 5.0 },
            std::vector<double>{ 4.0, 5.0, 6.0, 5.0, 3.0, 5.0 });

        ASSERT_EQ(result, expected);
    }

    {
        waveforms::Trace result;
        interpolate(samples, 2.0, 1, result);

        waveforms::Trace expected(
            std::vector<double>{ 0.0, 2.0, 4.0, 6.0, 8.0, 10.0 },
            std::vector<double>{ 4.0, 5.0, 6.0, 5.0, 3.0, 5.0 });

        ASSERT_EQ(result, expected);
    }

    {
        waveforms::Trace result;
        interpolate(samples, 1.0, 10, result);

        waveforms::Trace expected(
            std::vector<double>{ 0.0, 1.0, 2.0, 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 3.0, 4.0, 5.0 },
            std::vector<double>{ 4.0, 5.0, 6.0, 6.00896, 5.99537, 5.96031, 5.90397, 5.82529, 5.72194, 5.59065, 5.42797, 5.2313, 5.0, 3.0, 5.0 });

        ASSERT_EQ(result.size(), expected.size());
        const double epsilon = 0.00001;

        for (size_t i = 0; i < result.size(); ++i)
        {
            auto x = result.xs[i];
            auto y = result.ys[i];
            auto ex = expected.xs[i];
            auto ey = expected.ys[i];
            ASSERT_NEAR(x, ex, epsilon);
            ASSERT_NEAR(y, ey, epsilon);
        }
    }

    {
        waveforms::Trace result;
        interpolate(samples, 10.0, 10, result);

        waveforms::Trace expected(
            std::vector<double>{ 0.0, 10.0, 20.0, 21.0, 22.0, 23.0, 24.0, 25.0, 26.0, 27.0, 28.0, 29.0, 30.0, 40.0, 50.0 },
            std::vector<double>{ 4.0, 5.0, 6.0, 6.00896, 5.99537, 5.96031, 5.90397, 5.82529, 5.72194, 5.59065, 5.42797, 5.2313, 5.0, 3.0, 5.0 });

        ASSERT_EQ(result.size(), expected.size());
        const double epsilon = 0.00001;

        for (auto i = 0; i < result.size(); ++i)
        {
            auto x = result.xs[i];
            auto y = result.ys[i];
            auto ex = expected.xs[i];
            auto ey = expected.ys[i];
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
