#ifndef __MVLC_DIALOG_H__
#define __MVLC_DIALOG_H__

#include <functional>
#include <QVector>
#include "mvlc/mvlc_impl_abstract.h"
#include "mvlc/mvlc_buffer_validators.h"

// Higher level MVLC dialog (request/response) layer. Builds on top of the
// AbstractImpl abstraction.

namespace mesytec
{
namespace mvlc
{

std::error_code check_mirror(const QVector<u32> &request, const QVector<u32> &response);

class MVLCDialog
{
    public:

        MVLCDialog(AbstractImpl *mvlc);

        // MVLC register access
        std::error_code readRegister(u16 address, u32 &value);
        std::error_code writeRegister(u16 address, u32 value);
        std::error_code readRegisterBlock(u16 address, u16 words,
                                          QVector<u32> &dest);

        // Higher level VME access
        // Note: Stack0 is used for the VME commands and the stack is written
        // starting from offset 0 into stack memory.

        std::error_code vmeSingleRead(u32 address, u32 &value, u8 amod,
                                      VMEDataWidth dataWidth);

        std::error_code vmeSingleWrite(u32 address, u32 value, u8 amod,
                                       VMEDataWidth dataWidth);

        // Note: The data from the block read is currently returned as is
        // including the stack frame (0xF3) and block frame (0xF5) headers.
        // The flags of either of these headers are not interpreted by this
        // method.
        std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers,
                                     QVector<u32> &dest);

        // Lower level utilities

        // Read a full response buffer into dest. The buffer header is passed
        // to the BufferHeaderValidator and MVLCErrorCode::InvalidBufferHeader
        // is returned if the validation fails (in this case the data will
        // still be available in the dest buffer for inspection).
        //
        // Note: internally buffers are read from the MVLC until a
        // non-stack_error_notification type buffer is read. All error
        // notifications received up to that point are saved and can be queried
        // using getStackErrorNotifications().
        std::error_code readResponse(BufferHeaderValidator bhv, QVector<u32> &dest);

        // Send the given cmdBuffer to the MVLC, reads and verifies the mirror
        // response. The buffer must start with CmdBufferStart and end with
        // CmdBufferEnd, otherwise the MVLC cannot interpret it.
        std::error_code mirrorTransaction(const QVector<u32> &cmdBuffer,
                                          QVector<u32> &responseDest);

        // Sends the given stack data (which must include upload commands),
        // reads and verifies the mirror response, and executes the stack.
        // Notes:
        // - Stack0 is used and offset 0 into stack memory is assumed.
        // - Stack responses consisting of multiple frames (0xF3 followed by
        //   0xF9 frames) are supported. The stack frames will all be copied to
        //   the responseDest vector.
        // - Any stack error notifications read while attempting to read an
        //   actual stack response are available via
        //   getStackErrorNotifications().
        std::error_code stackTransaction(const QVector<u32> &stackUploadData,
                                         QVector<u32> &responseDest);

        // Low level read accepting any of the known buffer types (see
        // is_known_buffer_header()). Does not do any special handling for
        // stack error notification buffers as is done in readResponse().
        std::error_code readKnownBuffer(QVector<u32> &dest);

        // Returns the response buffer used internally by readRegister(),
        // readRegisterBlock(), writeRegister(), vmeSingleWrite() and
        // vmeSingleRead().
        // The buffer will contain the last data received from the MVLC.
        QVector<u32> getResponseBuffer() const { return m_responseBuffer; }

        QVector<QVector<u32>> getStackErrorNotifications() const
        {
            return m_stackErrorNotifications;
        }

        void clearStackErrorNotifications()
        {
            m_stackErrorNotifications.clear();
        }

        bool hasStackErrorNotifications() const
        {
            return !m_stackErrorNotifications.isEmpty();
        }

    private:
        std::error_code doWrite(const QVector<u32> &buffer);
        std::error_code readWords(u32 *dest, size_t count, size_t &wordsTransferred);

        void logBuffer(const QVector<u32> &buffer, const QString &info);

        AbstractImpl *m_mvlc = nullptr;
        u32 m_referenceWord = 1;
        QVector<u32> m_responseBuffer;
        QVector<QVector<u32>> m_stackErrorNotifications;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_DIALOG_H__ */
