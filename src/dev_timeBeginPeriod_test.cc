//#include <timeapi.h>
#include <windows.h>
#include <mmsystem.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include "typedefs.h"

using std::cerr;
using std::cout;
using std::endl;

struct SetsTimePeriod
{
    SetsTimePeriod(u16 period)
        : m_period(period)
    {
        if (timeBeginPeriod(m_period) != TIMERR_NOERROR)
            throw std::runtime_error("timeBeginPeriod failed");

        cerr << __PRETTY_FUNCTION__ << " " << this << "beginPeriod " << m_period << endl;
    }


    ~SetsTimePeriod()
    {
        if (timeEndPeriod(m_period) != TIMERR_NOERROR)
            throw std::runtime_error("timeEndPeriod failed");

        cerr << __PRETTY_FUNCTION__ << " " << this << "endPeriod " << m_period << endl;
    }

    u16 m_period = 0u;
};

struct SleepStats
{
    using Duration = std::chrono::microseconds;
    Duration dtMin = Duration::max();
    Duration dtMax = Duration::min();
    Duration dtAvg = Duration::zero();
    size_t nSleeps;
};

SleepStats test_sleep_granularity(std::chrono::milliseconds sleepDuration, size_t iterations)
{
    SleepStats result{};
    std::chrono::microseconds dtTotal{};

    for (size_t i=0; i < iterations; ++i)
    {
        auto tStart = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(sleepDuration);
        auto tEnd = std::chrono::steady_clock::now();

        auto dt = std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart);
        dtTotal += dt;
        result.dtMin = std::min(result.dtMin, dt);
        result.dtMax = std::max(result.dtMax, dt);
        ++result.nSleeps;
    }

    result.dtAvg = dtTotal / result.nSleeps;
    
    return result;
}

int main(int argc, char *argv[])
{
    if (argc != 3
        || argv[1] == std::string("-h")
        || argv[1] == std::string("--help")
        || argv[2] == std::string("-h")
        || argv[2] == std::string("-help"))
    {
        std::cout << "Usage: " << argv[0] << " <sleepDuration[ms]> <testIterations>" << endl;
        return 1;
    }

    std::chrono::milliseconds sleepDuration(std::atoi(argv[1]));
    size_t iterations = std::atoi(argv[2]);

    auto run_test = [=] ()
    {
        auto result = test_sleep_granularity(sleepDuration, iterations);

        cout
            << "dtSleep=" << sleepDuration.count() << " ms"
            << "nSleeps=" << result.nSleeps
            << ", dtMin=" << result.dtMin.count() << " us"
            << ", dtMax=" << result.dtMax.count() << " us"
            << ", dtAvg=" << result.dtAvg.count() << " us"
            << endl;
    };

    run_test();

    {
        SetsTimePeriod stp1(1);
        run_test();
    }

    run_test();

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
