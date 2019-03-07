#ifndef __MVLC_QT_OBJECT_H__
#define __MVLC_QT_OBJECT_H__

#include <memory>
#include <mutex>
#include <QObject>
#include <QVector>

#include "mvlc/mvlc_dialog.h"
#include "mvlc/mvlc_impl_abstract.h"
#include "mvlc/mvlc_threading.h"

namespace mesytec
{
namespace mvlc
{

class MVLCObject: public QObject
{
    Q_OBJECT
    public:
        enum State
        {
            Disconnected,
            Connecting,
            Connected,
        };

    signals:
        void stateChanged(const State &oldState, const State &newState);
        void stackErrorNotification(const QVector<u32> &notification);

    public:
        explicit MVLCObject(std::unique_ptr<AbstractImpl> impl, QObject *parent = nullptr);
        virtual ~MVLCObject();

        bool isConnected() const;
        State getState() const { return m_state; }

        AbstractImpl *getImpl();

        unsigned getReadTimeout(Pipe pipe) const;
        unsigned getWriteTimeout(Pipe pipe) const;

        //
        // Lowest level read and write operations
        //
        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred);

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred);

        //std::pair<std::error_code, size_t> write(Pipe pipe, const QVector<u32> &buffer);

        //
        // MVLC register access
        //
        std::error_code readRegister(u16 address, u32 &value);
        std::error_code writeRegister(u16 address, u32 value);

        std::error_code readRegisterBlock(u16 address, u16 words,
                                          QVector<u32> &dest);
        //
        // Higher level direct VME access using Stack0 and immeidate exec.
        //
        // Note: Stack0 is used and the stack is written starting from
        // offset 0 into stack memory.
        std::error_code vmeSingleRead(u32 address, u32 &value, AddressMode amod,
                                      VMEDataWidth dataWidth);

        std::error_code vmeSingleWrite(u32 address, u32 value, AddressMode amod,
                                       VMEDataWidth dataWidth);

        std::error_code vmeBlockRead(u32 address, AddressMode amod, u16 maxTransfers,
                                     QVector<u32> &dest);

        //
        // Lower level utilities
        //

        // Read a full response buffer into dest. The buffer header is passed
        // to the validator before attempting to read the rest of the response.
        // If validation fails no more data is read.
        std::error_code readResponse(BufferHeaderValidator bhv, QVector<u32> &dest);

        // Send the given cmdBuffer to the MVLC, reads and verifies the mirror
        // response. The buffer must start with CmdBufferStart and end with
        // CmdBufferEnd, otherwise the MVLC cannot interpret it.
        std::error_code mirrorTransaction(const QVector<u32> &cmdBuffer,
                                          QVector<u32> &responseDest);

        // Sends the given stack data (which must include upload commands),
        // reads and verifies the mirror response, and executes the stack.
        // Note: Stack0 is used and offset 0 into stack memory is assumed.
        std::error_code stackTransaction(const QVector<u32> &stackUploadData,
                                         QVector<u32> &responseDest);

        // Returns the response buffer which will contain the contents of the
        // last read from the MVLC.
        // After mirrorTransaction() the buffer will contain the mirror
        // response. After stackTransaction() the buffer will contain the
        // response from executing the stack.
        QVector<u32> getResponseBuffer() const;

        // Get the stack error notifications that may have resulted from a
        // previous operation. Performing another operation will clear the
        // internal buffer.
        // The data available from this method will also have been emitted via
        // the stackErrorNotification() signal.
        QVector<QVector<u32>> getStackErrorNotifications() const;

        Locks &getLocks() { return m_locks; }

    public slots:
        std::error_code connect();
        std::error_code disconnect();
        void setReadTimeout(Pipe pipe, unsigned ms);
        void setWriteTimeout(Pipe pipe, unsigned ms);

    private:
        void setState(const State &newState);
        Locks &getLocks() const { return m_locks; }
        void postDialogOperation();

        std::unique_ptr<AbstractImpl> m_impl;
        MVLCDialog m_dialog;
        State m_state;
        mutable Locks m_locks;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_QT_OBJECT_H__ */
