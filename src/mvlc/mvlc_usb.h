#ifndef __MVME_MVLC_USB_H__
#define __MVME_MVLC_USB_H__

#include <memory>
#include <vector>

#include <QVector>
#include <QString>

#include "mvlc/mvlc_constants.h"

namespace mesytec
{
namespace mvlc
{
namespace usb
{
using err_t = int;

struct USB_Impl
{
    void *handle = nullptr;
    u32 writeTimeout_ms = 5000;
    u32 readTimeout_ms = 5000;
};

USB_Impl open_by_index(unsigned index, err_t *error = nullptr);
USB_Impl open_by_serial(const char *serial, err_t *error = nullptr);
USB_Impl open_by_description(const char *description, err_t *error = nullptr);

err_t close(USB_Impl &mvlc);

const char *error_str(err_t error);
bool is_timeout(err_t error);
inline bool is_open(const USB_Impl &impl)
{
    return impl.handle != nullptr;
}

//
// Low level read and write operations
//

err_t write_bytes(USB_Impl *mvlc, u8 writePipe,
                  const u8 *buffer,  size_t size,
                  size_t *bytesTransferred = nullptr);

err_t read_bytes(USB_Impl *mvlc, u8 readPipe,
                 u8 *dest, size_t size,
                 size_t *bytesTransferred = nullptr);


err_t write_words(USB_Impl *mvlc, u8 pipe,
                  const u32 *buffer, size_t wordCount,
                  size_t *wordsTransferred = nullptr);

err_t read_words(USB_Impl *mvlc, u8 pipe,
                 u32 *dest, size_t wordCount,
                 size_t *wordsTransferred = nullptr);

//
// read and write using std::vector buffers
//

err_t write_words(USB_Impl *mvlc, u8 pipe, const std::vector<u32> &buffer,
                  size_t *wordsTransferred = nullptr);

// Trys to read dest.size() words into dest. Resizes dest buffer to actual
// number of words received.
// IMPORTANT: the resize operation done to the vector is really slow due to c++
// value initialization writing to all the memory.
err_t read_words(USB_Impl *mvlc, u8 pipe, std::vector<u32> &dest);

//
// read and write using QVector buffers
//

err_t write_words(USB_Impl *mvlc, u8 pipe, const QVector<u32> &buffer,
                  size_t *wordsTransferred = nullptr);

// Trys to read dest.size() words into dest. Resizes dest buffer to actual
// number of words received.
// IMPORTANT: the resize operation done to the vector is really slow due to c++
// value initialization writing to all the memory.
err_t read_words(USB_Impl *mvlc, u8 pipe, QVector<u32> &dest);


//
// Dialog API
//
// This is a protocol layer on top of the byte level read and write operations.
// Internally MVLCCommandListBuilders are used to create correct output
// buffers.
// Responses are read using the size information specific by the MVLC in its
// response header words (F1 for command buffer results, F3 for stack execution
// results).
//
// TODO and FIXME
// Unsolicited status responses are not yet handled in here. These must be
// detected and stored in a separate buffer. They form a separate channel of
// information.

struct MVLCError
{
    enum Type
    {
        NoError,
        USBError,
        IsOpen,
        IsClosed,
        ShortWrite,
        ShortRead,
        MirrorShortRequest,  // size < 1
        MirrorShortResponse, // size < 1
        MirrorResponseTooShort,
        MirrorNotEqual,
        ParseResponseUnexpectedSize,
        ParseUnexpectedBufferType,
        NoVMEResponse,
    };

    Type type = NoError;
    err_t ft_error = 0;

    MVLCError(Type t = NoError, err_t code = 0)
        : type(t), ft_error(code)
    {}

    QString toString() const;
    operator bool() const { return type == NoError; }
};

inline MVLCError make_usb_error(err_t err)
{
    if (err != 0)
        return { MVLCError::USBError, err };

    return { MVLCError::NoError, 0 };
}

inline MVLCError make_success()
{
    return { MVLCError::NoError, 0 };
}

MVLCError get_read_queue_size(USB_Impl *mvlc, u8 pipe, u32 &dest);
MVLCError check_mirror(const QVector<u32> &request, const QVector<u32> &response);
MVLCError write_buffer(USB_Impl *mvlc, const QVector<u32> &buffer);
MVLCError write_buffer(USB_Impl *mvlc, const std::vector<u32> &buffer);
//MVLCError read_response(USB_Impl *mvlc, u8 requiredBufferType, QVector<u32> &dest);
MVLCError read_response(USB_Impl *mvlc, QVector<u32> &dest);

class MVLCDialog
{
    public:
        MVLCDialog(const usb::USB_Impl &impl);

        MVLCError readRegister(u32 address, u32 &value);
        MVLCError writeRegister(u32 address, u32 value);

        MVLCError vmeSingleRead(u32 address, u32 &value, AddressMode amod,
                                VMEDataWidth dataWidth);
        MVLCError vmeSingleWrite(u32 address, u32 value, AddressMode amod,
                                 VMEDataWidth dataWidth);
        MVLCError vmeBlockRead(u32 address, AddressMode amod, u16 maxTransfers,
                               QVector<u32> &dest);

        MVLCError doWrite(const QVector<u32> &buffer);
        MVLCError doWrite(const std::vector<u32> &buffer);
        MVLCError readResponse(QVector<u32> &dest);

    private:
        MVLCError readResponse(u8 requiredBufferType, QVector<u32> &dest);

        usb::USB_Impl m_impl;
        u32 m_referenceWord = 1;
        QVector<u32> m_responseBuffer;
};

} // end namespace usb
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_USB_H__ */
