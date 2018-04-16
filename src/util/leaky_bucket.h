#ifndef __MVME_UTIL_LEAKY_BUCKET_H__
#define __MVME_UTIL_LEAKY_BUCKET_H__

/* Use case: Rate limitting of log message.
 *
 * User wants to know:
 * - if a message should be logged or dropped
 * - how many messages have been suppressed (since the last message was let
 *   through).
 */

#include <chrono>
#include <QDebug>

class LeakyBucketMeter
{
    public:
        using ClockType = std::chrono::steady_clock;
        using Duration  = ClockType::duration;

        LeakyBucketMeter(size_t capacity, const Duration &interval)
            : m_capacity(capacity)
            , m_interval(interval)
            , m_count(0)
            , m_overflow(0)
            , m_lastTime(ClockType::now())
        {
        }

        /* Returns true if the bucket has overflowed, false otherwise. */
        bool eventOverflows()
        {
            if (m_capacity == 0)
                return false;

            age();

            if (m_count >= m_capacity)
            {
                m_overflow++;
                return true;
            }

            m_count++;
            m_overflow = 0;

            return false;
        }

        size_t overflow() const
        {
            return m_overflow;
        }

    private:
        const size_t m_capacity;
        const Duration m_interval;

        size_t m_count;
        size_t m_overflow;
        ClockType::time_point m_lastTime;

        void age()
        {
            auto now = ClockType::now();
            std::chrono::duration<double> diff = now - m_lastTime;

            //qDebug() << __PRETTY_FUNCTION__ << "diff =" << diff.count();

            if (diff > m_interval)
            {
                size_t to_remove = diff / m_interval * m_capacity;

                //qDebug() << __PRETTY_FUNCTION__ << "to_remove =" << to_remove << ", m_count before remove =" << m_count;

                if (to_remove > m_count)
                    m_count = 0;
                else
                    m_count -= to_remove;

                qDebug() << __PRETTY_FUNCTION__ << "to_remove =" << to_remove << ", m_count after remove =" << m_count;

                m_lastTime = now;
            }
        }
};

#endif /* __MVME_UTIL_LEAKY_BUCKET_H__ */
