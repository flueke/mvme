#ifndef __MVME_UTIL_DEBUG_TIMER_H__
#define __MVME_UTIL_DEBUG_TIMER_H__

#include <chrono>

class DebugTimer
{
    public:
        using Clock = std::chrono::high_resolution_clock;

        DebugTimer() { start(); }

        void start() { tStart = Clock::now(); }

        Clock::duration elapsed() const { return Clock::now() - tStart; }

        Clock::duration restart()
        {
            auto now = Clock::now();
            auto elapsed_ = now - tStart;
            tStart = now;
            return elapsed_;
        }

    private:
        Clock::time_point tStart;
};

#endif /* __MVME_UTIL_DEBUG_TIMER_H__ */
