#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
#include "typedefs.h"

using std::cerr;
using std::cout;
using std::endl;

struct SetsTimePeriod
{
    SetsTimePeriod(unsigned period)
        : m_period(period)
    {
        if (timeBeginPeriod(m_period) != TIMERR_NOERROR)
            throw std::runtime_error("timeBeginPeriod failed");

        cerr << __PRETTY_FUNCTION__ << " " << this << " beginPeriod " << m_period << endl;
    }


    ~SetsTimePeriod()
    {
        if (timeEndPeriod(m_period) != TIMERR_NOERROR)
            throw std::runtime_error("timeEndPeriod failed");

        cerr << __PRETTY_FUNCTION__ << " " << this << " endPeriod " << m_period << endl;
    }

    unsigned m_period = 0u;
};

struct SleepStats
{
    using Duration = std::chrono::microseconds;
    Duration dtMin = Duration::max();
    Duration dtMax = Duration::min();
    Duration dtAvg = Duration::zero();
    Duration dtMed = Duration::zero();
    size_t nSleeps;
};

SleepStats test_sleep_granularity(std::chrono::milliseconds sleepDuration, size_t iterations)
{
    SleepStats result{};
    std::chrono::microseconds dtTotal{};
    std::vector<std::chrono::microseconds> deltaTimes(iterations);

    for (size_t i=0; i < iterations; ++i)
    {
        auto tStart = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(sleepDuration);
        auto tEnd = std::chrono::high_resolution_clock::now();

        auto dt = std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart);
        dtTotal += dt;
        result.dtMin = std::min(result.dtMin, dt);
        result.dtMax = std::max(result.dtMax, dt);
        ++result.nSleeps;
        deltaTimes[i] = dt;
    }

    result.dtAvg = dtTotal / result.nSleeps;

    std::sort(deltaTimes.begin(), deltaTimes.end());

    if (deltaTimes.size() % 2)
        result.dtMed = SleepStats::Duration(deltaTimes[deltaTimes.size() / 2]);
    else
        result.dtMed = SleepStats::Duration((deltaTimes[deltaTimes.size() / 2 - 1] + deltaTimes[deltaTimes.size() / 2]) / 2);
    
    return result;
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        std::cout << "Usage: " << argv[0] << " <sleepDuration[ms]> <testIterations> <timeBeginPeriod>" << endl;
        return 1;
    }

    std::chrono::milliseconds sleepDuration(std::atoi(argv[1]));
    size_t iterations = std::atoi(argv[2]);
    unsigned timePeriod = std::atoi(argv[3]);

    auto run_test = [=] ()
    {
        auto result = test_sleep_granularity(sleepDuration, iterations);

        cout
            << "sleepDuration=" << sleepDuration.count() << " ms"
            << ", iterations=" << iterations
            << ", timePeriod for 2nd run=" << timePeriod
            << endl
            << "  nSleeps=" << result.nSleeps
            << ", dtMin=" << result.dtMin.count() << " us"
            << ", dtMax=" << result.dtMax.count() << " us"
            << ", dtAvg=" << result.dtAvg.count() << " us"
            << ", dtMed=" << result.dtMed.count() << " us"
            << endl;
    };

    run_test();

    {
        SetsTimePeriod stp1(timePeriod);
        run_test();
    }


#if 0
    {
        SetsTimePeriod stp1(5);
        {
            SetsTimePeriod stp2(5);
            run_test();
        }
        run_test();
    }

    run_test();

    {
        SetsTimePeriod stp1(10);
        {
            SetsTimePeriod stp2(5);
            run_test();
        }
        run_test();
    }
#endif

    return 0;
}
