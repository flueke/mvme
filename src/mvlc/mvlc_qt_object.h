#ifndef __MVLC_QT_OBJECT_H__
#define __MVLC_QT_OBJECT_H__

#include <memory>
#include <mutex>
#include <QObject>
#include <QTimer>
#include <QVector>

#include "libmvme_mvlc_export.h"
#include "mvlc/mvlc_dialog.h"
#include "mvlc/mvlc_impl_abstract.h"
#include "mvlc/mvlc_stack_errors.h"
#include "mvlc/mvlc_threading.h"

namespace mesytec
{
namespace mvlc
{

class LIBMVME_MVLC_EXPORT MVLCObject: public QObject
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
        void stateChanged(const mesytec::mvlc::MVLCObject::State &oldState,
                          const mesytec::mvlc::MVLCObject::State &newState);

    public:
        explicit MVLCObject(std::unique_ptr<AbstractImpl> impl, QObject *parent = nullptr);
        virtual ~MVLCObject();

        bool isConnected() const;
        State getState() const { return m_state; }
        ConnectionType connectionType() const { return m_impl->connectionType(); }
        QString getConnectionInfo() const;

        AbstractImpl *getImpl();

        unsigned getReadTimeout(Pipe pipe) const;
        unsigned getWriteTimeout(Pipe pipe) const;

        //
        // Lowest level read and write operations
        //
        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred);

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred);

        std::error_code getReadQueueSize(Pipe pipe, u32 &dest);

        //
        // MVLC register access
        //
        std::error_code readRegister(u16 address, u32 &value);
        std::error_code writeRegister(u16 address, u32 value);

#if 0 // disabled for now. need to test if this is implemented in the firmware and working.
        std::error_code readRegisterBlock(u16 address, u16 words,
                                          QVector<u32> &dest);
#endif
        //
        // Higher level direct VME access using Stack0 and immediate exec.
        //
        // Note: Stack0 is used and the stack is written starting from offset 0
        // into stack memory. The output is sent to pipe 0, the command pipe.
        std::error_code vmeSingleRead(u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth);

        std::error_code vmeSingleWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);

        std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, QVector<u32> &dest);

        //
        // Lower level utilities
        //

        // Read a full response buffer into dest. The buffer header is passed
        // to the validator before attempting to read the rest of the response.
        // If validation fails no more data is read.
        // Note: if stack error notification buffers are received they are made
        // available via getStackErrorNotifications().
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

        // Low level read accepting any of the known buffer types (see
        // is_known_buffer_header()). Does not do any special handling for
        // stack error notification buffers as is done in readResponse().
        std::error_code readKnownBuffer(QVector<u32> &dest);
#ifndef __WIN32
        std::error_code readKnownBuffer(QVector<u32> &dest, unsigned timeout_ms);
#endif

        // Returns the response buffer containing the contents of the last read
        // operation from the MVLC.
        // After mirrorTransaction() the buffer will contain the mirror
        // response. After stackTransaction() the buffer will contain the
        // response from executing the stack.
        QVector<u32> getResponseBuffer() const;

        // Get the stack error notifications that may have resulted from the
        // previous stack operation. Performing another stack operation will
        // clear the internal buffer.
        QVector<QVector<u32>> getStackErrorNotifications() const;

        // Direct access to the per-pipe locks.
        inline Locks &getLocks() { return m_locks; }

        // Returns a copy of the stack error counts structure. This is
        // thread-safe.
        inline StackErrorCounters getStackErrorCounters() const
        {
            auto lock = m_stackErrors.lock();
            return m_stackErrors.counters;
        }

        // Returns a reference to the GuardedStackErrorCounters structure used
        // by this MVLCObject.
        inline GuardedStackErrorCounters &getGuardedStackErrorCounters()
        {
            return m_stackErrors;
        }

    public slots:
        std::error_code connect();
        std::error_code disconnect();
        void setReadTimeout(Pipe pipe, unsigned ms);
        void setWriteTimeout(Pipe pipe, unsigned ms);

    private:
        void setState(const State &newState);
        Locks &getLocks() const { return m_locks; }
        void preDialogOperation();
        void postDialogOperation();

        std::unique_ptr<AbstractImpl> m_impl;
        MVLCDialog m_dialog;
        State m_state;
        mutable Locks m_locks;
        GuardedStackErrorCounters m_stackErrors = {};
};

class LIBMVME_MVLC_EXPORT MVLCNotificationPoller: public QObject
{
    Q_OBJECT
    public:
        static const int Default_PollInterval_ms = 1000;
        static const unsigned PollReadTimeout_ms = 50;

        MVLCNotificationPoller(MVLCObject &mvlc, QObject *parent = nullptr);

        void enablePolling(int interval_ms = Default_PollInterval_ms);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 8, 0))
        void enablePolling(const std::chrono::milliseconds &interval);
#endif
        void disablePolling();

    private slots:
        void doPoll();

    private:
        MVLCObject &m_mvlc;
        QTimer m_pollTimer;
        std::atomic<bool> m_isPolling;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_QT_OBJECT_H__ */
