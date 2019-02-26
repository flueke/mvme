#ifndef __MVLC_DIALOG_H__
#define __MVLC_DIALOG_H__

#include <functional>
#include <QVector>
#include "mvlc/mvlc_qt_object.h"

namespace mesytec
{
namespace mvlc
{

std::error_code check_mirror(const QVector<u32> &request, const QVector<u32> &response);

class MVLCDialog
{
    public:
        using BufferHeaderValidator = std::function<bool (u32 header)>;

        MVLCDialog(MVLCObject *mvlc);

        // MVLC register access
        std::error_code readRegister(u32 address, u32 &value);
        std::error_code writeRegister(u32 address, u32 value);

        // Higher level VME access
        std::error_code vmeSingleRead(u32 address, u32 &value, AddressMode amod,
                                      VMEDataWidth dataWidth);

        std::error_code vmeSingleWrite(u32 address, u32 value, AddressMode amod,
                                       VMEDataWidth dataWidth);

        std::error_code vmeBlockRead(u32 address, AddressMode amod, u16 maxTransfers,
                                     QVector<u32> &dest);

        // Lower level helpers

        // Read a full response buffer into dest. The buffer header is passed
        // to the validator before attempting to read the rest of the response.
        // If validation fails no more data is read.
        std::error_code readResponse(BufferHeaderValidator bhv, QVector<u32> &dest);

        // Send the given cmdBuffer to the MVLC and read and verify the mirror
        // response. The buffer must start with CmdBufferStart and end with
        // CmdBufferEnd, otherwise the MVLC cannot interpret it.
        std::error_code mirrorTransaction(const QVector<u32> &cmdBuffer,
                                          QVector<u32> &responseDest);

        // Sends the given stack data (which must include upload commands),
        // reads and verifies the mirror response and executes the stack.
        // IMPORTANT: Stack0 is used and offset 0 into stack memory is assumed.
        std::error_code stackTransaction(const QVector<u32> &stackUploadData,
                                         QVector<u32> &responseDest);

    private:
        std::error_code doWrite(const QVector<u32> &buffer);

        void logBuffer(const QVector<u32> &buffer, const QString &info);

        MVLCObject *m_mvlc;
        u32 m_referenceWord = 1;
        QVector<u32> m_responseBuffer;
};

// BufferHeaderValidators

inline bool is_super_buffer(u32 header)
{
    return (header >> buffer_types::TypeShift) == buffer_types::SuperBuffer;
}

inline bool is_stack_buffer(u32 header)
{
    return (header >> buffer_types::TypeShift) == buffer_types::StackBuffer;
}

inline bool is_blockread_buffer(u32 header)
{
    return (header >> buffer_types::TypeShift) == buffer_types::BlockRead;
}

inline bool is_stackerror_buffer(u32 header)
{
    return (header >> buffer_types::TypeShift) == buffer_types::StackError;
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_DIALOG_H__ */
