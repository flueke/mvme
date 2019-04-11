#ifndef __MVLC_ABSTRACT_IMPL_H__
#define __MVLC_ABSTRACT_IMPL_H__

#include <system_error>
#include "mvlc/mvlc_constants.h"
#include "typedefs.h"

namespace mesytec
{
namespace mvlc
{

class AbstractImpl
{
    public:
        virtual std::error_code connect() = 0;
        virtual std::error_code disconnect() = 0;
        virtual bool isConnected() const = 0;

        virtual void setWriteTimeout(Pipe pipe, unsigned ms) = 0;
        virtual void setReadTimeout(Pipe pipe, unsigned ms) = 0;

        virtual unsigned getWriteTimeout(Pipe pipe) const = 0;
        virtual unsigned getReadTimeout(Pipe pipe) const = 0;

        virtual std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                                      size_t &bytesTransferred) = 0;

        virtual std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                                     size_t &bytesTransferred) = 0;

        virtual ConnectionType connectionType() const = 0;

        AbstractImpl() = default;
        AbstractImpl &operator=(const AbstractImpl &) = delete;
        AbstractImpl(const AbstractImpl &) = delete;
        virtual ~AbstractImpl() {};
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_ABSTRACT_IMPL_H__ */
