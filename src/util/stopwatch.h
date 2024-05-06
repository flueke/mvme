#ifndef A51CBD7A_4146_4BB8_8DC4_6C365EBDA3DE
#define A51CBD7A_4146_4BB8_8DC4_6C365EBDA3DE

#include <chrono>

class StopWatch
{
public:
    using duration_type = std::chrono::microseconds;

    void start()
    {
        tStart_ = tInterval_ = std::chrono::high_resolution_clock::now();
    }

    // Returns the elapsed time in the current interval and restarts the interval.
    duration_type interval()
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto result = std::chrono::duration_cast<duration_type>(now - tInterval_);
        tInterval_ = now;
        return result;
    }

    // Returns the elapsed time from start() to now.
    duration_type end()
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto result = std::chrono::duration_cast<duration_type>(now - tStart_);
        return result;
    }

    // Const version of interval(): returns the elapsed time in the current
    // interval without resetting it.
    duration_type get_interval() const
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto result = std::chrono::duration_cast<duration_type>(now - tInterval_);
        return result;
    }

private:
    std::chrono::high_resolution_clock::time_point tStart_;
    std::chrono::high_resolution_clock::time_point tInterval_;
};

#endif /* A51CBD7A_4146_4BB8_8DC4_6C365EBDA3DE */
