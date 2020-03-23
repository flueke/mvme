#ifndef __MESYTEC_MVLC_THREADSAFEQUEUE_H__
#define __MESYTEC_MVLC_THREADSAFEQUEUE_H__

#include <condition_variable>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>

namespace mesytec
{
namespace mvlc
{

template<class T, class Allocator = std::allocator<T>>
class ThreadSafeQueue
{
    public:
        explicit ThreadSafeQueue(const Allocator &alloc = Allocator())
            : m_queue(alloc)
        {}

        void enqueue(const T &value)
        {
            {
                Lock lock(m_mutex);
                m_queue.push_back(value);
            }

            m_cond.notify_one();
        }

        void enqueue(const T &&value)
        {
            {
                Lock lock(m_mutex);
                m_queue.push_back(value);
            }

            m_cond.notify_one();
        }

        // Dequeue operation returning a default constructed value if the queue
        // is empty.
        T dequeue()
        {
            Lock lock(m_mutex);

            if (!m_queue.empty())
            {
                auto ret = m_queue.front();
                m_queue.pop_front();
                return ret;
            }

            return {};
        }

        // Dequeue operation waiting for the queues wait condition to be
        // signaled in case the queue is empty. A default constructed value is
        // returned in case the wait times out and the queue is still empty.
        T dequeue(const std::chrono::milliseconds &timeout)
        {
            auto pred = [this] () { return !m_queue.empty(); };

            Lock lock(m_mutex);

            if (!m_cond.wait_for(lock, timeout, pred))
                return {};

            auto ret = m_queue.front();
            m_queue.pop_front();
            return ret;
        }

        T dequeue_blocking()
        {
            auto pred = [this] () { return !m_queue.empty(); };

            Lock lock(m_mutex);

            m_cond.wait(lock, pred);

            auto ret = m_queue.front();
            m_queue.pop_front();
            return ret;
        }

        bool empty() const
        {
            Lock lock(m_mutex);
            return m_queue.empty();
        }

        typename std::deque<T>::size_type size() const
        {
            Lock lock(m_mutex);
            return m_queue.size();
        }

    private:
        std::deque<T> m_queue;
        mutable std::mutex m_mutex;
        std::condition_variable m_cond;

        using Lock = std::unique_lock<std::mutex>;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_THREADSAFEQUEUE_H__ */
