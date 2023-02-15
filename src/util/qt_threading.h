#ifndef SRC_UTIL_QT_THREADING_H_
#define SRC_UTIL_QT_THREADING_H_

#include <exception>
#include <QException>

namespace mesytec::mvme
{

// Helper to transport std::exception_ptr instances between Qt threads.
// Usage:
//   try { myCode(); }
//   catch (...) { throw QtExceptionPtr(std::current_exception()); }
class QtExceptionPtr : public QException
{
    public:
        explicit QtExceptionPtr(const std::exception_ptr &ptr)
            : m_ptr(ptr)
        {
        }

        std::exception_ptr get() const { return m_ptr; }
        void raise() const override { std::rethrow_exception(m_ptr); }
        QtExceptionPtr *clone() const override { return new QtExceptionPtr(*this); }

    private:
        std::exception_ptr m_ptr;
};

}

#endif // SRC_UTIL_QT_THREADING_H_